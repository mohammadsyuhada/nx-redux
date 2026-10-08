// usblink.elf: carries IP packets between two handhelds over one USB cable.
//
//   usblink.elf start   daemonize; 0 when a daemon is running (new or already)
//   usblink.elf stop    always 0; no daemon, no ffs.net, no pid/state/udc files
//   usblink.elf status  prints /tmp/usblink.state, or USBLINK_LINK=down
//   usblink.elf run     the daemon in the foreground (debugging); ignores SIGHUP,
//                       because attach rebinds the UDC and drops an adb shell
//
// The same daemon runs on both handhelds and does not know which end of the
// cable it is on: it exposes an "nxlink" FunctionFS interface on its gadget
// port and, at the same time, looks for that interface on its USB host port.
// Whichever side hears the other first brings nxlink0 up (see usblink_link.c).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <endian.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "usblink_frame.h"
#include "usblink_gadget.h"
#include "usblink_host.h"
#include "usblink_link.h"
#include "usblink_state.h"
#include "usblink_tun.h"

#define PID_FILE "/tmp/usblink.pid"
#define STATE_FILE "/tmp/usblink.state"
#define STATE_TMP STATE_FILE ".tmp" // usblink_state_write's temp name
#define UDC_SAVE "/tmp/usblink.udc"
#define G_UDC "/sys/kernel/config/usb_gadget/g1/UDC"
#define TUN_NAME "nxlink0"
#define HOST_IP "10.99.0.1"
#define DEVICE_IP "10.99.0.2"
#define TICK_MS 50
#define HOST_READ_TIMEOUT_MS 200 // bounds how long host_io holds nothing but a read
#define HOST_SCAN_MS 250
#define START_WAIT_MS 3000 // `start` waits this long for the daemon's first state

// Link state machine; guarded by mu. mu only covers state changes and the
// tun address ioctls, never endpoint I/O: a gadget write blocks until the peer
// reads, and holding mu across it would stall the tick that declares the link
// dead. Each endpoint has its own write lock so frames never interleave.
static UsbLink g_link;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gadget_wr = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t host_wr = PTHREAD_MUTEX_INITIALIZER; // also guards claim/release of host.fd
static UsbLinkHost host = {.fd = -1};
static volatile int host_claimed;
static int g_out = -1, g_in = -1, tun_fd = -1;
static char last_error[32];

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

static void sleep_ms(int ms) {
	struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
	nanosleep(&ts, NULL);
}

static void block_signals(sigset_t* old) {
	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, SIGTERM);
	sigaddset(&s, SIGINT);
	sigaddset(&s, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &s, old);
}

// True when the stock gadget is bound to a UDC. Async-signal-safe.
static int udc_bound(void) {
	char c = 0;
	int fd = open(G_UDC, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	int r = (int)read(fd, &c, 1);
	close(fd);
	return r == 1 && c != '\n';
}

// The whole teardown, shared by the signal handler and the fatal paths, so it
// only uses async-signal-safe calls. ffs.net must leave c.1 before the process
// (and with it ep0) goes away, or the gadget cannot rebind and adb is dead.
static void shutdown_and_exit(int code, int keep_state) {
	usblink_gadget_detach();
	// Only a gadget that is bound again proves the detach worked; otherwise
	// the save file stays so `stop`'s repair can still rebind it.
	if (udc_bound())
		unlink(UDC_SAVE);
	if (host.fd >= 0)
		close(host.fd); // the kernel releases the claimed interface with the fd
	if (tun_fd >= 0)
		close(tun_fd); // nxlink0 vanishes with its last fd
	if (!keep_state)
		unlink(STATE_FILE);
	unlink(STATE_TMP);
	unlink(PID_FILE);
	_exit(code);
}

// Only the main thread takes these (workers are created with them blocked).
static void on_signal(int sig) {
	(void)sig;
	shutdown_and_exit(0, 0);
}

// Startup failure: leave a state file that tells the wizard why.
static void fatal(const char* error) {
	sigset_t old;
	block_signals(&old);
	UsbLinkState st;
	memset(&st, 0, sizeof(st));
	strcpy(st.link, "error");
	snprintf(st.error, sizeof(st.error), "%s", error);
	usblink_state_write(STATE_FILE, &st);
	fprintf(stderr, "usblink: fatal: %s\n", error);
	shutdown_and_exit(1, 1);
}

static void publish(void) {
	UsbLinkState st;
	usblink_state_from_link(&g_link, last_error, &st);
	if (usblink_state_write(STATE_FILE, &st) != 0)
		fprintf(stderr, "usblink: write %s: %s\n", STATE_FILE, strerror(errno));
}

// Applies the state-side effects of a USBLINK_ACT_* set (mu held) and returns
// the sends still to do, which the caller performs after dropping mu.
static int apply(int act) {
	if (act & USBLINK_ACT_UP) {
		int host_side = (g_link.side == USBLINK_SIDE_HOST);
		const char* local = host_side ? HOST_IP : DEVICE_IP;
		const char* peer = host_side ? DEVICE_IP : HOST_IP;
		if (usblink_tun_set_up(TUN_NAME, local, peer) != 0)
			fprintf(stderr, "usblink: %s up failed\n", TUN_NAME);
		fprintf(stderr, "usblink: link up via %s port, %s -> %s\n", host_side ? "host" : "gadget", local, peer);
		publish();
	}
	if (act & USBLINK_ACT_DOWN) {
		usblink_tun_set_down(TUN_NAME);
		fprintf(stderr, "usblink: link down\n");
		publish();
	}
	if (act & USBLINK_ACT_ERROR) {
		strcpy(last_error, "version");
		fprintf(stderr, "usblink: peer speaks protocol %d, we speak %d\n", g_link.peer_version, USBLINK_PROTO_VERSION);
		publish();
	}
	return act & (USBLINK_ACT_SEND_HELLO | USBLINK_ACT_SEND_ACK);
}

static void gadget_send_raw(const uint8_t* frame, int n) {
	pthread_mutex_lock(&gadget_wr);
	if (write(g_in, frame, (size_t)n) != n && errno != ESHUTDOWN)
		fprintf(stderr, "usblink: gadget write: %s\n", strerror(errno));
	pthread_mutex_unlock(&gadget_wr);
}

static void host_send_raw(const uint8_t* frame, int n) {
	pthread_mutex_lock(&host_wr);
	if (host.fd >= 0 && usblink_host_write(&host, frame, n) < 0)
		fprintf(stderr, "usblink: host write: %s\n", strerror(errno));
	pthread_mutex_unlock(&host_wr);
}

static void do_sends(int sends) {
	uint8_t frame[16];
	uint8_t ver = USBLINK_PROTO_VERSION;
	if (sends & USBLINK_ACT_SEND_ACK) {
		int n = usblink_frame_encode(ULF_HELLO_ACK, &ver, 1, frame, sizeof(frame));
		if (n > 0)
			gadget_send_raw(frame, n);
	}
	if (sends & USBLINK_ACT_SEND_HELLO) {
		int n = usblink_frame_encode(ULF_HELLO, &ver, 1, frame, sizeof(frame));
		if (n > 0)
			host_send_raw(frame, n);
	}
}

static void on_frame(UsbLinkSide via, const uint8_t* buf, int n) {
	uint8_t type;
	const uint8_t* payload;
	int len = usblink_frame_decode(buf, n, &type, &payload);
	if (len < 0)
		return;
	pthread_mutex_lock(&mu);
	int act = usblink_link_on_frame(&g_link, via, type, payload, len, now_ms());
	int sends = apply(act);
	pthread_mutex_unlock(&mu);
	if ((act & USBLINK_ACT_DELIVER) && write(tun_fd, payload, (size_t)len) != len)
		fprintf(stderr, "usblink: tun write: %s\n", strerror(errno));
	do_sends(sends);
}

// Frames from the peer's USB host arriving on our gadget OUT endpoint. Reads
// fail (ESHUTDOWN) while no host has the function enabled; that is normal.
static void* gadget_rx(void* arg) {
	(void)arg;
	static uint8_t buf[USBLINK_FRAME_MAX];
	for (;;) {
		int n = (int)read(g_out, buf, sizeof(buf));
		if (n <= 0) {
			sleep_ms(100);
			continue;
		}
		on_frame(USBLINK_SIDE_DEVICE, buf, n);
	}
	return NULL;
}

static const char* ffs_event_name(int type) {
	switch (type) {
	case FUNCTIONFS_BIND:
		return "bind";
	case FUNCTIONFS_UNBIND:
		return "unbind";
	case FUNCTIONFS_ENABLE:
		return "enable";
	case FUNCTIONFS_DISABLE:
		return "disable";
	case FUNCTIONFS_SETUP:
		return "setup";
	case FUNCTIONFS_SUSPEND:
		return "suspend";
	case FUNCTIONFS_RESUME:
		return "resume";
	}
	return "?";
}

// nxlink has no control requests of its own. FunctionFS hands any request
// addressed to our interface to user space and holds the control pipe until we
// answer, so an unanswered one stalls enumeration until the host resets the
// port. Each is refused with a stall: a zero-length transfer in the direction
// opposite to the data stage (that it "fails" with EL2HLT/EBADMSG is the stall).
// Each distinct request is logged once, to learn what hosts ask.
static void stall_setup(int fd, const struct usb_ctrlrequest* r) {
	static uint32_t seen[16];
	static int nseen;
	uint32_t key = (uint32_t)r->bRequestType << 24 | (uint32_t)r->bRequest << 16 | le16toh(r->wValue);
	int known = 0;
	for (int i = 0; i < nseen; i++)
		known |= seen[i] == key;
	if (!known) {
		if (nseen < (int)(sizeof(seen) / sizeof(seen[0])))
			seen[nseen++] = key;
		fprintf(stderr, "usblink: gadget setup bRequestType=%02x bRequest=%02x wValue=%04x wIndex=%04x wLength=%u (stalled)\n",
				r->bRequestType, r->bRequest, le16toh(r->wValue), le16toh(r->wIndex), le16toh(r->wLength));
	}
	int rc = (r->bRequestType & USB_DIR_IN) ? (int)read(fd, NULL, 0) : (int)write(fd, NULL, 0);
	if (rc < 0 && errno != EL2HLT && errno != EBADMSG)
		fprintf(stderr, "usblink: gadget setup stall: %s\n", strerror(errno));
}

// FunctionFS queues events on ep0; they must be drained or the queue fills.
static void* ep0_drain(void* arg) {
	(void)arg;
	struct usb_functionfs_event ev[4];
	for (;;) {
		int fd = usblink_gadget_ep0_fd();
		int n = fd < 0 ? -1 : (int)read(fd, ev, sizeof(ev));
		if (n <= 0) {
			sleep_ms(100);
			continue;
		}
		for (int i = 0; i < n / (int)sizeof(ev[0]); i++) {
			if (ev[i].type == FUNCTIONFS_SETUP)
				stall_setup(fd, &ev[i].u.setup);
			else
				fprintf(stderr, "usblink: gadget %s\n", ffs_event_name(ev[i].type));
		}
	}
	return NULL;
}

// Our USB host port: claim the peer's nxlink interface when it appears, then
// read frames from it until it goes away.
static void* host_io(void* arg) {
	(void)arg;
	static uint8_t buf[USBLINK_FRAME_MAX];
	for (;;) {
		if (!host_claimed) {
			pthread_mutex_lock(&host_wr);
			int ok = usblink_host_find_and_claim(&host) == 0;
			pthread_mutex_unlock(&host_wr);
			if (ok) {
				host_claimed = 1;
				fprintf(stderr, "usblink: peer found on host port\n");
			} else
				sleep_ms(HOST_SCAN_MS);
			continue;
		}
		int n = usblink_host_read(&host, buf, sizeof(buf), HOST_READ_TIMEOUT_MS);
		if (n > 0) {
			on_frame(USBLINK_SIDE_HOST, buf, n);
			continue;
		}
		if (n == 0)
			continue;
		if (errno == ENODEV || errno == ESHUTDOWN || errno == EPROTO || errno == ENOENT) {
			fprintf(stderr, "usblink: peer gone from host port (%s)\n", strerror(errno));
			pthread_mutex_lock(&mu);
			int sends = apply(usblink_link_on_host_detached(&g_link));
			pthread_mutex_unlock(&mu);
			do_sends(sends);
			pthread_mutex_lock(&host_wr);
			host_claimed = 0;
			usblink_host_release(&host);
			pthread_mutex_unlock(&host_wr);
		} else
			sleep_ms(10);
	}
	return NULL;
}

// Outgoing IP packets: framed and sent over whichever port the link is up on.
static void* tun_rx(void* arg) {
	(void)arg;
	static uint8_t pkt[USBLINK_FRAME_MAX];
	static uint8_t frame[USBLINK_FRAME_MAX];
	for (;;) {
		int n = (int)read(tun_fd, pkt, sizeof(pkt));
		if (n <= 0) {
			sleep_ms(100);
			continue;
		}
		pthread_mutex_lock(&mu);
		int up = g_link.phase == USBLINK_UP;
		UsbLinkSide side = g_link.side;
		pthread_mutex_unlock(&mu);
		if (!up)
			continue;
		int fn = usblink_frame_encode(ULF_DATA, pkt, n, frame, sizeof(frame));
		if (fn < 0)
			continue;
		if (side == USBLINK_SIDE_HOST)
			host_send_raw(frame, fn);
		else
			gadget_send_raw(frame, fn);
	}
	return NULL;
}

static int write_pid_file(void) {
	char s[16];
	int len = snprintf(s, sizeof(s), "%d\n", (int)getpid());
	int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return -1;
	int r = (int)write(fd, s, (size_t)len);
	close(fd);
	return r == len ? 0 : -1;
}

// pid of a live usblink daemon named by the pid file, or 0. The cmdline check
// keeps a recycled pid (or a zombie) from counting as ours.
static pid_t running_pid(void) {
	FILE* f = fopen(PID_FILE, "r");
	if (!f)
		return 0;
	int pid = 0;
	if (fscanf(f, "%d", &pid) != 1)
		pid = 0;
	fclose(f);
	if (pid <= 0 || pid == getpid() || kill(pid, 0) != 0)
		return 0;
	char path[64], cmd[256];
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	int n = (int)read(fd, cmd, sizeof(cmd) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	cmd[n] = 0;
	for (int i = 0; i < n; i++)
		if (!cmd[i])
			cmd[i] = ' ';
	return strstr(cmd, "usblink") ? pid : 0;
}

static int run_daemon(int foreground) {
	// Signals stay blocked through attach and thread creation: workers inherit
	// the mask, so only the main thread ever runs the shutdown handler, and a
	// signal cannot land halfway through the configfs relink.
	sigset_t old;
	block_signals(&old);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigfillset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	// In `run` the controlling terminal is usually an adb shell, which our own
	// UDC rebind hangs up; that must not take the daemon down with it.
	if (foreground)
		signal(SIGHUP, SIG_IGN);
	else
		sigaction(SIGHUP, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	if (write_pid_file() != 0)
		fprintf(stderr, "usblink: write %s: %s\n", PID_FILE, strerror(errno));
	usblink_link_init(&g_link);

	// A SIGKILLed predecessor leaves ffs.net linked (or the gadget unbound),
	// and attach would refuse an unbound gadget; repair is a no-op otherwise.
	usblink_gadget_repair();

	char ko[512];
	const char* sys = getenv("SYSTEM_PATH");
	snprintf(ko, sizeof(ko), "%s/lib/modules/tun.ko", sys && *sys ? sys : "/mnt/SDCARD/.system");
	if (usblink_tun_ensure_module(ko) != 0)
		fatal("tun");
	tun_fd = usblink_tun_open(TUN_NAME);
	if (tun_fd < 0)
		fatal("tun");
	// No gadget (UDC unbound, configfs refused) only rules out a peer on our
	// main port; the host port can still claim one, so run host-only.
	int gadget_ok = usblink_gadget_attach(&g_out, &g_in) == 0;
	if (gadget_ok)
		fprintf(stderr, "usblink: attached, waiting for a peer\n");
	else
		fprintf(stderr, "usblink: gadget unavailable, host port only\n");

	pthread_mutex_lock(&mu);
	publish();
	pthread_mutex_unlock(&mu);

	void* (*workers[])(void*) = {host_io, tun_rx, gadget_rx, ep0_drain};
	unsigned nworkers = gadget_ok ? 4 : 2;
	for (unsigned i = 0; i < nworkers; i++) {
		pthread_t t;
		if (pthread_create(&t, NULL, workers[i], NULL) != 0)
			fatal("thread");
		pthread_detach(t);
	}
	pthread_sigmask(SIG_SETMASK, &old, NULL);

	for (;;) {
		sleep_ms(TICK_MS);
		pthread_mutex_lock(&mu);
		int sends = apply(usblink_link_tick(&g_link, host_claimed, now_ms()));
		pthread_mutex_unlock(&mu);
		do_sends(sends);
	}
	return 0;
}

static void redirect_stdio(void) {
	char path[512];
	const char* logs = getenv("LOGS_PATH");
	if (logs && *logs)
		snprintf(path, sizeof(path), "%s/usblink.txt", logs);
	else
		snprintf(path, sizeof(path), "/tmp/usblink.log");
	int in = open("/dev/null", O_RDONLY);
	int out = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (in >= 0) {
		dup2(in, 0);
		if (in > 2)
			close(in);
	}
	if (out >= 0) {
		dup2(out, 1);
		dup2(out, 2);
		if (out > 2)
			close(out);
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
}

// The daemon outlives the wizard into the game; it must not hold on to any
// of the wizard's descriptors. `start` waits on the state file, not a pipe,
// so nothing above stdio is needed here.
static void close_inherited_fds(void) {
	long max = sysconf(_SC_OPEN_MAX);
	if (max < 0 || max > 65536)
		max = 65536;
	for (int fd = 3; fd < (int)max; fd++)
		close(fd);
}

static int cmd_start(void) {
	if (running_pid() > 0)
		return 0;
	unlink(PID_FILE);
	// The daemon's first state file is the startup verdict, so no stale one
	// from an earlier run may answer for it (no daemon is running here).
	unlink(STATE_FILE);
	unlink(STATE_TMP);
	pid_t p = fork();
	if (p < 0)
		return 1;
	if (p == 0) {
		setsid();
		pid_t q = fork();
		if (q != 0)
			_exit(q < 0 ? 1 : 0);
		close_inherited_fds();
		redirect_stdio();
		_exit(run_daemon(0));
	}
	waitpid(p, NULL, 0);
	// The daemon publishes "down" once tun is up and the gadget attached (or
	// given up on: host-only), or "error" on a startup failure. Budget covers
	// tun.ko loading (up to 1 s) plus the UDC rebind in attach.
	for (int i = 0; i < START_WAIT_MS / 50; i++) {
		UsbLinkState st;
		if (usblink_state_read(STATE_FILE, &st) == 0 && st.link[0])
			return strcmp(st.link, "error") == 0 ? 1 : 0;
		sleep_ms(50);
	}
	fprintf(stderr, "usblink: daemon did not report within %d ms\n", START_WAIT_MS);
	return 1;
}

static int cmd_stop(void) {
	pid_t pid = running_pid();
	if (pid > 0) {
		kill(pid, SIGTERM);
		for (int i = 0; i < 40 && running_pid() == pid; i++)
			sleep_ms(50);
		if (running_pid() == pid) {
			kill(pid, SIGKILL);
			for (int i = 0; i < 20 && running_pid() == pid; i++)
				sleep_ms(50);
		}
	}
	// Covers a daemon that was SIGKILLed (now or earlier): unlinks ffs.net and
	// rebinds the UDC so adb comes back. nxlink0 died with the daemon's fd.
	usblink_gadget_repair();
	unlink(PID_FILE);
	unlink(STATE_FILE);
	unlink(STATE_TMP);
	return 0;
}

static int cmd_status(void) {
	FILE* f = fopen(STATE_FILE, "r");
	if (!f) {
		puts("USBLINK_LINK=down");
		return 0;
	}
	char line[256];
	while (fgets(line, sizeof(line), f))
		fputs(line, stdout);
	fclose(f);
	return 0;
}

int main(int argc, char** argv) {
	const char* cmd = argc > 1 ? argv[1] : "";
	if (!strcmp(cmd, "start"))
		return cmd_start();
	if (!strcmp(cmd, "stop"))
		return cmd_stop();
	if (!strcmp(cmd, "status"))
		return cmd_status();
	if (!strcmp(cmd, "run")) {
		if (running_pid() > 0) {
			fprintf(stderr, "usblink: already running\n");
			return 1;
		}
		setvbuf(stderr, NULL, _IOLBF, 0);
		return run_daemon(1);
	}
	fprintf(stderr, "usage: %s start|stop|status|run\n", argv[0]);
	return 2;
}
