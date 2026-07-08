#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define PID_FILE "/tmp/screenrecorder.pid"
#define SHM_PATH "/tmp/fb_mirror.raw"
#define FFMPEG_PATH "/usr/bin/ffmpeg"
#define POLL_INTERVAL_US 33333 // ~30fps

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

	size_t frame_size = (size_t)width * height * 4; // RGBA

	// Set up signal handlers
	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	// Write PID file
	FILE* pf = fopen(PID_FILE, "w");
	if (pf) {
		fprintf(pf, "%d", getpid());
		fclose(pf);
	}

	ensure_output_dir(output_path);

	// Wait for shm file to appear (capture system creates it). Generous
	// timeout: the foreground app only services capture_check() on rendered
	// frames, and dirty-flag apps (nextui) don't render until the user closes
	// the OSD and starts interacting again.
	int shm_fd = -1;
	for (int i = 0; i < 1800 && !quit; i++) { // up to ~60s
		shm_fd = open(SHM_PATH, O_RDONLY);
		if (shm_fd >= 0)
			break;
		usleep(33333);
	}
	if (shm_fd < 0 || quit) {
		fprintf(stderr, "Timed out waiting for %s\n", SHM_PATH);
		remove(PID_FILE);
		return 1;
	}

	// Verify file size matches expected frame size
	off_t file_size = lseek(shm_fd, 0, SEEK_END);
	if ((size_t)file_size < frame_size) {
		fprintf(stderr, "SHM file too small: %ld < %zu\n", (long)file_size, frame_size);
		close(shm_fd);
		remove(PID_FILE);
		return 1;
	}

	// mmap the shm file read-only
	uint8_t* shm_ptr = mmap(NULL, frame_size, PROT_READ, MAP_SHARED, shm_fd, 0);
	if (shm_ptr == MAP_FAILED) {
		perror("mmap");
		close(shm_fd);
		remove(PID_FILE);
		return 1;
	}

	// Create pipe to ffmpeg
	int pipe_fds[2];
	if (pipe(pipe_fds) < 0) {
		perror("pipe");
		munmap(shm_ptr, frame_size);
		close(shm_fd);
		remove(PID_FILE);
		return 1;
	}

	// Build video_size string
	char video_size[32];
	snprintf(video_size, sizeof(video_size), "%dx%d", width, height);

	// Fork ffmpeg
	pid_t ffmpeg_pid = fork();
	if (ffmpeg_pid < 0) {
		perror("fork");
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		munmap(shm_ptr, frame_size);
		close(shm_fd);
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
		execl(FFMPEG_PATH, "ffmpeg", "-nostdin",
			  "-f", "rawvideo", "-pixel_format", "rgba",
			  "-video_size", video_size, "-framerate", "30",
			  "-i", "pipe:0",
			  "-vf", "vflip",
			  "-c:v", "mjpeg", "-q:v", "10",
			  "-y", output_path,
			  (char*)NULL);
		_exit(1);
	}

	// Parent: close read end of pipe
	close(pipe_fds[0]);

	// Brief check that ffmpeg started
	usleep(100000);
	if (waitpid(ffmpeg_pid, NULL, WNOHANG) != 0) {
		fprintf(stderr, "ffmpeg failed to start\n");
		close(pipe_fds[1]);
		munmap(shm_ptr, frame_size);
		close(shm_fd);
		remove(PID_FILE);
		return 1;
	}

	// Main loop: read from shm, pipe to ffmpeg at ~30fps
	while (!quit) {
		ssize_t total = 0;
		const uint8_t* ptr = shm_ptr;
		size_t remaining = frame_size;
		while (remaining > 0) {
			ssize_t n = write(pipe_fds[1], ptr, remaining);
			if (n < 0) {
				quit = 1;
				break;
			}
			ptr += n;
			remaining -= n;
			total += n;
		}
		usleep(POLL_INTERVAL_US);
	}

	// Cleanup: close pipe (sends EOF to ffmpeg), wait for it to finish
	close(pipe_fds[1]);
	waitpid(ffmpeg_pid, NULL, 0);

	munmap(shm_ptr, frame_size);
	close(shm_fd);
	remove(PID_FILE);
	return 0;
}
