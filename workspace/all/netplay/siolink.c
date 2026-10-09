#define _GNU_SOURCE
#include "siolink.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "api.h"
#include "siolink_proto.h"
#include "usblink_sio.h"
#include "usblink_state.h"

#define USBLINK_STATE_PATH "/tmp/usblink.state"
#define REGISTER_RETRY_MS 100
#define DRAIN_MAX 64

_Static_assert(SIOLINK_MSG_MAX <= USBLINK_SIO_MAX, "a siolink message must fit one ULF_SIO payload");

// Every socket call is MSG_DONTWAIT and mu is never held across deliver(), so
// the core's mid-frame poll and its send never wait on the daemon, on gbalink's
// net thread, or on themselves (the core may send from inside receive).
static struct {
	pthread_mutex_t mu;
	int fd;			// -1 = not started
	int dead;		// the daemon went away: stay down, lockstep uses IP
	int registered; // the daemon echoed our REGISTER
	uint32_t last_register_ms;
	SioLinkHs hs;
	struct sockaddr_un self;
} s = {.mu = PTHREAD_MUTEX_INITIALIZER, .fd = -1};

static uint32_t now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

// Caller holds s.mu. EAGAIN (daemon socket full) just drops: the handshake
// and the lockstep layer both resend. A missing daemon retires the channel.
static int send_raw(const void* b, size_t n) {
	if (s.fd < 0 || s.dead)
		return -1;
	if (send(s.fd, b, n, MSG_DONTWAIT) == (ssize_t)n)
		return 0;
	if (errno == ECONNREFUSED || errno == ENOENT || errno == ENOTCONN) {
		LOG_info("siolink: usblink socket gone (%s), lockstep uses IP\n", strerror(errno));
		s.dead = 1;
	}
	return -1;
}

static int send_msg(uint8_t type, const void* p, int len) {
	uint8_t m[SIOLINK_MSG_MAX];
	int n = siolink_msg_encode(type, p, len, m, sizeof(m));
	return n < 0 ? -1 : send_raw(m, (size_t)n);
}

static void send_register(uint32_t now) {
	uint8_t r = USBLINK_SIO_REGISTER;
	s.last_register_ms = now;
	send_raw(&r, 1);
}

// Caller holds s.mu.
static void apply(int act) {
	uint8_t v = SIOLINK_PROTO_VERSION;
	if (act & SIOHS_SEND_HELLO)
		send_msg(ULF_HELLO, &v, 1);
	if (act & SIOHS_SEND_ACK)
		send_msg(ULF_HELLO_ACK, &v, 1);
	// Same critical section as the HELLO_ACK that asked for it, so the
	// handshake timeout cannot land between the CONFIRM leaving and the host
	// coming up. A refused send leaves the host down; its next HELLO gets a
	// fresh ACK and another CONFIRM.
	if ((act & SIOHS_SEND_CONFIRM) && send_msg(ULF_CONFIRM, &v, 1) == 0)
		act |= siolink_hs_confirm_sent(&s.hs);
	if (act & SIOHS_UP)
		LOG_info("siolink: direct channel up (%s side)\n", s.hs.side == SIOLINK_SIDE_HOST ? "USB-host" : "USB-gadget");
	if (act & SIOHS_FAIL)
		LOG_info("siolink: no direct channel (peer version %d), lockstep stays on IP\n", s.hs.peer_version);
}

int siolink_start(void) {
	pthread_mutex_lock(&s.mu);
	int running = s.fd >= 0;
	pthread_mutex_unlock(&s.mu);
	if (running)
		return 0;
	const char* off = getenv("NX_SIOLINK");
	if (off && !strcmp(off, "0")) {
		LOG_info("siolink: disabled, lockstep uses IP\n");
		return -1;
	}
	UsbLinkState st;
	if (usblink_state_read(USBLINK_STATE_PATH, &st) != 0 || strcmp(st.link, "up") != 0) {
		LOG_info("siolink: usblink is not up, lockstep uses IP\n");
		return -1;
	}
	int side = siolink_side_from_state(st.side);
	if (!side)
		return -1;
	struct sockaddr_un self, daemon;
	memset(&self, 0, sizeof(self));
	memset(&daemon, 0, sizeof(daemon));
	self.sun_family = daemon.sun_family = AF_UNIX;
	snprintf(self.sun_path, sizeof(self.sun_path), USBLINK_SIO_CLIENT_FMT, (int)getpid());
	snprintf(daemon.sun_path, sizeof(daemon.sun_path), "%s", USBLINK_SIO_SOCK);
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	unlink(self.sun_path); // left by an earlier process with our pid
	if (bind(fd, (struct sockaddr*)&self, sizeof(self)) != 0 || connect(fd, (struct sockaddr*)&daemon, sizeof(daemon)) != 0) {
		LOG_info("siolink: %s: %s, lockstep uses IP\n", USBLINK_SIO_SOCK, strerror(errno));
		close(fd);
		unlink(self.sun_path);
		return -1;
	}
	pthread_mutex_lock(&s.mu);
	s.fd = fd;
	s.self = self;
	s.dead = s.registered = 0;
	uint32_t now = now_ms();
	siolink_hs_init(&s.hs, side, now);
	send_register(now);
	int dead = s.dead;
	pthread_mutex_unlock(&s.mu);
	if (dead) {
		siolink_stop();
		return -1;
	}
	LOG_info("siolink: started (%s side)\n", side == SIOLINK_SIDE_HOST ? "USB-host" : "USB-gadget");
	return 0;
}

bool siolink_is_up(void) {
	pthread_mutex_lock(&s.mu);
	bool up = s.fd >= 0 && !s.dead && s.hs.up;
	pthread_mutex_unlock(&s.mu);
	return up;
}

int siolink_send(const void* buf, size_t len) {
	if (!buf || len == 0 || len > SIOLINK_PAYLOAD_MAX)
		return -1;
	pthread_mutex_lock(&s.mu);
	int rc = (s.fd >= 0 && !s.dead && s.hs.up) ? send_msg(ULF_DATA, buf, (int)len) : -1;
	pthread_mutex_unlock(&s.mu);
	return rc;
}

int siolink_drain(void (*deliver)(const void* buf, size_t len, void* ctx), void* ctx) {
	int delivered = 0;
	for (int i = 0; i < DRAIN_MAX; i++) {
		uint8_t buf[SIOLINK_MSG_MAX + 1], pl[SIOLINK_PAYLOAD_MAX];
		int plen = 0;
		pthread_mutex_lock(&s.mu);
		if (s.fd < 0) {
			pthread_mutex_unlock(&s.mu);
			break;
		}
		if (i == 0) {
			// Housekeeping rides the drain (every frame, and every mid-frame
			// poll): REGISTER until the daemon acks, the host's HELLO cadence
			// and its handshake timeout.
			uint32_t now = now_ms();
			if (!s.registered && now - s.last_register_ms >= REGISTER_RETRY_MS)
				send_register(now);
			apply(siolink_hs_tick(&s.hs, now));
		}
		ssize_t n = recv(s.fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (n < 0) {
			pthread_mutex_unlock(&s.mu);
			break; // EAGAIN: nothing more queued
		}
		if (n == 1 && buf[0] == USBLINK_SIO_REGISTER) {
			if (!s.registered)
				LOG_info("siolink: registered with usblink\n");
			s.registered = 1;
		} else {
			uint8_t type;
			const uint8_t* p;
			int len = siolink_msg_decode(buf, (int)n, &type, &p);
			int act = len < 0 ? 0 : siolink_hs_on_frame(&s.hs, type, p, len);
			// DATA in every phase, also after our handshake failed: the peer
			// may be up on a CONFIRM that crossed our timeout.
			if (act & SIOHS_DELIVER) {
				memcpy(pl, p, (size_t)len);
				plen = len;
			}
			apply(act & ~SIOHS_DELIVER);
		}
		pthread_mutex_unlock(&s.mu);
		if (plen) {
			deliver(pl, (size_t)plen, ctx);
			delivered++;
		}
	}
	return delivered;
}

void siolink_stop(void) {
	pthread_mutex_lock(&s.mu);
	if (s.fd >= 0) {
		close(s.fd);
		unlink(s.self.sun_path); // the daemon's next forward fails ENOENT and forgets us
		s.fd = -1;
		LOG_info("siolink: stopped\n");
	}
	s.dead = s.registered = 0;
	pthread_mutex_unlock(&s.mu);
}
