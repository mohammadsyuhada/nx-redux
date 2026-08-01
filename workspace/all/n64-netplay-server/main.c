/*
 * m64p-server.elf — minimal mupen64plus netplay server.
 *
 *   m64p-server.elf --port 55445 --players 2 [--buffer-target 2]
 *
 * Speaks the TCP/UDP protocol of mupen64plus-core's netplay client
 * (mupen64plus-core/src/main/netplay.c); all multi-byte wire integers are
 * big-endian. Single-threaded: one poll() loop over the TCP listener, up to
 * MS_MAX_CONNS accepted connections and one UDP socket, with a 100 ms poll
 * timeout driving the periodic ticks. Replies whose data is not available
 * yet (settings/save/registration gate) sit in a per-connection deferred
 * queue serviced every loop iteration (see tcp.c).
 *
 * Exit codes: 0 normal shutdown (all players gone), 1 socket setup failed,
 * 2 usage error.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "server.h"

// One UDP datagram; the protocol's largest packet is 508 bytes (type 1/3).
#define UDP_READ_BUF 1024

//////////////////////////////////
// Shared helpers (declared in server.h)
//////////////////////////////////

long long ms_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Timestamped line to stdout, flushed each call — the log is watched
// through a file, so buffered output would look like a hang.
void ms_log(const char* fmt, ...) {
	char stamp[32];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
	printf("[%s] ", stamp);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

int ms_registered_count(const MsServer* s) {
	int n = 0;
	for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
		if (s->players[i].reg_id != 0)
			++n;
	}
	return n;
}

int ms_uint_larger(uint32_t a, uint32_t b) {
	return (uint32_t)(b - a) > UINT32_MAX / 2;
}

//////////////////////////////////
// Startup
//////////////////////////////////

static void usage(const char* argv0) {
	fprintf(stderr,
			"usage: %s --port <1-65535> --players <1-4> [--buffer-target <1-10>]\n"
			"defaults: --port 55445 --players 2 --buffer-target 2\n",
			argv0);
	exit(2);
}

// One --flag value: parse as long, enforce [lo,hi], usage() on anything else.
static int parse_arg(const char* argv0, const char* name, const char* value,
					 long lo, long hi) {
	if (value == NULL) {
		fprintf(stderr, "%s: %s needs a value\n", argv0, name);
		usage(argv0);
	}
	char* end = NULL;
	long v = strtol(value, &end, 10);
	if (end == value || *end != '\0' || v < lo || v > hi) {
		fprintf(stderr, "%s: %s must be %ld-%ld, got \"%s\"\n", argv0, name, lo, hi, value);
		usage(argv0);
	}
	return (int)v;
}

static int set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Bound socket of the given type on INADDR_ANY:port, or -1 (perror'd).
static int open_socket(int type, int port) {
	int fd = socket(AF_INET, type, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}
	if (type == SOCK_STREAM && listen(fd, MS_MAX_CONNS) < 0) {
		perror("listen");
		close(fd);
		return -1;
	}
	if (set_nonblock(fd) < 0) {
		perror("fcntl(O_NONBLOCK)");
		close(fd);
		return -1;
	}
	return fd;
}

//////////////////////////////////
// Poll loop
//////////////////////////////////

static void accept_conns(MsServer* s) {
	for (;;) {
		int fd = accept(s->tcp_fd, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			return; // EAGAIN or a transient error; poll again next loop
		}
		MsConn* c = NULL;
		for (int i = 0; i < MS_MAX_CONNS; ++i) {
			if (s->conns[i].fd < 0) {
				c = &s->conns[i];
				break;
			}
		}
		if (c == NULL) {
			ms_log("connection table full, refusing a client");
			close(fd);
			continue;
		}
		set_nonblock(fd);
		memset(c, 0, sizeof(*c));
		c->fd = fd;
		c->request = MS_TCP_NONE;
	}
}

static void drain_udp(MsServer* s) {
	uint8_t pkt[UDP_READ_BUF];
	for (;;) {
		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);
		ssize_t n = recvfrom(s->udp_fd, pkt, sizeof(pkt), 0,
							 (struct sockaddr*)&from, &fromlen);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return; // EAGAIN: drained
		}
		ms_udp_handle_packet(s, pkt, (size_t)n, &from);
	}
}

/*
 * Read everything currently available on the conn. Returns 1 while the peer
 * is still connected, 0 on EOF/error — in which case any bytes already
 * buffered must still be parsed before the conn is closed: the mupen64plus
 * client sends its DISCONNECT_NOTICE and closes the socket right away
 * (netplay.c:189-196), so the notice can arrive in the same poll wake as
 * the FIN. Only an unrecoverable buffer condition closes the conn here.
 */
static int read_conn(MsServer* s, MsConn* c) {
	for (;;) {
		if (c->len == c->cap) {
			if (c->cap >= MS_CONN_BUF_MAX) {
				ms_log("connection buffer exceeded %u bytes, closing", (unsigned)MS_CONN_BUF_MAX);
				ms_conn_close(s, c);
				return 0;
			}
			size_t cap = c->cap ? c->cap * 2 : 4096;
			if (cap > MS_CONN_BUF_MAX)
				cap = MS_CONN_BUF_MAX;
			uint8_t* buf = realloc(c->buf, cap);
			if (buf == NULL) {
				ms_log("out of memory growing a connection buffer, closing");
				ms_conn_close(s, c);
				return 0;
			}
			c->buf = buf;
			c->cap = cap;
		}
		ssize_t n = read(c->fd, c->buf + c->len, c->cap - c->len);
		if (n > 0) {
			c->len += (size_t)n;
			continue;
		}
		if (n == 0)
			return 0; // peer closed; parse what's buffered, then close
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 1;
		return 0;
	}
}

int main(int argc, char* argv[]) {
	int port = 55445;
	int players = 2;
	int buffer_target = 2;

	for (int i = 1; i < argc; ++i) {
		const char* next = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (strcmp(argv[i], "--port") == 0) {
			port = parse_arg(argv[0], "--port", next, 1, 65535);
			++i;
		} else if (strcmp(argv[i], "--players") == 0) {
			players = parse_arg(argv[0], "--players", next, 1, MS_MAX_PLAYERS);
			++i;
		} else if (strcmp(argv[i], "--buffer-target") == 0) {
			buffer_target = parse_arg(argv[0], "--buffer-target", next, 1, 10);
			++i;
		} else {
			fprintf(stderr, "%s: unknown argument \"%s\"\n", argv[0], argv[i]);
			usage(argv[0]);
		}
	}

	// A client vanishing mid-write must not kill the server.
	signal(SIGPIPE, SIG_IGN);

	static MsServer server; // MsPlayer's input rings make this too big for the stack
	MsServer* s = &server;
	s->port = port;
	s->expected_players = players;
	s->buffer_target = buffer_target;
	s->buffer_size = 3;
	for (int i = 0; i < MS_MAX_CONNS; ++i) {
		s->conns[i].fd = -1;
		s->conns[i].request = MS_TCP_NONE;
	}

	s->tcp_fd = open_socket(SOCK_STREAM, port);
	s->udp_fd = (s->tcp_fd >= 0) ? open_socket(SOCK_DGRAM, port) : -1;
	if (s->tcp_fd < 0 || s->udp_fd < 0) {
		fprintf(stderr, "failed to open port %d\n", port);
		return 1;
	}

	ms_log("listening on port %d (%d players, buffer target %d)",
		   port, players, buffer_target);

	long long last_buffer_tick = ms_now_ms();
	long long last_sweep_tick = last_buffer_tick; // first sweep at startup+30 s

	for (;;) {
		// [0]=tcp listener, [1]=udp, [2..]=live conns
		struct pollfd pfds[2 + MS_MAX_CONNS];
		MsConn* pfd_conn[2 + MS_MAX_CONNS];
		pfds[0].fd = s->tcp_fd;
		pfds[0].events = POLLIN;
		pfds[1].fd = s->udp_fd;
		pfds[1].events = POLLIN;
		int nfds = 2;
		for (int i = 0; i < MS_MAX_CONNS; ++i) {
			if (s->conns[i].fd < 0)
				continue;
			pfds[nfds].fd = s->conns[i].fd;
			pfds[nfds].events = POLLIN;
			pfd_conn[nfds] = &s->conns[i];
			++nfds;
		}

		int pr = poll(pfds, (nfds_t)nfds, 100);
		if (pr < 0 && errno != EINTR) {
			perror("poll");
			return 1;
		}

		if (pr > 0) {
			if (pfds[0].revents & POLLIN)
				accept_conns(s);
			if (pfds[1].revents & POLLIN)
				drain_udp(s);
			for (int i = 2; i < nfds; ++i) {
				MsConn* c = pfd_conn[i];
				if (c->fd < 0)
					continue; // closed earlier this iteration
				if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR)))
					continue;
				int still_open = read_conn(s, c);
				if (c->fd < 0)
					continue; // read_conn hit an unrecoverable state
				ms_tcp_handle_conn(s, c);
				if (!still_open && c->fd >= 0)
					ms_conn_close(s, c);
			}
		}

		ms_tcp_service_deferred(s);

		long long now = ms_now_ms();
		if (now - last_buffer_tick >= 1000) {
			ms_tick_buffer(s);
			last_buffer_tick = now;
		}
		if (now - last_sweep_tick >= 30000) {
			if (ms_tick_sweep(s)) {
				ms_log("no players remain, shutting down");
				return 0;
			}
			last_sweep_tick = now;
		}
	}
}
