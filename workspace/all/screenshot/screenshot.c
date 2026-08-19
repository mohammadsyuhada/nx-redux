#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <linux/fb.h>

#if defined(__has_include)
#if __has_include(<drm/drm.h>) && __has_include(<drm/drm_mode.h>)
#define HAVE_DRM 1
#include <drm/drm.h>
#include <drm/drm_mode.h>
// Toolchain UAPI headers predate kernel 5.7; the tg5050 kernel (5.15) has it.
#ifndef DRM_IOCTL_MODE_GETFB2
#define DRM_IOCTL_MODE_GETFB2 DRM_IOWR(0xCE, struct drm_mode_fb_cmd2)
#endif
#endif
#endif

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
#define FB_MIRROR_INFO_PATH "/tmp/fb_mirror.info"
#define FB_MIRROR_VIDEO_SIZE_FALLBACK "1280x720"

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

// A live mirror is one whose writer still holds the advisory flock taken at
// creation (generic_video.c capture_check). A leftover file from a dead or
// exec'd-away process carries no lock: encoding it would produce a stale
// frame from the previous app, so treat it as garbage.
static int mirror_live(void) {
	int fd = open(FB_MIRROR_PATH, O_RDONLY);
	if (fd < 0)
		return 0;
	int live = 0;
	if (flock(fd, LOCK_EX | LOCK_NB) == 0)
		flock(fd, LOCK_UN);
	else if (errno == EWOULDBLOCK)
		live = 1;
	close(fd);
	return live;
}

// Geometry published by the mirror's writer alongside the raw frames
static void mirror_video_size(char* out, size_t n) {
	snprintf(out, n, "%s", FB_MIRROR_VIDEO_SIZE_FALLBACK);
	FILE* f = fopen(FB_MIRROR_INFO_PATH, "r");
	if (f) {
		int w = 0, h = 0;
		if (fscanf(f, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
			snprintf(out, n, "%dx%d", w, h);
		fclose(f);
	}
}

// On tg5050 the display engine never scans out of fb0 — apps render through
// GL/DRM and fb0 stays black (its only writer is the zeroing in
// PLAT_quitVideo) — so an fbdev grab is guaranteed to produce an empty JPEG.
static int fbdev_usable(void) {
	return strcmp(PLATFORM, "tg5050") != 0;
}

// Returns 1 if the visible fb0 pane holds any non-black RGB pixel. On tg5040
// fb0 is the real scanout but goes black briefly around app transitions
// (apps zero it on quit). Geometry or read failures assume content so a
// sampler gap can never block a capture that might have worked.
static int fb0_has_content(void) {
	int fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0)
		return 0;
	int result = 1;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 &&
		ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
		var.bits_per_pixel == 32 && var.xres > 0 && var.yres > 0 &&
		fix.line_length >= var.xres * 4) {
		// Only the framebuffer's own channel bits decide blackness: an
		// opaque-black fill (alpha 0xFF) or garbage in unused bits must not
		// read as content.
		uint32_t rgb_mask = (((var.red.length ? (1u << var.red.length) - 1 : 0)) << var.red.offset) |
							(((var.green.length ? (1u << var.green.length) - 1 : 0)) << var.green.offset) |
							(((var.blue.length ? (1u << var.blue.length) - 1 : 0)) << var.blue.offset);
		if (!rgb_mask)
			rgb_mask = 0x00FFFFFF;
		static uint32_t line[4096];
		size_t line_bytes = (size_t)var.xres * 4;
		if (line_bytes > sizeof(line))
			line_bytes = sizeof(line);
		result = 0;
		for (unsigned y = 0; y < var.yres && !result; y += 8) {
			off_t off = ((off_t)var.yoffset + y) * fix.line_length +
						(off_t)var.xoffset * 4;
			ssize_t n = pread(fd, line, line_bytes, off);
			if (n < (ssize_t)line_bytes) {
				result = 1; // can't sample: assume content
				break;
			}
			for (size_t x = 0; x < line_bytes / 4; x += 4) {
				if (line[x] & rgb_mask) {
					result = 1;
					break;
				}
			}
		}
	}
	close(fd);
	return result;
}

#define DRM_RAW_PATH "/tmp/screenshot_drm.raw"

// Read the framebuffer currently scanned out by the display engine straight
// from DRM/KMS: GETFB2 + PRIME export + mmap. Needs only root
// (CAP_SYS_ADMIN), not DRM master, so it captures ANY app — including
// third-party paks that don't publish the GPU mirror. This is the primary
// tg5050 source; on tg5040 the DRM node is just the Mali render device with
// no KMS planes, so it fails fast and the fbdev path takes over.
// On success writes packed rows to DRM_RAW_PATH and fills video_size
// ("WxH") + pixfmt (ffmpeg rawvideo pixel_format name).
#ifdef HAVE_DRM
static int drm_ioctl(int fd, unsigned long req, void* arg) {
	int ret;
	do {
		ret = ioctl(fd, req, arg);
	} while (ret < 0 && (errno == EINTR || errno == EAGAIN));
	return ret;
}

static int drm_capture_raw(char* video_size, size_t vn, char* pixfmt, size_t pn) {
	int ok = 0;
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return 0;

	struct drm_set_client_cap cap = {DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1};
	drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);

	uint32_t plane_ids[64];
	struct drm_mode_get_plane_res pres = {0};
	pres.plane_id_ptr = (uintptr_t)plane_ids;
	pres.count_planes = 64;
	if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) ||
		pres.count_planes == 0 || pres.count_planes > 64) {
		close(fd);
		return 0;
	}

	for (uint32_t i = 0; i < pres.count_planes && !ok; i++) {
		struct drm_mode_get_plane pl = {0};
		pl.plane_id = plane_ids[i];
		if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &pl) || !pl.fb_id)
			continue;

		struct drm_mode_fb_cmd2 fb = {0};
		fb.fb_id = pl.fb_id;
		if (drm_ioctl(fd, DRM_IOCTL_MODE_GETFB2, &fb))
			continue;
		if (fb.modifier[0] != 0) // tiled/compressed (e.g. AFBC): can't read raw
			continue;

		// Single-plane 32bpp RGB only; video overlays (NV12 etc.) are skipped
		// and the loop falls through to the UI plane.
		const char* fmt = NULL;
		switch (fb.pixel_format) {
		case 0x34325258: // XR24 XRGB8888: B G R X in memory
		case 0x34325241:
			fmt = "bgr0";
			break;		 // AR24 ARGB8888
		case 0x34325842: // XB24 XBGR8888: R G B X in memory
		case 0x34324241:
			fmt = "rgb0";
			break; // AB24 ABGR8888
		default:
			continue;
		}

		struct drm_prime_handle prime = {0};
		prime.handle = fb.handles[0];
		prime.flags = O_CLOEXEC;
		if (drm_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime))
			continue;

		size_t len = (size_t)fb.offsets[0] + (size_t)fb.pitches[0] * fb.height;
		uint8_t* map = mmap(NULL, len, PROT_READ, MAP_SHARED, prime.fd, 0);
		if (map != MAP_FAILED) {
			FILE* out = fopen(DRM_RAW_PATH, "w");
			if (out) {
				// Pack rows in case pitch > width*4
				size_t row = (size_t)fb.width * 4;
				const uint8_t* src = map + fb.offsets[0];
				size_t written = 0;
				for (uint32_t y = 0; y < fb.height; y++)
					written += fwrite(src + (size_t)y * fb.pitches[0], 1, row, out);
				fclose(out);
				if (written == row * fb.height) {
					snprintf(video_size, vn, "%ux%u", fb.width, fb.height);
					snprintf(pixfmt, pn, "%s", fmt);
					ok = 1;
				}
			}
			munmap(map, len);
		}
		close(prime.fd);
	}
	close(fd); // releases the GEM handle references from GETFB2
	return ok;
}
#else
static int drm_capture_raw(char* video_size, size_t vn, char* pixfmt, size_t pn) {
	(void)video_size;
	(void)vn;
	(void)pixfmt;
	(void)pn;
	return 0;
}
#endif

static void capture_screenshot(void) {
	mkdir_p(SCREENSHOT_DIR);

	// Pick a source, in order of preference:
	//   1. live GPU mirror (app-published, vsync'd frame)
	//   2. DRM plane readback (composited scanout — works for ANY app on
	//      tg5050, including third-party paks; fails fast where the DRM node
	//      has no KMS planes)
	//   3. non-black fb0 via fbdev (tg5040, where GL renders through fb0)
	// Retry briefly: enabling capture wakes idle apps via PLAT_pokeCapture
	// (fires once the app has been idle ~1s, ≤500ms poll cadence) and app
	// transitions repaint fb0 within a frame or two.
	enum { SRC_NONE,
		   SRC_MIRROR,
		   SRC_DRM,
		   SRC_FBDEV } src = SRC_NONE;
	char video_size[32];
	char pixfmt[8];
	for (int i = 0; i < 20 && src == SRC_NONE; i++) {
		if (mirror_live()) {
			src = SRC_MIRROR;
			break;
		}
		if (drm_capture_raw(video_size, sizeof(video_size), pixfmt, sizeof(pixfmt))) {
			src = SRC_DRM;
			break;
		}
		if (fbdev_usable() && fb0_has_content()) {
			src = SRC_FBDEV;
			break;
		}
		usleep(100000);
	}
	if (src == SRC_NONE) {
		// Nothing can supply pixels here: fail honestly instead of writing
		// an all-black JPEG and claiming success.
		osd_toast("Capture not available here", 2000);
		return;
	}
	if (src == SRC_MIRROR)
		mirror_video_size(video_size, sizeof(video_size));

	time_t now = time(NULL);
	struct tm* t = localtime(&now);
	char output[512];
	snprintf(output, sizeof(output),
			 SCREENSHOT_DIR "/SCR_%04d%02d%02d_%02d%02d%02d.jpg",
			 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
			 t->tm_hour, t->tm_min, t->tm_sec);

	pid_t pid = fork();
	if (pid < 0)
		return;

	if (pid == 0) {
		setsid();
		freopen("/dev/null", "r", stdin);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		if (src == SRC_MIRROR) {
			// glReadPixels frames are bottom-to-top RGBA: vflip at encode
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
				  "-f", "rawvideo", "-pixel_format", "rgba",
				  "-video_size", video_size,
				  "-i", FB_MIRROR_PATH,
				  "-vf", "vflip",
				  "-frames:v", "1", "-c:v", "mjpeg", "-q:v", "2",
				  "-y", output,
				  (char*)NULL);
		} else if (src == SRC_DRM) {
			// scanout buffer is top-down: no flip
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
				  "-f", "rawvideo", "-pixel_format", pixfmt,
				  "-video_size", video_size,
				  "-i", DRM_RAW_PATH,
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
	if (src == SRC_DRM)
		remove(DRM_RAW_PATH);
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
	// The OSD toggle launches us with a plain `&`; a controlling-terminal HUP
	// (e.g. an adb shell exiting) must run cleanup, not kill us mid-flight
	// leaving a stale PID file that keeps every app mirroring forever.
	// Respect an inherited SIG_IGN (nohup) instead of overriding it.
	struct sigaction old_hup;
	sigaction(SIGHUP, NULL, &old_hup);
	if (old_hup.sa_handler != SIG_IGN)
		sigaction(SIGHUP, &sa, NULL);

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
