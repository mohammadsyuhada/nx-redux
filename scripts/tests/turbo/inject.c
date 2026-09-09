// Test-only evdev injector for on-device turbo checks (not part of any build).
//   inject <dev> [k<code>:<ms>] [p<code>:<period_us>:<total_ms>] [a<code>:<val>:<ms>] [s<code>:<val>] [w<ms>] ...
//   k = press key for ms then release; p = daemon-style pulse train (toggle
//   every period_us for total_ms, ends released); a = hold an ABS axis at val
//   for ms then return to 0; s = set an ABS axis to val and leave it (no
//   wait, so two axes can be held together); w = wait ms.
//
// Build with the device toolchain (must be dynamically linked: static glibc
// aborts on the Brick's 4.9 kernel), e.g. inside the tg5040 container:
//   aarch64-nextui-linux-gnu-gcc -O2 -o inject scripts/tests/turbo/inject.c
// Brick recipe used for upstream #780 (gamepad node = /dev/input/event3,
// B = 304, SELECT = 314, START = 315, MENU = 316, d-pad = ABS_HAT0Y 17):
//   touch /tmp/trimui_inputd/turbo_b            # what the FN switch / shortcut does
//   launch 1942 (FBN) via /tmp/next, then
//   inject /dev/input/event3 k314:150 w600 k315:150 w7000   # coin, start, wait for take-off
//   inject /dev/input/event3 p304:8333:6000                  # the daemon's 8 ms pulses
//   echo /tmp/shot.bmp > /sys/class/disp/disp/attr/capture_dump   # composite screenshot
// Fixed minarch shows two bullet pairs in flight; v1.9.0 showed one or none.
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int fd;
static void emit(int type, int code, int val) {
	struct input_event ev;
	memset(&ev, 0, sizeof ev);
	gettimeofday(&ev.time, NULL);
	ev.type = type;
	ev.code = code;
	ev.value = val;
	if (write(fd, &ev, sizeof ev) != sizeof ev)
		perror("write");
}
static void axis(int code, int val) {
	emit(EV_ABS, code, val);
	emit(EV_SYN, SYN_REPORT, 0);
}
static void key(int code, int val) {
	emit(EV_KEY, code, val);
	emit(EV_SYN, SYN_REPORT, 0);
}
static void sleep_us(long us) {
	struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
	nanosleep(&ts, NULL);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s <dev> <actions...>\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_WRONLY);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	for (int i = 2; i < argc; i++) {
		const char* a = argv[i];
		int code, ms;
		long period;
		if (a[0] == 'k' && sscanf(a + 1, "%d:%d", &code, &ms) == 2) {
			key(code, 1);
			sleep_us(ms * 1000L);
			key(code, 0);
		} else if (a[0] == 'p' && sscanf(a + 1, "%d:%ld:%d", &code, &period, &ms) == 3) {
			struct timespec t0, t;
			clock_gettime(CLOCK_MONOTONIC, &t0);
			int state = 0;
			long n = 0;
			for (;;) {
				clock_gettime(CLOCK_MONOTONIC, &t);
				long el = (t.tv_sec - t0.tv_sec) * 1000000L + (t.tv_nsec - t0.tv_nsec) / 1000;
				if (el >= ms * 1000L)
					break;
				state = !state;
				key(code, state);
				n++;
				sleep_us(period);
			}
			if (state)
				key(code, 0);
			fprintf(stderr, "pulse code=%d period=%ldus edges=%ld\n", code, period, n);
		} else if (a[0] == 'a' && sscanf(a + 1, "%d:%ld:%d", &code, &period, &ms) == 3) {
			axis(code, (int)period);
			sleep_us(ms * 1000L);
			axis(code, 0);
		} else if (a[0] == 's' && sscanf(a + 1, "%d:%ld", &code, &period) == 2) {
			axis(code, (int)period);
		} else if (a[0] == 'w' && sscanf(a + 1, "%d", &ms) == 1) {
			sleep_us(ms * 1000L);
		} else {
			fprintf(stderr, "bad action %s\n", a);
			return 2;
		}
	}
	close(fd);
	return 0;
}
