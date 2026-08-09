#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <linux/input.h>

#define PID_FILE "/tmp/screenshot.pid"
#define SCREENSHOT_DIR "/mnt/SDCARD/Images/Screenshots"
#define FFMPEG_PATH "/usr/bin/ffmpeg"
#define OSD_TOAST_PATH "/tmp/trimui_osd/osd_toast_msg"
#define INPUT_COUNT 5

// evdev codes for L2/R2 analog triggers
#define ABS_Z_CODE 2  // L2 trigger axis
#define ABS_RZ_CODE 5 // R2 trigger axis

#define COOLDOWN_MS 1000 // minimum ms between screenshots

static int inputs[INPUT_COUNT] = {};
static volatile int quit = 0;

static void on_term(int sig) {
	quit = 1;
}

static void cleanup(void) {
	remove(PID_FILE);
	for (int i = 0; i < INPUT_COUNT; i++) {
		if (inputs[i] >= 0)
			close(inputs[i]);
	}
}

static void mkdir_p(const char* path) {
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char* p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

#define FB_MIRROR_PATH "/tmp/fb_mirror.raw"
#define FB_MIRROR_WIDTH "1280"
#define FB_MIRROR_HEIGHT "720"
#define FB_MIRROR_VIDEO_SIZE FB_MIRROR_WIDTH "x" FB_MIRROR_HEIGHT

// Toast size:1 draws the 400px-wide bg_msg_w2.png background
#define TOAST_BG_WIDTH 400

static int screen_width(void) {
	int w = 1280;
	FILE* f = fopen("/sys/class/graphics/fb0/virtual_size", "r");
	if (f) {
		int fw = 0;
		if (fscanf(f, "%d", &fw) == 1 && fw > 0)
			w = fw;
		fclose(f);
	}
	return w;
}

// Toast rendered by trimui_osdd (ignored silently if the daemon isn't running)
static void osd_toast(const char* msg, int duration_ms) {
	FILE* f = fopen(OSD_TOAST_PATH, "w");
	if (!f)
		return;
	fprintf(f,
			"{\n"
			"    \"type\":\"default\",\n"
			"    \"id\":\"com.trimui.osd.msg.default\",\n"
			"    \"duration\":%d,\n"
			"    \"size\":1,\n"
			"    \"x\":%d,\n"
			"    \"y\":500,\n"
			"    \"w\":300,\n"
			"    \"h\":80,\n"
			"    \"message\":\"%s\",\n"
			"    \"font\":\"\",\n"
			"    \"bg\":\"\",\n"
			"    \"icon\":\"\",\n"
			"    \"fontsize\":24,\n"
			"    \"fontcolor\":\"FFFFFFFF\"\n"
			"}\n",
			duration_ms, (screen_width() - TOAST_BG_WIDTH) / 2, msg);
	fclose(f);
}

static void capture_screenshot(void) {
	mkdir_p(SCREENSHOT_DIR);

	time_t now = time(NULL);
	struct tm* t = localtime(&now);
	char output[512];
	snprintf(output, sizeof(output),
			 SCREENSHOT_DIR "/SCR_%04d%02d%02d_%02d%02d%02d.jpg",
			 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
			 t->tm_hour, t->tm_min, t->tm_sec);

	int use_rawvideo = (access(FB_MIRROR_PATH, F_OK) == 0);

	pid_t pid = fork();
	if (pid < 0)
		return;

	if (pid == 0) {
		setsid();
		freopen("/dev/null", "r", stdin);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		if (use_rawvideo) {
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
				  "-f", "rawvideo", "-pixel_format", "rgba",
				  "-video_size", FB_MIRROR_VIDEO_SIZE,
				  "-i", FB_MIRROR_PATH,
				  "-vf", "vflip",
				  "-frames:v", "1", "-c:v", "mjpeg", "-q:v", "2",
				  "-y", output,
				  (char*)NULL);
		} else {
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
				  "-f", "fbdev", "-i", "/dev/fb0",
				  "-frames:v", "1", "-c:v", "mjpeg", "-q:v", "2",
				  "-y", output,
				  (char*)NULL);
		}
		_exit(1);
	}

	// Wait for ffmpeg to finish (single frame capture is fast)
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		osd_toast("Screenshot saved", 1500);
	else
		osd_toast("Screenshot failed", 1500);
}

int main(int argc, char* argv[]) {
	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	// Write PID file
	FILE* f = fopen(PID_FILE, "w");
	if (f) {
		fprintf(f, "%d", getpid());
		fclose(f);
	}

	// Open input devices
	char path[32];
	for (int i = 0; i < INPUT_COUNT; i++) {
		sprintf(path, "/dev/input/event%i", i);
		inputs[i] = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	}

	int l2_pressed = 0;
	int r2_pressed = 0;
	int combo_latched = 0; // require both triggers released before the next shot
	uint32_t last_capture_ms = 0;
	struct input_event ev;
	struct timeval tod;

	while (!quit) {
		gettimeofday(&tod, NULL);
		uint32_t now_ms = tod.tv_sec * 1000 + tod.tv_usec / 1000;

		for (int i = 0; i < INPUT_COUNT; i++) {
			if (inputs[i] < 0)
				continue;
			while (read(inputs[i], &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_ABS) {
					if (ev.code == ABS_Z_CODE)
						l2_pressed = ev.value > 0;
					else if (ev.code == ABS_RZ_CODE)
						r2_pressed = ev.value > 0;
				}
			}
		}

		if (l2_pressed && r2_pressed) {
			if (!combo_latched && (now_ms - last_capture_ms) > COOLDOWN_MS) {
				combo_latched = 1;
				capture_screenshot();
				last_capture_ms = now_ms;
			}
		} else {
			combo_latched = 0;
		}

		usleep(16666); // ~60fps polling
	}

	cleanup();
	return 0;
}
