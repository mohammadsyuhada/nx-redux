#include "../music_service_client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(PLATFORM_TG5040) || defined(PLATFORM_TG5050)
#include <sys/inotify.h>
#endif
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;
static void check(int condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}
static void wait_ms(int ms) {
	usleep((useconds_t)ms * 1000);
}
static int64_t clock_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int raw_connect(const char* path) {
	int fd = MusicService_connect(path, 1000);
	if (fd >= 0) {
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags >= 0)
			(void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	}
	return fd;
}
static int raw_request(int fd, uint16_t command, const void* payload, size_t length, MusicResponseWire* out) {
	return MusicService_request(fd, command, payload, length, out, 5000);
}
static int write_wav(const char* path, int seconds_tenths) {
	FILE* f = fopen(path, "wb");
	if (!f)
		return -1;
	uint32_t frames = 4800u * (uint32_t)seconds_tenths, data_size = frames * 4, riff_size = 36 + data_size;
	fwrite("RIFF", 1, 4, f);
	fwrite(&riff_size, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	uint32_t fmt_size = 16;
	uint16_t pcm = 1, channels = 2;
	uint32_t rate = 48000, bytes = rate * 4;
	uint16_t block = 4, bits = 16;
	fwrite(&fmt_size, 4, 1, f);
	fwrite(&pcm, 2, 1, f);
	fwrite(&channels, 2, 1, f);
	fwrite(&rate, 4, 1, f);
	fwrite(&bytes, 4, 1, f);
	fwrite(&block, 2, 1, f);
	fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&data_size, 4, 1, f);
	for (uint32_t i = 0; i < frames; i++) {
		int16_t sample = (i % 100 < 50) ? 1200 : -1200;
		fwrite(&sample, 2, 1, f);
		fwrite(&sample, 2, 1, f);
	}
	fclose(f);
	return 0;
}
static int run_cli(const char* cli_path, const char* command, const char* argument) {
	pid_t child = fork();
	if (child == 0) {
		if (argument)
			execl(cli_path, cli_path, command, argument, NULL);
		else
			execl(cli_path, cli_path, command, NULL);
		_exit(127);
	}
	int status = 0;
	if (child < 0 || waitpid(child, &status, 0) < 0)
		return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static pid_t start_owner(const char* daemon_path, const char* socket_path, int* fd_out) {
	pid_t owner = fork();
	if (owner == 0) {
		execl(daemon_path, daemon_path, NULL);
		_exit(127);
	}
	int fd = -1;
	for (int i = 0; i < 100 && fd < 0; i++) {
		wait_ms(20);
		fd = raw_connect(socket_path);
	}
	*fd_out = fd;
	return owner;
}

static void stop_owner(pid_t owner, int fd) {
	MusicResponseWire response;
	if (fd >= 0) {
		(void)raw_request(fd, MUSIC_CMD_SHUTDOWN, NULL, 0, &response);
		close(fd);
	}
	waitpid(owner, NULL, 0);
}

static int send_raw_header(int fd, const MusicFrameHeader* header, const void* body, size_t body_size, int split) {
	if (split) {
		if (send(fd, header, split, 0) != split)
			return -1;
		if (send(fd, (const char*)header + split, sizeof(*header) - split, 0) != (ssize_t)(sizeof(*header) - split))
			return -1;
	} else if (send(fd, header, sizeof(*header), 0) != sizeof(*header))
		return -1;
	return body_size ? send(fd, body, body_size, 0) == (ssize_t)body_size ? 0 : -1 : 0;
}

#if defined(PLATFORM_TG5040) || defined(PLATFORM_TG5050)
static void test_paused_poll_does_not_rewrite_resume(const char* resume_root) {
	int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	check(inotify_fd >= 0, "resume write regression inotify setup");
	if (inotify_fd < 0)
		return;
	int watch = inotify_add_watch(inotify_fd, resume_root, IN_MOVED_TO);
	check(watch >= 0, "resume write regression inotify watch");
	if (watch < 0) {
		close(inotify_fd);
		return;
	}

	char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	while (read(inotify_fd, buffer, sizeof(buffer)) > 0)
		;

	int writes = 0;
	int64_t deadline = clock_ms() + 2200;
	while (clock_ms() < deadline) {
		int timeout = (int)(deadline - clock_ms());
		if (timeout < 1)
			timeout = 1;
		struct pollfd pollfd = {.fd = inotify_fd, .events = POLLIN};
		int result = poll(&pollfd, 1, timeout);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			check(0, "resume write regression inotify poll");
			break;
		}
		if (result == 0 || !(pollfd.revents & POLLIN))
			continue;
		ssize_t length;
		while ((length = read(inotify_fd, buffer, sizeof(buffer))) > 0) {
			for (ssize_t offset = 0; offset + (ssize_t)sizeof(struct inotify_event) <= length;) {
				struct inotify_event* event = (struct inotify_event*)(buffer + offset);
				ssize_t event_size = (ssize_t)sizeof(*event) + event->len;
				if (event_size > length - offset)
					break;
				if ((event->mask & IN_MOVED_TO) && event->len && strcmp(event->name, "resume.cfg") == 0)
					writes++;
				offset += event_size;
			}
		}
	}
	if (writes > 1)
		fprintf(stderr, "FAIL: paused resume rewrites during poll window: %d\n", writes);
	check(writes <= 1, "paused polling does not rewrite resume state at 100Hz");
	inotify_rm_watch(inotify_fd, watch);
	close(inotify_fd);
}
#endif

int main(int argc, char** argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: test_music_service musicplayerd.elf musicplayerctl.elf\n");
		return 2;
	}
	char root[128];
	snprintf(root, sizeof(root), "/tmp/nx_music_service_test_%ld", (long)getpid());
	mkdir(root, 448);
	char socket_path[160], first[200], second[200], playlist_path[200], resume_root[200];
	snprintf(socket_path, sizeof(socket_path), "%s/control.sock", root);
	snprintf(resume_root, sizeof(resume_root), "%s/resume", root);
	mkdir(resume_root, 448);
	snprintf(first, sizeof(first), "%s/01.wav", root);
	snprintf(second, sizeof(second), "%s/02.wav", root);
	snprintf(playlist_path, sizeof(playlist_path), "%s/list.m3u", root);
	check(write_wav(first, 4) == 0 && write_wav(second, 40) == 0, "create fixtures");
	FILE* playlist_file = fopen(playlist_path, "w");
	if (playlist_file) {
		fprintf(playlist_file, "%s\n%s\n", first, second);
		fclose(playlist_file);
	}
	check(playlist_file != NULL, "create playlist fixture");
	setenv(MUSIC_SERVICE_SOCKET_ENV, socket_path, 1);
	setenv("NX_MUSIC_RESUME_DIR", resume_root, 1);
	pid_t daemon = fork();
	if (daemon == 0) {
		execl(argv[1], argv[1], NULL);
		_exit(127);
	}
	int fd = -1;
	for (int i = 0; i < 100 && fd < 0; i++) {
		wait_ms(20);
		fd = raw_connect(socket_path);
	}
	check(fd >= 0, "daemon starts and socket is connectable");
	if (fd < 0)
		goto cleanup;

	/* A request split at the header boundary is accepted. */
	MusicFrameHeader header = {.magic = MUSIC_SERVICE_MAGIC, .version = MUSIC_SERVICE_PROTOCOL_VERSION, .command = MUSIC_CMD_SNAPSHOT, .request_id = 7, .payload_length = 0};
	check(send_raw_header(fd, &header, NULL, 0, 3) == 0, "partial header write");
	MusicFrameHeader reply_header;
	MusicResponseWire response;
	check(recv(fd, &reply_header, sizeof(reply_header), MSG_WAITALL) == sizeof(reply_header), "partial request response header");
	check(reply_header.payload_length == sizeof(response) && recv(fd, &response, sizeof(response), MSG_WAITALL) == sizeof(response), "partial request response payload");
	close(fd);

	/* Two clients can remain attached and receive independent copied snapshots. */
	int a = raw_connect(socket_path), b = raw_connect(socket_path);
	check(a >= 0 && b >= 0, "two clients attach");
	if (a >= 0 && b >= 0) {
		MusicResponseWire ra, rb;
		check(raw_request(a, MUSIC_CMD_SNAPSHOT, NULL, 0, &ra) == 0, "client A snapshot");
		check(raw_request(b, MUSIC_CMD_SNAPSHOT, NULL, 0, &rb) == 0, "client B snapshot");
	}
	if (a >= 0)
		close(a);
	if (b >= 0)
		close(b);

	/* Oversize and partial timeout clients are dropped without stopping the owner. */
	int bad = raw_connect(socket_path);
	check(bad >= 0, "oversize client attach");
	if (bad >= 0) {
		MusicFrameHeader huge = header;
		huge.payload_length = MUSIC_SERVICE_MAX_FRAME;
		(void)send(bad, &huge, sizeof(huge), 0);
		wait_ms(30);
		close(bad);
	}
	int timeout_client = raw_connect(socket_path);
	check(timeout_client >= 0, "timeout client attach");
	if (timeout_client >= 0) {
		(void)send(timeout_client, &header, 2, 0);
		wait_ms(2200);
		close(timeout_client);
	}
	fd = raw_connect(socket_path);
	check(fd >= 0, "owner survives malformed clients");
	if (fd >= 0) {
		check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0, "snapshot after malformed clients");
		check(raw_request(fd, MUSIC_CMD_TOGGLE, NULL, 0, &response) == MUSIC_STATUS_NOT_FOUND &&
				  response.status == MUSIC_STATUS_NOT_FOUND && response.error[0],
			  "toggle without loaded source returns failure");
		MusicRadioLoadRequest radio_request;
		memset(&radio_request, 0, sizeof(radio_request));
		check(raw_request(fd, MUSIC_CMD_RADIO_LOAD, &radio_request, sizeof(radio_request) - 1, &response) == MUSIC_STATUS_BAD_REQUEST &&
				  response.status == MUSIC_STATUS_BAD_REQUEST,
			  "radio request validates fixed payload");
		MusicPodcastLoadRequest podcast_request;
		memset(&podcast_request, 0, sizeof(podcast_request));
		check(raw_request(fd, MUSIC_CMD_PODCAST_LOAD, &podcast_request, sizeof(podcast_request) - 1, &response) == MUSIC_STATUS_BAD_REQUEST &&
				  response.status == MUSIC_STATUS_BAD_REQUEST,
			  "podcast request validates fixed payload");
		close(fd);
	}

	/* A second owner is rejected while the singleton is alive. */
	pid_t second_owner = fork();
	if (second_owner == 0) {
		execl(argv[1], argv[1], NULL);
		_exit(127);
	}
	int second_status = 0;
	waitpid(second_owner, &second_status, 0);
	check(WIFEXITED(second_status) && WEXITSTATUS(second_status) != 0, "singleton owner rejection");

	/* Resume persistence is exercised through real owner restarts. The private
	 * directory keeps this test from touching a device user's saved playback. */
	char missing[200];
	snprintf(missing, sizeof(missing), "%s/missing.wav", root);
	MusicLoadRequest folder_load;
	memset(&folder_load, 0, sizeof(folder_load));
	strncpy(folder_load.path, root, sizeof(folder_load.path) - 1);
	fd = raw_connect(socket_path);
	check(fd >= 0, "resume stopped attach");
	if (fd >= 0) {
		check(raw_request(fd, MUSIC_CMD_LOAD, &folder_load, sizeof(folder_load), &response) == 0,
			  "resume stopped load");
		check(response.snapshot.state == MUSIC_STATE_STOPPED &&
				  response.snapshot.queue_kind == MUSIC_QUEUE_FOLDER &&
				  strcmp(response.snapshot.queue_path, root) == 0,
			  "resume stopped state and queue identity");
		stop_owner(daemon, fd);
		daemon = start_owner(argv[1], socket_path, &fd);
		check(fd >= 0, "stopped owner restart");
		if (fd >= 0) {
			check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
					  response.snapshot.state == MUSIC_STATE_STOPPED &&
					  !response.snapshot.audio_open &&
					  strcmp(response.snapshot.current_file, first) == 0 &&
					  response.snapshot.queue_kind == MUSIC_QUEUE_FOLDER &&
					  strcmp(response.snapshot.queue_path, root) == 0,
				  "stopped intent restores without starting or opening audio");
		}
	}

	if (fd >= 0) {
		check(raw_request(fd, MUSIC_CMD_SELECT, &(MusicIntRequest){.value = 1}, sizeof(MusicIntRequest), &response) == 0,
			  "select long resume fixture");
		check(raw_request(fd, MUSIC_CMD_PLAY, NULL, 0, &response) == 0, "resume playing start");
		check(raw_request(fd, MUSIC_CMD_SEEK, &(MusicIntRequest){.value = 1000}, sizeof(MusicIntRequest), &response) == 0,
			  "resume playing seek");
		wait_ms(120);
		check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING,
			  "resume playing state before restart");
		int playing_position = response.snapshot.position_ms;
		stop_owner(daemon, fd);
		daemon = start_owner(argv[1], socket_path, &fd);
		check(fd >= 0, "playing owner restart");
		if (fd >= 0) {
			check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
					  response.snapshot.state == MUSIC_STATE_PLAYING &&
					  strcmp(response.snapshot.current_file, second) == 0 &&
					  response.snapshot.queue_index == 1 &&
					  response.snapshot.queue_kind == MUSIC_QUEUE_FOLDER &&
					  response.snapshot.position_ms >= playing_position - 250,
				  "playing intent and position restore");
			check(raw_request(fd, MUSIC_CMD_PAUSE, NULL, 0, &response) == 0 &&
					  response.snapshot.state == MUSIC_STATE_PAUSED,
				  "resume paused state before restart");
			int paused_position = response.snapshot.position_ms;
			stop_owner(daemon, fd);
			daemon = start_owner(argv[1], socket_path, &fd);
			check(fd >= 0, "paused owner restart");
			if (fd >= 0) {
				int paused_snapshot_status = raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response);
				check(paused_snapshot_status == 0 &&
						  response.snapshot.state == MUSIC_STATE_PAUSED &&
						  !response.snapshot.audio_open &&
						  strcmp(response.snapshot.current_file, second) == 0 &&
						  response.snapshot.queue_index == 1 &&
						  response.snapshot.position_ms >= paused_position - 250 &&
						  response.snapshot.position_ms <= paused_position + 250,
					  "paused intent and position restore");
#if defined(PLATFORM_TG5040) || defined(PLATFORM_TG5050)
				if (paused_snapshot_status == 0 && response.snapshot.state == MUSIC_STATE_PAUSED)
					test_paused_poll_does_not_rewrite_resume(resume_root);
#endif
			}
		}
	}

	if (fd >= 0) {
		check(raw_request(fd, MUSIC_CMD_STOP, NULL, 0, &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_NONE,
			  "stop clears persisted resume intent");
		stop_owner(daemon, fd);
		daemon = start_owner(argv[1], socket_path, &fd);
		check(fd >= 0, "no-intent owner restart");
		if (fd >= 0)
			check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
					  response.snapshot.source == MUSIC_SOURCE_NONE && !response.error[0],
				  "no intent remains clear after restart");
	}

	/* Paused radio resumes as identity-only state; restore must not reconnect
	 * just to stop it again. */
	const char* radio_resume_url = "http://127.0.0.1:1/music";
	if (fd >= 0) {
		MusicRadioLoadRequest radio_resume;
		memset(&radio_resume, 0, sizeof(radio_resume));
		strncpy(radio_resume.url, radio_resume_url, sizeof(radio_resume.url) - 1);
		check(raw_request(fd, MUSIC_CMD_RADIO_LOAD, &radio_resume, sizeof(radio_resume), &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_RADIO,
			  "radio resume fixture loads");
		check(raw_request(fd, MUSIC_CMD_PAUSE, NULL, 0, &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_RADIO &&
				  response.snapshot.state == MUSIC_STATE_PAUSED,
			  "radio pause persists transport intent");
		stop_owner(daemon, fd);
		daemon = start_owner(argv[1], socket_path, &fd);
		check(fd >= 0, "paused radio owner restart");
		if (fd >= 0)
			check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
					  response.snapshot.source == MUSIC_SOURCE_RADIO &&
					  response.snapshot.state == MUSIC_STATE_PAUSED &&
					  strcmp(response.snapshot.current_file, radio_resume_url) == 0,
				  "paused radio identity restores without connecting");
	}
	if (fd >= 0) {
		check(raw_request(fd, MUSIC_CMD_STOP, NULL, 0, &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_NONE,
			  "radio restore stop clears intent");
		stop_owner(daemon, fd);
		char stopped_resume_file[220];
		snprintf(stopped_resume_file, sizeof(stopped_resume_file), "%s/resume.cfg", resume_root);
		FILE* stopped_radio = fopen(stopped_resume_file, "w");
		if (stopped_radio) {
			fprintf(stopped_radio, "type=1\nsource=2\ntransport=0\nradio_url=%s\n", radio_resume_url);
			fclose(stopped_radio);
		}
		check(stopped_radio != NULL, "create stopped-radio resume fixture");
		daemon = start_owner(argv[1], socket_path, &fd);
		check(fd >= 0, "stopped radio owner restart");
		if (fd >= 0) {
			check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
					  response.snapshot.source == MUSIC_SOURCE_RADIO &&
					  response.snapshot.state == MUSIC_STATE_STOPPED &&
					  strcmp(response.snapshot.current_file, radio_resume_url) == 0,
				  "stopped radio identity restores without connecting");
			check(raw_request(fd, MUSIC_CMD_STOP, NULL, 0, &response) == 0 &&
					  response.snapshot.source == MUSIC_SOURCE_NONE,
				  "stopped radio clear removes intent");
		}
	}

	/* A missing saved source is cleared and reported through the normal wire
	 * response instead of becoming a silent empty snapshot. */
	if (fd >= 0)
		stop_owner(daemon, fd);
	char resume_file[220];
	snprintf(resume_file, sizeof(resume_file), "%s/resume.cfg", resume_root);
	FILE* missing_resume = fopen(resume_file, "w");
	if (missing_resume) {
		fprintf(missing_resume, "type=1\nsource=0\ntransport=1\nfolder_path=%s\ntrack_path=%s\nposition_ms=2000\n",
				root, missing);
		fclose(missing_resume);
	}
	check(missing_resume != NULL, "create missing-source resume fixture");
	daemon = start_owner(argv[1], socket_path, &fd);
	check(fd >= 0, "missing-source owner restart");
	if (fd >= 0) {
		check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_NONE && response.error[0],
			  "missing-source restore reports error");
		check(access(resume_file, F_OK) != 0, "missing-source restore clears saved intent");
	}

	check(run_cli(argv[2], "load", missing) != 0, "failed load CLI returns error");
	MusicLoadRequest load;
	memset(&load, 0, sizeof(load));
	strncpy(load.path, first, sizeof(load.path) - 1);
	fd = raw_connect(socket_path);
	check(fd >= 0, "load client attach");
	if (fd >= 0) {
		int load_status = raw_request(fd, MUSIC_CMD_LOAD, &load, sizeof(load), &response);
		if (load_status != 0)
			fprintf(stderr, "load status=%d error=%s file=%s daemon_alive=%d\n", load_status, response.error, response.snapshot.current_file, kill(daemon, 0) == 0);
		check(load_status == 0 && response.snapshot.state == MUSIC_STATE_STOPPED &&
				  !response.snapshot.audio_open,
			  "load local track stopped with audio closed");
		check(raw_request(fd, MUSIC_CMD_TOGGLE, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.audio_open,
			  "toggle starts loaded stopped track and opens audio");
		check(raw_request(fd, MUSIC_CMD_SEEK, &(MusicIntRequest){.value = 100}, sizeof(MusicIntRequest), &response) == 0,
			  "seek before pause lifecycle check");
		check(raw_request(fd, MUSIC_CMD_PAUSE, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PAUSED && !response.snapshot.audio_open &&
				  response.snapshot.position_ms >= 100 && response.snapshot.position_ms <= 250,
			  "pause closes audio while retaining consumed position");
		check(raw_request(fd, MUSIC_CMD_NEXT, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_STOPPED && !response.snapshot.audio_open &&
				  strstr(response.snapshot.current_file, "02.wav"),
			  "next track remains stopped with audio closed after pause");
		check(raw_request(fd, MUSIC_CMD_TOGGLE, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.audio_open,
			  "toggle starts selected stopped track");
		check(raw_request(fd, MUSIC_CMD_TOGGLE, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PAUSED && !response.snapshot.audio_open,
			  "toggle pauses and closes playing track");
		check(raw_request(fd, MUSIC_CMD_TOGGLE, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.audio_open,
			  "toggle resumes paused track and opens audio");
		check(raw_request(fd, MUSIC_CMD_PREVIOUS, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.queue_index == 0,
			  "previous preserves playing intent");
		wait_ms(300);
		check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.position_ms > 0,
			  "previous track consumes audio");
		check(raw_request(fd, MUSIC_CMD_NEXT, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.queue_index == 1,
			  "next preserves playing intent");
		wait_ms(300);
		check(raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PLAYING && response.snapshot.position_ms > 0,
			  "next track consumes audio");

		/* Reattach to the live owner and verify its source identity rather than
		 * merely echoing a client payload. Folder and M3U queues have distinct
		 * reconstruction contracts. */
		close(fd);
		fd = raw_connect(socket_path);
		check(fd >= 0 && raw_request(fd, MUSIC_CMD_LOAD, &(MusicLoadRequest){.path = {0}}, 0, &response) != 0,
			  "reattach does not accept malformed load");
		MusicLoadRequest folder_load;
		memset(&folder_load, 0, sizeof(folder_load));
		strncpy(folder_load.path, root, sizeof(folder_load.path) - 1);
		check(raw_request(fd, MUSIC_CMD_LOAD, &folder_load, sizeof(folder_load), &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_LOCAL &&
				  response.snapshot.queue_kind == MUSIC_QUEUE_FOLDER &&
				  strcmp(response.snapshot.queue_path, root) == 0,
			  "owner identifies directory queue after client reattach");
		MusicPlaylistLoadRequest playlist_load;
		memset(&playlist_load, 0, sizeof(playlist_load));
		strncpy(playlist_load.path, playlist_path, sizeof(playlist_load.path) - 1);
		playlist_load.index = 1;
		check(raw_request(fd, MUSIC_CMD_LOAD_PLAYLIST, &playlist_load, sizeof(playlist_load), &response) == 0 &&
				  response.snapshot.queue_kind == MUSIC_QUEUE_M3U &&
				  strcmp(response.snapshot.queue_path, playlist_path) == 0 &&
				  response.snapshot.queue_index == 1,
			  "owner identifies M3U queue after client reattach");

		MusicRadioLoadRequest radio_request;
		memset(&radio_request, 0, sizeof(radio_request));
		strncpy(radio_request.url, "http://127.0.0.1:1/music", sizeof(radio_request.url) - 1);
		check(raw_request(fd, MUSIC_CMD_RADIO_LOAD, &radio_request, sizeof(radio_request), &response) == 0 &&
				  response.status == MUSIC_STATUS_OK && response.snapshot.source == MUSIC_SOURCE_RADIO,
			  "radio source starts behind service");
		check((response.snapshot.source_state == MUSIC_RADIO_CONNECTING ||
			   response.snapshot.source_state == MUSIC_RADIO_BUFFERING ||
			   response.snapshot.source_state == MUSIC_RADIO_PLAYING) &&
				  (response.snapshot.source_state == MUSIC_RADIO_PLAYING
					   ? response.snapshot.state == MUSIC_STATE_PLAYING
					   : response.snapshot.state == MUSIC_STATE_PAUSED) &&
				  (response.snapshot.capabilities & (MUSIC_CAP_PLAY | MUSIC_CAP_PAUSE)) ==
					  (MUSIC_CAP_PLAY | MUSIC_CAP_PAUSE),
			  "radio snapshot publishes effective transport and controls");
		check(raw_request(fd, MUSIC_CMD_LOAD, &load, sizeof(load), &response) == 0 &&
				  response.snapshot.source == MUSIC_SOURCE_LOCAL && !response.snapshot.audio_open,
			  "local load switches source exclusively without opening audio");
		check(raw_request(fd, MUSIC_CMD_PLAY, NULL, 0, &response) == 0 &&
				  raw_request(fd, MUSIC_CMD_SLEEP, NULL, 0, &response) == 0 &&
				  response.snapshot.audio_open == 0 && response.snapshot.state == MUSIC_STATE_PLAYING,
			  "sleep closes audio while preserving playing intent");
		check(raw_request(fd, MUSIC_CMD_WAKE, NULL, 0, &response) == 0 &&
				  response.snapshot.audio_open != 0 && response.snapshot.state == MUSIC_STATE_PLAYING,
			  "wake restores playing intent");
		check(raw_request(fd, MUSIC_CMD_PAUSE, NULL, 0, &response) == 0 &&
				  !response.snapshot.audio_open &&
				  raw_request(fd, MUSIC_CMD_SLEEP, NULL, 0, &response) == 0 &&
				  raw_request(fd, MUSIC_CMD_WAKE, NULL, 0, &response) == 0 &&
				  response.snapshot.state == MUSIC_STATE_PAUSED && !response.snapshot.audio_open,
			  "wake preserves paused intent with audio closed");
		check(run_cli(argv[2], "shuffle", NULL) == 0, "shuffle CLI uses zero payload");
		check(raw_request(fd, MUSIC_CMD_LOAD, &load, sizeof(load), &response) == 0, "reload first track after shuffle");
		int play_status = raw_request(fd, MUSIC_CMD_PLAY, NULL, 0, &response);
		if (play_status != 0)
			fprintf(stderr, "play status=%d error=%s file=%s\n", play_status, response.error, response.snapshot.current_file);
		check(play_status == 0, "play local track");
		bool advanced = false, second_playing = false, second_position_advanced = false, ended = false;
		int second_initial_position = -1;
		int64_t deadline = clock_ms() + 10000;
		while (clock_ms() < deadline) {
			wait_ms(80);
			if (raw_request(fd, MUSIC_CMD_SNAPSHOT, NULL, 0, &response) != 0)
				break;
			if (strstr(response.snapshot.current_file, "02.wav")) {
				advanced = true;
				if (response.snapshot.state == 1) {
					second_playing = true;
					if (second_initial_position < 0)
						second_initial_position = response.snapshot.position_ms;
					else if (response.snapshot.position_ms > second_initial_position)
						second_position_advanced = true;
				}
			}
			if (advanced && response.snapshot.stream_eof && response.snapshot.state == 0) {
				ended = true;
				break;
			}
		}
		check(advanced, "EOF advances local queue");
		check(second_playing, "auto-advanced track reaches PLAYING");
		check(second_position_advanced, "auto-advanced track position advances");
		check(ended, "actual EOF reaches stopped state");
		close(fd);
	}

	fd = raw_connect(socket_path);
	if (fd >= 0) {
		(void)raw_request(fd, MUSIC_CMD_SHUTDOWN, NULL, 0, &response);
		close(fd);
	}
	waitpid(daemon, NULL, 0);
	check(run_cli(argv[2], "snapshot", NULL) != 0, "disconnected CLI returns safe error");
	/* Dead owner metadata and a stale socket inode are cleaned before rebinding. */
	char stale_lock_path[180];
	snprintf(stale_lock_path, sizeof(stale_lock_path), "%s.lock", socket_path);
	FILE* stale_lock = fopen(stale_lock_path, "w");
	if (stale_lock) {
		fputs("999999\n", stale_lock);
		fclose(stale_lock);
	}
	int stale_socket = open(socket_path, O_WRONLY | O_CREAT, 384);
	if (stale_socket >= 0)
		close(stale_socket);
	daemon = fork();
	if (daemon == 0) {
		execl(argv[1], argv[1], NULL);
		_exit(127);
	}
	fd = -1;
	for (int i = 0; i < 100 && fd < 0; i++) {
		wait_ms(20);
		fd = raw_connect(socket_path);
	}
	check(fd >= 0, "stale socket cleanup and restart");
	if (fd >= 0) {
		(void)raw_request(fd, MUSIC_CMD_SHUTDOWN, NULL, 0, &response);
		close(fd);
	}
	waitpid(daemon, NULL, 0);
cleanup:
	unlink(socket_path);
	char lock[180];
	snprintf(lock, sizeof(lock), "%s.lock", socket_path);
	unlink(lock);
	unlink(first);
	unlink(second);
	unlink(playlist_path);
	rmdir(root);
	return failures ? 1 : 0;
}
