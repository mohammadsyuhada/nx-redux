#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

#include <msettings.h>

#define VOLUME_MIN 0
#define VOLUME_MAX 20
#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 10
#define COLORTEMP_MIN 0
#define COLORTEMP_MAX 40

#define CODE_MENU0 314
#define CODE_MENU1 315
#define CODE_MENU2 316
#define CODE_PLUS 115
#define CODE_MINUS 114
#define CODE_MUTE 1
#define CODE_JACK 2

#define RELEASED 0
#define PRESSED 1
#define REPEAT 2

#define MUTE_STATE_PATH "/sys/class/gpio/gpio243/value"

#define MAX_INPUT_DEVICES 16
#define INPUT_DIR "/dev/input"
#define REPEAT_DELAY_MS 300
#define REPEAT_RATE_MS 100

// epoll data tags — use high bits to distinguish input fds from inotify fd
#define EPOLL_TAG_INOTIFY 0xFF000000
#define EPOLL_TAG_INPUT 0x00000000

static int input_fds[MAX_INPUT_DEVICES];
static int input_count = 0;
static int epoll_fd = -1;

static volatile int quit = 0;
static void on_term(int sig) {
	quit = 1;
}

static int getInt(char* path) {
	int i = 0;
	FILE* file = fopen(path, "r");
	if (file != NULL) {
		fscanf(file, "%i", &i);
		fclose(file);
	}
	return i;
}

static pthread_t mute_pt;
static void* watchMute(void* arg) {
	int is_muted, was_muted;

	is_muted = was_muted = getInt(MUTE_STATE_PATH);
	SetMute(is_muted);

	while (!quit) {
		usleep(1000000);

		is_muted = getInt(MUTE_STATE_PATH);
		if (is_muted >= 0 && was_muted != is_muted) {
			was_muted = is_muted;
			SetMute(is_muted);
			if (GetMute()) {
				system("echo 1500000 > /sys/class/motor/voltage");
				system("echo 1 > /sys/class/gpio/gpio227/value");
				usleep(100000);
				system("echo 0 > /sys/class/gpio/gpio227/value");
				usleep(100000);
				system("echo 1 > /sys/class/gpio/gpio227/value");
				usleep(100000);
				system("echo 0 > /sys/class/gpio/gpio227/value");
			}
		}
	}

	return NULL;
}

// Open an input device and add it to epoll. Returns fd or -1.
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

// Close an input device and remove from epoll.
static void close_input_device(int index) {
	if (input_fds[index] < 0)
		return;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, input_fds[index], NULL);
	close(input_fds[index]);
	input_fds[index] = -1;
}

// Scan /dev/input/ and open all event devices.
static void scan_input_devices(void) {
	char path[64];

	// Close all existing
	for (int i = 0; i < input_count; i++)
		close_input_device(i);
	input_count = 0;

	// Open all event devices that exist
	for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
		snprintf(path, sizeof(path), INPUT_DIR "/event%d", i);
		int fd = open_input_device(path, i);
		input_fds[i] = fd;
		if (i >= input_count && fd >= 0)
			input_count = i + 1;
	}
}

static uint32_t now_ms(void) {
	struct timeval tod;
	gettimeofday(&tod, NULL);
	return tod.tv_sec * 1000 + tod.tv_usec / 1000;
}

int main(int argc, char* argv[]) {
	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);

	InitSettings();
	pthread_create(&mute_pt, NULL, &watchMute, NULL);

	// Create epoll instance
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		perror("epoll_create1");
		return 1;
	}

	// Set up inotify on /dev/input/ for hotplug detection
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

	// Initial device scan
	memset(input_fds, -1, sizeof(input_fds));
	scan_input_devices();

	// Input state
	uint32_t val;
	uint32_t menu_pressed = 0;
	uint32_t menu2_pressed = 0;

	uint32_t up_pressed = 0;
	uint32_t up_just_pressed = 0;
	uint32_t up_repeat_at = 0;

	uint32_t down_pressed = 0;
	uint32_t down_just_pressed = 0;
	uint32_t down_repeat_at = 0;

	uint32_t then = now_ms();

	struct epoll_event events[16];
	struct input_event input_ev;

	while (!quit) {
		// Compute timeout: if a key is held, wake up for repeat; otherwise block longer
		int timeout_ms;
		uint32_t now = now_ms();
		if (up_pressed || down_pressed) {
			uint32_t next_repeat = up_pressed ? up_repeat_at : down_repeat_at;
			if (up_pressed && down_pressed)
				next_repeat = up_repeat_at < down_repeat_at ? up_repeat_at : down_repeat_at;
			int remaining = (int)(next_repeat - now);
			timeout_ms = remaining > 0 ? remaining : 1;
		} else {
			timeout_ms = 1000; // idle — wake up occasionally
		}

		int nfds = epoll_wait(epoll_fd, events, 16, timeout_ms);
		if (nfds < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		now = now_ms();
		if (now - then > 1000) {
			// Ignore stale input after sleep
			menu_pressed = 0;
			menu2_pressed = 0;
			up_pressed = up_just_pressed = 0;
			down_pressed = down_just_pressed = 0;
			up_repeat_at = 0;
			down_repeat_at = 0;
			// Drain all input fds
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
				// Hotplug event — drain inotify buffer and rescan devices
				char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
				while (read(inotify_fd, buf, sizeof(buf)) > 0)
					;
				scan_input_devices();
				continue;
			}

			// Input event from an event device
			int idx = tag & 0x00FFFFFF;
			if (idx >= MAX_INPUT_DEVICES || input_fds[idx] < 0)
				continue;

			while (read(input_fds[idx], &input_ev, sizeof(input_ev)) == sizeof(input_ev)) {
				if (input_ev.type == EV_SW) {
					if (input_ev.code == CODE_JACK)
						SetJack(input_ev.value);
				}
				val = input_ev.value;
				if (input_ev.type != EV_KEY || val > REPEAT)
					continue;
				switch (input_ev.code) {
				case CODE_PLUS:
					up_pressed = up_just_pressed = val;
					if (val)
						up_repeat_at = now + REPEAT_DELAY_MS;
					break;
				case CODE_MINUS:
					down_pressed = down_just_pressed = val;
					if (val)
						down_repeat_at = now + REPEAT_DELAY_MS;
					break;
				case CODE_MENU2:
					menu_pressed = val;
					break;
				case CODE_MENU0:
					menu2_pressed = val;
					break;
				default:
					break;
				}
			}
		}

		// Handle key repeat for volume/brightness
		if (up_just_pressed || (up_pressed && now >= up_repeat_at)) {
			if (menu_pressed) {
				val = GetBrightness();
				if (val < BRIGHTNESS_MAX)
					SetBrightness(++val);
			} else if (menu2_pressed) {
				val = GetColortemp();
				if (val < COLORTEMP_MAX)
					SetColortemp(++val);
			} else {
				val = GetVolume();
				if (val < VOLUME_MAX)
					SetVolume(++val);
			}

			if (up_just_pressed)
				up_just_pressed = 0;
			else
				up_repeat_at += REPEAT_RATE_MS;
		}

		if (down_just_pressed || (down_pressed && now >= down_repeat_at)) {
			if (menu_pressed) {
				val = GetBrightness();
				if (val > BRIGHTNESS_MIN)
					SetBrightness(--val);
			} else if (menu2_pressed) {
				val = GetColortemp();
				if (val > COLORTEMP_MIN)
					SetColortemp(--val);
			} else {
				val = GetVolume();
				if (val > VOLUME_MIN)
					SetVolume(--val);
			}

			if (down_just_pressed)
				down_just_pressed = 0;
			else
				down_repeat_at += REPEAT_RATE_MS;
		}
	}

	// Cleanup
	for (int i = 0; i < input_count; i++)
		close_input_device(i);
	close(inotify_fd);
	close(epoll_fd);

	pthread_cancel(mute_pt);
	pthread_join(mute_pt, NULL);
}
