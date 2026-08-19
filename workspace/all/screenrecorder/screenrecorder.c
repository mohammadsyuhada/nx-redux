#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <linux/fb.h>

#include "../common/drm_scanout.h"

#define PID_FILE "/tmp/screenrecorder.pid"
#define SHM_PATH "/tmp/fb_mirror.raw"
#define FFMPEG_PATH "/usr/bin/ffmpeg"
#define FRAME_INTERVAL_NS 33333333LL // ~30fps

static volatile int quit = 0;

static void on_term(int sig) {
	(void)sig;
	quit = 1;
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

// Ensure output directory exists (create parent dirs of output file)
static void ensure_output_dir(const char* filepath) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", filepath);
	char* last_slash = strrchr(dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		mkdir_p(dir);
	}
}

static int64_t mono_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// --- fbdev source (tg5040) ------------------------------------------------
// On tg5040 the Mali fbdev EGL renders straight into fb0, so the visible
// pane IS the screen. On tg5050 fb0 is never scanned out (stays black) —
// never use this source there.
static int fbdev_usable(void) {
	return strcmp(PLATFORM, "tg5050") != 0;
}

static int fbdev_fd = -1;

// Probe fb0 geometry; fills width/height. Returns 1 if usable.
static int fbdev_open(int* width, int* height) {
	if (!fbdev_usable())
		return 0;
	int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 ||
		ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
		var.bits_per_pixel != 32 || var.xres == 0 || var.yres == 0 ||
		fix.line_length < var.xres * 4) {
		close(fd);
		return 0;
	}
	fbdev_fd = fd;
	*width = (int)var.xres;
	*height = (int)var.yres;
	return 1;
}

// Reads the currently visible pane (yoffset moves as the GL driver pans
// between buffers) into buf as packed top-down rows. Returns 1 on success.
static int fbdev_read(uint8_t* buf, int width, int height) {
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	if (ioctl(fbdev_fd, FBIOGET_VSCREENINFO, &var) != 0 ||
		ioctl(fbdev_fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
		var.xres != (uint32_t)width || var.yres != (uint32_t)height)
		return 0;
	size_t row = (size_t)width * 4;
	for (int y = 0; y < height; y++) {
		off_t off = ((off_t)var.yoffset + y) * fix.line_length +
					(off_t)var.xoffset * 4;
		if (pread(fbdev_fd, buf + (size_t)y * row, row, off) != (ssize_t)row)
			return 0;
	}
	return 1;
}

// --- GPU mirror fallback source (tg5040 / no-DRM platforms) ---------------
// The mirror path (/tmp/fb_mirror.raw) is unlinked and re-created whenever
// the foreground app changes, so the mapping must follow the path's inode:
// keeping the original mmap would freeze the recording on the dead app's
// last frame.
static int mirror_fd = -1;
static uint8_t* mirror_ptr = NULL;
static size_t mirror_size = 0;
static ino_t mirror_ino = 0;

static void mirror_unmap(void) {
	if (mirror_ptr) {
		munmap(mirror_ptr, mirror_size);
		mirror_ptr = NULL;
	}
	if (mirror_fd >= 0) {
		close(mirror_fd);
		mirror_fd = -1;
	}
	mirror_ino = 0;
}

// Maps (or remaps) the current mirror file if present and large enough.
// Returns 1 when a valid mapping exists.
static int mirror_map(size_t frame_size) {
	struct stat st;
	if (stat(SHM_PATH, &st) != 0 || (size_t)st.st_size < frame_size) {
		// Path gone (app transition): drop the mapping so we don't stream a
		// dead inode; the caller keeps sending its last-good frame meanwhile.
		mirror_unmap();
		return 0;
	}
	if (mirror_ptr && st.st_ino == mirror_ino)
		return 1;
	mirror_unmap();
	int fd = open(SHM_PATH, O_RDONLY);
	if (fd < 0)
		return 0;
	struct stat fst;
	if (fstat(fd, &fst) != 0 || (size_t)fst.st_size < frame_size) {
		close(fd);
		return 0;
	}
	uint8_t* ptr = mmap(NULL, frame_size, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		close(fd);
		return 0;
	}
	mirror_fd = fd;
	mirror_ptr = ptr;
	mirror_size = frame_size;
	mirror_ino = fst.st_ino;
	return 1;
}

int main(int argc, char* argv[]) {
	if (argc < 4) {
		fprintf(stderr, "Usage: screenrecorder <output_path> <width> <height>\n");
		return 1;
	}

	const char* output_path = argv[1];
	int width = atoi(argv[2]);
	int height = atoi(argv[3]);

	if (width <= 0 || height <= 0) {
		fprintf(stderr, "Invalid dimensions: %dx%d\n", width, height);
		return 1;
	}

	// Set up signal handlers (respect an inherited SIG_IGN, e.g. nohup)
	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	struct sigaction old_hup;
	sigaction(SIGHUP, NULL, &old_hup);
	if (old_hup.sa_handler != SIG_IGN)
		sigaction(SIGHUP, &sa, NULL);
	// The encoder pipe can break if ffmpeg dies; take that as a write error,
	// not a process kill.
	signal(SIGPIPE, SIG_IGN);

	// Write PID file
	FILE* pf = fopen(PID_FILE, "w");
	if (pf) {
		fprintf(pf, "%d", getpid());
		fclose(pf);
	}

	ensure_output_dir(output_path);

	// Pick a source. DRM scanout (tg5050) and fbdev (tg5040) both record the
	// real screen for ANY app — including third-party paks — and follow the
	// display across app switches. The GPU mirror published by
	// generic_video.c apps stays as a last-resort fallback: wait for it to
	// appear (PLAT_pokeCapture wakes idle apps within ~1.5s of the PID file).
	enum { SRC_DRM,
		   SRC_FBDEV,
		   SRC_MIRROR } src;
	drm_scanout_info drm_info;
	if (drm_scanout_read(&drm_info, NULL, 0)) {
		src = SRC_DRM;
		width = drm_info.width;
		height = drm_info.height;
	} else if (fbdev_open(&width, &height)) {
		src = SRC_FBDEV;
	} else {
		src = SRC_MIRROR;
		int found = 0;
		for (int i = 0; i < 300 && !quit; i++) { // up to ~10s
			if (mirror_map((size_t)width * height * 4)) {
				found = 1;
				break;
			}
			usleep(33333);
		}
		if (!found || quit) {
			fprintf(stderr, "No capture source (no DRM planes, no fbdev, no %s)\n", SHM_PATH);
			mirror_unmap();
			remove(PID_FILE);
			return 1;
		}
	}
	size_t frame_size = (size_t)width * height * 4;

	uint8_t* frame = malloc(frame_size);
	uint8_t* prev = malloc(frame_size);
	if (!frame || !prev) {
		free(frame);
		free(prev);
		mirror_unmap();
		remove(PID_FILE);
		return 1;
	}
	int have_prev = 0;

	// Create pipe to ffmpeg
	int pipe_fds[2];
	if (pipe(pipe_fds) < 0) {
		perror("pipe");
		free(frame);
		free(prev);
		mirror_unmap();
		remove(PID_FILE);
		return 1;
	}

	char video_size[32];
	snprintf(video_size, sizeof(video_size), "%dx%d", width, height);

	// Fork ffmpeg. Frames are stamped with their real arrival time and the
	// output is variable-frame-rate: the encoder on these cores can't hold a
	// fixed 30fps (and paks often cap the CPU clocks), so frame-count
	// timestamps would make the video play too fast. With VFR the timeline
	// is correct at whatever rate the device achieves, and unchanged frames
	// can simply be skipped. DRM scanout frames are top-down bgr0/rgb0;
	// mirror frames are bottom-up RGBA from glReadPixels (vflip at encode).
	pid_t ffmpeg_pid = fork();
	if (ffmpeg_pid < 0) {
		perror("fork");
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		free(frame);
		free(prev);
		mirror_unmap();
		remove(PID_FILE);
		return 1;
	}

	if (ffmpeg_pid == 0) {
		// ffmpeg child
		close(pipe_fds[1]); // close write end
		dup2(pipe_fds[0], STDIN_FILENO);
		close(pipe_fds[0]);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		// DRM scanout: top-down bgr0/rgb0. fbdev: top-down bgr0 (alpha byte
		// ignored). Mirror: bottom-up RGBA from glReadPixels → vflip.
		const char* pixfmt = src == SRC_DRM		? drm_info.pixfmt
							 : src == SRC_FBDEV ? "bgr0"
												: "rgba";
		if (src == SRC_MIRROR) {
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
				  "-use_wallclock_as_timestamps", "1",
				  "-f", "rawvideo", "-pixel_format", pixfmt,
				  "-video_size", video_size,
				  "-i", "pipe:0",
				  "-vf", "vflip",
				  "-c:v", "mjpeg", "-q:v", "10", "-pix_fmt", "yuvj420p",
				  "-fps_mode", "vfr",
				  // fragmented mp4: file stays playable even if we're SIGKILLed
				  "-movflags", "+frag_keyframe+empty_moov",
				  "-y", output_path,
				  (char*)NULL);
		} else {
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
				  "-use_wallclock_as_timestamps", "1",
				  "-f", "rawvideo", "-pixel_format", pixfmt,
				  "-video_size", video_size,
				  "-i", "pipe:0",
				  "-c:v", "mjpeg", "-q:v", "10", "-pix_fmt", "yuvj420p",
				  "-fps_mode", "vfr",
				  "-movflags", "+frag_keyframe+empty_moov",
				  "-y", output_path,
				  (char*)NULL);
		}
		_exit(1);
	}

	// Parent: close read end of pipe
	close(pipe_fds[0]);

	// Brief check that ffmpeg started
	usleep(100000);
	if (waitpid(ffmpeg_pid, NULL, WNOHANG) != 0) {
		fprintf(stderr, "ffmpeg failed to start\n");
		close(pipe_fds[1]);
		free(frame);
		free(prev);
		mirror_unmap();
		remove(PID_FILE);
		return 1;
	}

	// Main loop: capture up to ~30fps, but only feed the encoder frames that
	// changed (plus a ~1s heartbeat so a static screen still advances the
	// VFR timeline). If capture or encode runs slow the loop just delivers
	// fewer frames — timestamps keep the video at real speed.
	int64_t next_ns = mono_ns();
	int64_t last_write_ns = 0;
	while (!quit) {
		int got = 0;
		if (src == SRC_DRM) {
			drm_scanout_info fi;
			got = drm_scanout_read(&fi, frame, frame_size) &&
				  fi.width == (uint32_t)width && fi.height == (uint32_t)height;
			// mode/geometry change mid-recording: skip until it's back
		} else if (src == SRC_FBDEV) {
			got = fbdev_read(frame, width, height);
		} else {
			if (mirror_map(frame_size)) {
				memcpy(frame, mirror_ptr, frame_size);
				got = 1;
			}
		}

		int64_t now = mono_ns();
		int changed = got && (!have_prev || memcmp(frame, prev, frame_size) != 0);
		if (got && (changed || now - last_write_ns > 1000000000LL)) {
			const uint8_t* ptr = frame;
			size_t remaining = frame_size;
			while (remaining > 0) {
				ssize_t n = write(pipe_fds[1], ptr, remaining);
				if (n < 0) {
					if (errno == EINTR)
						continue;
					quit = 1;
					break;
				}
				ptr += n;
				remaining -= n;
			}
			last_write_ns = now;
			uint8_t* tmp = prev;
			prev = frame;
			frame = tmp;
			have_prev = 1;
		}

		next_ns += FRAME_INTERVAL_NS;
		now = mono_ns();
		if (next_ns > now)
			usleep((useconds_t)((next_ns - now) / 1000));
		else
			next_ns = now; // running behind: don't accumulate debt
	}

	// Cleanup: close pipe (sends EOF to ffmpeg), wait for it to finish
	close(pipe_fds[1]);
	waitpid(ffmpeg_pid, NULL, 0);

	free(frame);
	free(prev);
	mirror_unmap();
	remove(PID_FILE);
	return 0;
}
