// Cross-compiled for a tg5040 device and run on a Brick by
// scripts/tests/test-music-service-e2e.sh. It drives music_client.c against a
// fake in-process owner thread, so it takes no arguments. Not built by
// tests/run_tests.sh.
#include "../music_client.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int failures;
static void check(int condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}
static int read_full(int fd, void* buffer, size_t length) {
	size_t done = 0;
	while (done < length) {
		ssize_t n = recv(fd, (char*)buffer + done, length - done, 0);
		if (n <= 0)
			return -1;
		done += (size_t)n;
	}
	return 0;
}
static int write_full(int fd, const void* buffer, size_t length) {
	size_t done = 0;
	while (done < length) {
		ssize_t n = send(fd, (const char*)buffer + done, length - done, 0);
		if (n <= 0)
			return -1;
		done += (size_t)n;
	}
	return 0;
}

typedef struct {
	char path[sizeof(((struct sockaddr_un*)0)->sun_path)];
	int received_command;
	int received_index;
	char received_playlist[MUSIC_SERVICE_MAX_PATH];
	char received_folder[MUSIC_SERVICE_MAX_PATH];
	char received_selected[MUSIC_SERVICE_MAX_PATH];
	char received_feed[MUSIC_SERVICE_MAX_PATH];
	char received_guid[128];
	int received_progress_sec;
	float received_speed;
	int received_progress_command;
	int received_mark_command;
	int received_speed_command;
	int received_volume;
	int received_volume_command;
} ServerState;

static void* server_main(void* argument) {
	ServerState* state = argument;
	int listener = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, state->path, sizeof(address.sun_path) - 1);
	unlink(state->path);
	if (listener < 0 || bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0 || listen(listener, 1) != 0)
		return NULL;
	int client = accept(listener, NULL, NULL);
	if (client >= 0) {
		for (int request_number = 0; request_number < 7; request_number++) {
			MusicFrameHeader request;
			if (read_full(client, &request, sizeof(request)) != 0 || request.payload_length > MUSIC_SERVICE_MAX_FRAME - sizeof(request))
				break;
			unsigned char payload[MUSIC_SERVICE_MAX_FRAME];
			if (read_full(client, payload, request.payload_length) != 0)
				break;
			state->received_command = request.command;
			MusicResponseWire response;
			memset(&response, 0, sizeof(response));
			response.status = request_number == 0 ? MUSIC_STATUS_OK : MUSIC_STATUS_NOT_FOUND;
			if (response.status != MUSIC_STATUS_OK)
				strncpy(response.error, "owner rejected", sizeof(response.error) - 1);
			response.snapshot.state = MUSIC_STATE_PLAYING;
			response.snapshot.source = MUSIC_SOURCE_LOCAL;
			response.snapshot.loaded = 1;
			strncpy(response.snapshot.current_file, "/Music/owner.wav", sizeof(response.snapshot.current_file) - 1);
			if (request.command == MUSIC_CMD_SET_SPEED) {
				response.snapshot.source = MUSIC_SOURCE_RADIO;
				response.snapshot.source_state = MUSIC_RADIO_ERROR;
				strncpy(response.snapshot.current_file, "https://owner.invalid/station", sizeof(response.snapshot.current_file) - 1);
			}
			if (request.command == MUSIC_CMD_LOAD && request.payload_length == sizeof(MusicLoadRequest)) {
				const MusicLoadRequest* load = (const MusicLoadRequest*)payload;
				strncpy(state->received_folder, load->path, sizeof(state->received_folder) - 1);
				strncpy(state->received_selected, load->selected_path, sizeof(state->received_selected) - 1);
			} else if (request.command == MUSIC_CMD_LOAD_PLAYLIST && request.payload_length == sizeof(MusicPlaylistLoadRequest)) {
				const MusicPlaylistLoadRequest* playlist = (const MusicPlaylistLoadRequest*)payload;
				state->received_index = playlist->index;
				strncpy(state->received_playlist, playlist->path, sizeof(state->received_playlist) - 1);
				strncpy(response.snapshot.queue_path, playlist->path, sizeof(response.snapshot.queue_path) - 1);
			} else if ((request.command == MUSIC_CMD_PODCAST_PROGRESS ||
						request.command == MUSIC_CMD_PODCAST_MARK_PLAYED) &&
					   request.payload_length == sizeof(MusicPodcastProgressRequest)) {
				const MusicPodcastProgressRequest* progress = (const MusicPodcastProgressRequest*)payload;
				strncpy(state->received_feed, progress->feed_url, sizeof(state->received_feed) - 1);
				strncpy(state->received_guid, progress->episode_guid, sizeof(state->received_guid) - 1);
				state->received_progress_sec = progress->position_sec;
				if (request.command == MUSIC_CMD_PODCAST_PROGRESS)
					state->received_progress_command++;
				else
					state->received_mark_command++;
			} else if (request.command == MUSIC_CMD_SET_SPEED && request.payload_length == sizeof(MusicSpeedRequest)) {
				state->received_speed = ((const MusicSpeedRequest*)payload)->speed;
				state->received_speed_command++;
			} else if (request.command == MUSIC_CMD_SET_VOLUME && request.payload_length == sizeof(MusicIntRequest)) {
				state->received_volume = ((const MusicIntRequest*)payload)->value;
				state->received_volume_command++;
			}
			MusicFrameHeader reply = {.magic = MUSIC_SERVICE_MAGIC,
									  .version = MUSIC_SERVICE_PROTOCOL_VERSION,
									  .command = request.command,
									  .request_id = request.request_id,
									  .payload_length = sizeof(response)};
			if (write_full(client, &reply, sizeof(reply)) != 0 || write_full(client, &response, sizeof(response)) != 0)
				break;
		}
		close(client);
	}
	close(listener);
	return NULL;
}

int main(void) {
	ServerState server;
	char socket_dir[sizeof(server.path) - sizeof("/control.sock")];
	memset(&server, 0, sizeof(server));
	snprintf(socket_dir, sizeof(socket_dir), "/tmp/nx_music_client_test_%ld", (long)getpid());
	mkdir(socket_dir, 448);
	snprintf(server.path, sizeof(server.path), "%s/control.sock", socket_dir);
	setenv(MUSIC_SERVICE_SOCKET_ENV, server.path, 1);
	pthread_t thread;
	check(pthread_create(&thread, NULL, server_main, &server) == 0, "fake owner starts");
	usleep(50000);

	check(MusicClient_init(NULL) == 0, "passive client attaches to existing owner");
	check(MusicClient_isConnected(), "client remains attached after initial snapshot");
	check(MusicClient_loadFolder("/Music/root", "/Music/root/nested/song.ogg") == MUSIC_STATUS_NOT_FOUND &&
			  strcmp(server.received_folder, "/Music/root") == 0 &&
			  strcmp(server.received_selected, "/Music/root/nested/song.ogg") == 0,
		  "folder selection sends recursive root and selected track to owner");
	check(MusicClient_loadPlaylist("/Music/list.m3u", 7) == MUSIC_STATUS_NOT_FOUND,
		  "application error is returned to caller");
	check(MusicClient_isConnected(), "application error does not look like disconnect");
	check(MusicClient_snapshot()->state == MUSIC_STATE_PLAYING && MusicClient_snapshot()->loaded,
		  "application error keeps fresh loaded snapshot");
	check(strcmp(MusicClient_error(), "owner rejected") == 0, "application error is exposed by client");
	check(server.received_command == MUSIC_CMD_LOAD_PLAYLIST && server.received_index == 7 &&
			  strcmp(server.received_playlist, "/Music/list.m3u") == 0,
		  "playlist selection sends owner identity and index");
	check(MusicClient_setPodcastProgress("feed", "episode", 42) == MUSIC_STATUS_NOT_FOUND &&
			  server.received_progress_command == 1 && server.received_progress_sec == 42 &&
			  strcmp(server.received_feed, "feed") == 0 && strcmp(server.received_guid, "episode") == 0,
		  "podcast progress sends stable identity to owner");
	check(MusicClient_markPodcastPlayed("feed", "episode", true) == MUSIC_STATUS_NOT_FOUND &&
			  server.received_mark_command == 1 && server.received_progress_sec == -1,
		  "podcast played state sends owner command");
	check(MusicClient_setSpeed(1.5f) == MUSIC_STATUS_NOT_FOUND &&
			  server.received_speed_command == 1 && server.received_speed == 1.5f,
		  "podcast speed sends owner command");
	check(MusicClient_setVolume(7) == MUSIC_STATUS_NOT_FOUND &&
			  server.received_volume_command == 1 && server.received_volume == 7,
		  "music volume sends owner command");
	check(!MusicClient_isRadioActive(), "radio error state is not active after reattach");

	check(MusicClient_stop() == MUSIC_SERVICE_TRANSPORT_ERROR, "transport disconnect is reported distinctly");
	check(!MusicClient_isConnected() && MusicClient_isStopped() && !MusicClient_snapshot()->loaded &&
			  MusicClient_snapshot()->source == MUSIC_SOURCE_NONE,
		  "transport disconnect clears cached playback state");
	MusicClient_quit();
	pthread_join(thread, NULL);
	unlink(server.path);
	rmdir(socket_dir);

	/* Spawn attempts for an owner that never comes up back off: one immediate
	 * attempt, the next no sooner than 1 s later, then 2 s, never one per poll
	 * (music_client.c MUSIC_CLIENT_SPAWN_BACKOFF_MIN_MS). Timeline from the
	 * start of MusicClient_init: attempt 1 at +0, attempt 2 at +1.0..+1.5 s
	 * (the 500 ms reconnect gate adds up to one tick), attempt 3 at +3.0..+4.0 s. */
	char launch_script[160], launch_count[160];
	snprintf(launch_script, sizeof(launch_script), "/tmp/music_client_launch_%ld.sh", (long)getpid());
	snprintf(launch_count, sizeof(launch_count), "/tmp/music_client_launch_%ld.count", (long)getpid());
	FILE* script = fopen(launch_script, "w");
	if (script) {
		fprintf(script, "#!/bin/sh\ncount=0\n[ -f '%s' ] && count=$(cat '%s')\necho $((count + 1)) > '%s'\n",
				launch_count, launch_count, launch_count);
		fclose(script);
		chmod(launch_script, 448);
	}
	check(script != NULL, "retry launch script created");
	struct timespec spawn_start;
	clock_gettime(CLOCK_MONOTONIC, &spawn_start);
	check(MusicClient_init(launch_script) != 0, "failed owner remains unavailable");
	int launch_attempts = 0;
	FILE* count_file = fopen(launch_count, "r");
	if (count_file) {
		fscanf(count_file, "%d", &launch_attempts);
		fclose(count_file);
	}
	check(launch_attempts >= 1 && launch_attempts <= 2, "failed owner launch is attempted once, at most twice, during init");
	/* Keep polling like the app does and sample the attempt count at +2.5 s and
	 * +4.5 s from the start: exactly 2 then exactly 3 proves the 1 s / 2 s gaps. */
	int attempts_at_2500 = -1, attempts_at_4500 = -1;
	for (;;) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed_ms = (now.tv_sec - spawn_start.tv_sec) * 1000 + (now.tv_nsec - spawn_start.tv_nsec) / 1000000;
		if (elapsed_ms >= 2500 && attempts_at_2500 < 0) {
			attempts_at_2500 = 0;
			count_file = fopen(launch_count, "r");
			if (count_file) {
				fscanf(count_file, "%d", &attempts_at_2500);
				fclose(count_file);
			}
		}
		if (elapsed_ms >= 4500) {
			attempts_at_4500 = 0;
			count_file = fopen(launch_count, "r");
			if (count_file) {
				fscanf(count_file, "%d", &attempts_at_4500);
				fclose(count_file);
			}
			break;
		}
		MusicClient_update();
		usleep(50000);
	}
	check(attempts_at_2500 == 2, "second spawn attempt lands after the 1 s backoff and no other before 2.5 s");
	check(attempts_at_4500 == 3, "third spawn attempt lands after the 2 s backoff and no other before 4.5 s");
	launch_attempts = attempts_at_4500;
	MusicClient_quit();
	check(MusicClient_init(NULL) != 0, "passive client stays unavailable without an owner");
	usleep(100000);
	count_file = fopen(launch_count, "r");
	int passive_launch_attempts = 0;
	if (count_file) {
		fscanf(count_file, "%d", &passive_launch_attempts);
		fclose(count_file);
	}
	check(passive_launch_attempts == launch_attempts, "passive client never launches an owner");
	MusicClient_quit();
	unlink(launch_script);
	unlink(launch_count);
	unsetenv(MUSIC_SERVICE_SOCKET_ENV);
	return failures ? 1 : 0;
}
