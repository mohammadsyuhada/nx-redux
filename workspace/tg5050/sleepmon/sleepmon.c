// sleepmon - lightweight power button sleep/poweroff service
//
// Monitors input devices for the power button (KEY_POWER = 116).
// Short press: suspend (backlight off → /bin/suspend → backlight on)
// Long press (1s+): poweroff
//
// Usage: sleepmon.elf &
// Stop:  killall sleepmon.elf

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

#include <msettings.h>

#define KEY_POWER 116
#define POWEROFF_HOLD_MS 1000

#define MAX_INPUT_DEVICES 16
#define INPUT_DIR "/dev/input"

#define EPOLL_TAG_INOTIFY 0xFF000000
#define EPOLL_TAG_INPUT 0x00000000

static int input_fds[MAX_INPUT_DEVICES];
static int input_count = 0;
static int epoll_fd = -1;

static volatile int quit = 0;
static void on_term(int sig) {
	quit = 1;
}

static uint32_t now_ms(void) {
	struct timeval tod;
	gettimeofday(&tod, NULL);
	return tod.tv_sec * 1000 + tod.tv_usec / 1000;
}

static int open_input_device(const char* path, int index) {
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.u32 = EPOLL_TAG_INPUT | index;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void close_input_device(int index) {
	if (input_fds[index] < 0)
		return;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, input_fds[index], NULL);
	close(input_fds[index]);
	input_fds[index] = -1;
}

static void scan_input_devices(void) {
	char path[64];

	for (int i = 0; i < input_count; i++)
		close_input_device(i);
	input_count = 0;

	for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
		snprintf(path, sizeof(path), INPUT_DIR "/event%d", i);
		int fd = open_input_device(path, i);
		input_fds[i] = fd;
		if (i >= input_count && fd >= 0)
			input_count = i + 1;
	}
}

static void do_suspend(void) {
	// Turn off backlight
	SetRawBrightness(0);

	// Mute volume
	SetRawVolume(0);

	// Call platform suspend script (try PATH first, then known locations)
	if (system("suspend") != 0) {
		system("/mnt/SDCARD/.system/" PLATFORM "/bin/suspend");
	}

	// Restore backlight and volume on wake
	SetBrightness(GetBrightness());
	SetVolume(GetVolume());
}

static void do_poweroff(void) {
	// Signal the main loop to poweroff
	FILE* f = fopen("/tmp/poweroff", "w");
	if (f)
		fclose(f);
	sync();
	// Kill the foreground app so the MinUI loop picks up /tmp/poweroff
	system("killall -TERM minarch.elf 2>/dev/null");
	system("killall -TERM nextui.elf 2>/dev/null");
	// Fallback: direct poweroff after a brief delay
	usleep(3000000);
	system("poweroff");
}

int main(int argc, char* argv[]) {
	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	InitSettings();

	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		perror("epoll_create1");
		return 1;
	}

	int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotify_fd < 0) {
		perror("inotify_init1");
		return 1;
	}
	inotify_add_watch(inotify_fd, INPUT_DIR, IN_CREATE | IN_DELETE);

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.u32 = EPOLL_TAG_INOTIFY;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev);

	memset(input_fds, -1, sizeof(input_fds));
	scan_input_devices();

	uint32_t power_pressed_at = 0;
	int power_held = 0;
	uint32_t resume_at = 0;
	uint32_t then = now_ms();

	struct epoll_event events[16];
	struct input_event input_ev;

	while (!quit) {
		int timeout_ms = power_held ? 50 : 1000;

		int nfds = epoll_wait(epoll_fd, events, 16, timeout_ms);
		if (nfds < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		uint32_t now = now_ms();

		// Ignore stale input after suspend/resume (time jump)
		if (now - then > 2000) {
			power_pressed_at = 0;
			power_held = 0;
			for (int i = 0; i < input_count; i++) {
				if (input_fds[i] < 0)
					continue;
				while (read(input_fds[i], &input_ev, sizeof(input_ev)) > 0)
					;
			}
			then = now;
			continue;
		}
		then = now;

		for (int n = 0; n < nfds; n++) {
			uint32_t tag = events[n].data.u32;

			if (tag == EPOLL_TAG_INOTIFY) {
				char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
				while (read(inotify_fd, buf, sizeof(buf)) > 0)
					;
				scan_input_devices();
				continue;
			}

			int idx = tag & 0x00FFFFFF;
			if (idx >= MAX_INPUT_DEVICES || input_fds[idx] < 0)
				continue;

			while (read(input_fds[idx], &input_ev, sizeof(input_ev)) == sizeof(input_ev)) {
				if (input_ev.type != EV_KEY || input_ev.code != KEY_POWER)
					continue;

				if (input_ev.value == 1) {
					// Press — ignore if within 1s of resume
					if (resume_at && now - resume_at < 1000) {
						power_pressed_at = 0;
						power_held = 0;
					} else {
						power_pressed_at = now;
						power_held = 1;
					}
				} else if (input_ev.value == 0) {
					// Release
					if (power_held && power_pressed_at &&
						now - power_pressed_at < POWEROFF_HOLD_MS) {
						do_suspend();
						// Drain all pending input after wake
						for (int i = 0; i < input_count; i++) {
							if (input_fds[i] < 0)
								continue;
							struct input_event drain;
							while (read(input_fds[i], &drain, sizeof(drain)) > 0)
								;
						}
						resume_at = now_ms();
						then = resume_at;
					}
					power_pressed_at = 0;
					power_held = 0;
				}
			}
		}

		// Check for long press → poweroff
		if (power_held && power_pressed_at &&
			now - power_pressed_at >= POWEROFF_HOLD_MS) {
			do_poweroff();
			// Should not return, but just in case
			power_pressed_at = 0;
			power_held = 0;
		}
	}

	for (int i = 0; i < input_count; i++)
		close_input_device(i);
	close(inotify_fd);
	close(epoll_fd);

	return 0;
}
