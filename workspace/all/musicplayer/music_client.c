#include "music_client.h"
#include "music_service_client.h"
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_CLIENT_POLL_INTERVAL_MS 250
#define MUSIC_CLIENT_RECONNECT_INTERVAL_MS 500
#define MUSIC_CLIENT_REQUEST_TIMEOUT_MS 100
/* Commands that make the owner stop one source and open another (decoder
 * open, catalog reload, queued seek) run synchronously inside the request
 * and measured ~0.7 s on a Brick; 1 s produced false "Failed to play"
 * results while playback started anyway. Match musicplayerctl's budget. */
#define MUSIC_CLIENT_LOAD_TIMEOUT_MS 5000
#define MUSIC_CLIENT_COMMAND_TIMEOUT_MS 1000

static int client_fd = -1;
static MusicSnapshotWire current_snapshot;
static char current_error[MUSIC_SERVICE_MAX_ERROR];
static char owner_path[MUSIC_SERVICE_MAX_PATH];
static int64_t next_reconnect_ms;
static int64_t next_poll_ms;
#define MUSIC_CLIENT_SPAWN_BACKOFF_MIN_MS 1000
#define MUSIC_CLIENT_SPAWN_BACKOFF_MAX_MS 30000
static int64_t next_spawn_ms;
static int spawn_backoff_ms = MUSIC_CLIENT_SPAWN_BACKOFF_MIN_MS;

static int64_t monotonic_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void clear_snapshot(void) {
	memset(&current_snapshot, 0, sizeof(current_snapshot));
	current_snapshot.state = MUSIC_STATE_STOPPED;
	current_snapshot.source_state = MUSIC_STATE_STOPPED;
	current_error[0] = '\0';
}

static void detach(void) {
	if (client_fd >= 0)
		MusicService_disconnect(client_fd);
	client_fd = -1;
	clear_snapshot();
	next_reconnect_ms = monotonic_ms() + MUSIC_CLIENT_RECONNECT_INTERVAL_MS;
}

static void close_inherited_fds(void) {
	struct rlimit limits;
	long maximum = 1024;
	if (getrlimit(RLIMIT_NOFILE, &limits) == 0 && limits.rlim_max < (rlim_t)maximum)
		maximum = (long)limits.rlim_max;
	for (int fd = 3; fd < maximum; fd++)
		close(fd);
}

static int start_daemon(void) {
	if (!owner_path[0] || access(owner_path, X_OK) != 0)
		return -1;
	pid_t child = fork();
	if (child < 0)
		return -1;
	if (child == 0) {
		/* Double-fork so the daemon is reparented to init and never becomes this
		 * process's child: nothing is ever left to reap. */
		(void)setsid();
		pid_t grandchild = fork();
		if (grandchild < 0)
			_exit(127);
		if (grandchild == 0) {
			int null_fd = open("/dev/null", O_RDWR);
			if (null_fd >= 0) {
				(void)dup2(null_fd, STDIN_FILENO);
				(void)dup2(null_fd, STDOUT_FILENO);
				(void)dup2(null_fd, STDERR_FILENO);
				if (null_fd > STDERR_FILENO)
					close(null_fd);
			}
			close_inherited_fds();
			execl(owner_path, owner_path, (char*)NULL);
			_exit(127);
		}
		_exit(0);
	}
	(void)waitpid(child, NULL, 0);
	return 0;
}

static bool attach(bool allow_start) {
	if (client_fd >= 0)
		return true;
	int64_t now = monotonic_ms();
	if (now < next_reconnect_ms)
		return false;
	client_fd = MusicService_connect(NULL, 20);
	if (client_fd >= 0) {
		next_poll_ms = 0;
		next_spawn_ms = 0;
		spawn_backoff_ms = MUSIC_CLIENT_SPAWN_BACKOFF_MIN_MS;
		return true;
	}
	next_reconnect_ms = now + MUSIC_CLIENT_RECONNECT_INTERVAL_MS;
	/* Respawn a crashed owner with exponential backoff rather than every reconnect
	 * tick forever. The owner lock stays the singleton guard. The first failed
	 * connect still spawns immediately (MusicClient_init's retry loop relies on
	 * that); the next waits 1 s, then 2, 4, 8, 16, 30, 30, ... */
	if (allow_start && now >= next_spawn_ms) {
		(void)start_daemon();
		next_spawn_ms = now + spawn_backoff_ms;
		spawn_backoff_ms = spawn_backoff_ms * 2 > MUSIC_CLIENT_SPAWN_BACKOFF_MAX_MS
							   ? MUSIC_CLIENT_SPAWN_BACKOFF_MAX_MS
							   : spawn_backoff_ms * 2;
	}
	return false;
}

static int request(uint16_t command, const void* payload, size_t length, int timeout_ms) {
	if (!attach(owner_path[0] != '\0'))
		return MUSIC_SERVICE_TRANSPORT_ERROR;
	MusicResponseWire response;
	int status = MusicService_request(client_fd, command, payload, length, &response, timeout_ms);
	if (status == MUSIC_SERVICE_TRANSPORT_ERROR) {
		detach();
		return status;
	}
	/* An application rejection is a valid response from the owner. Keep the
	 * connection and publish its current state; only transport failure detaches. */
	current_snapshot = response.snapshot;
	strncpy(current_error, response.error, sizeof(current_error) - 1);
	current_error[sizeof(current_error) - 1] = '\0';
	return status;
}

int MusicClient_init(const char* daemon_path) {
	if (client_fd >= 0)
		return 0;
	clear_snapshot();
	if (daemon_path && daemon_path[0])
		strncpy(owner_path, daemon_path, sizeof(owner_path) - 1);
	else
		owner_path[0] = '\0';
	owner_path[sizeof(owner_path) - 1] = '\0';
	next_reconnect_ms = 0;
	next_spawn_ms = 0;
	spawn_backoff_ms = MUSIC_CLIENT_SPAWN_BACKOFF_MIN_MS;
	if (!owner_path[0]) {
		(void)request(MUSIC_CMD_SNAPSHOT, NULL, 0, 100);
		return client_fd >= 0 ? 0 : -1;
	}
	for (int attempt = 0; attempt < 20 && client_fd < 0; attempt++) {
		if (request(MUSIC_CMD_SNAPSHOT, NULL, 0, 100) >= MUSIC_STATUS_OK)
			return 0;
		usleep(50000);
	}
	return client_fd >= 0 ? 0 : -1;
}

void MusicClient_quit(void) {
	if (client_fd >= 0)
		MusicService_disconnect(client_fd);
	client_fd = -1;
	clear_snapshot();
}

void MusicClient_disconnect(void) {
	/* Drop the socket without spawning or restarting the owner, so an idle
	 * daemon can exit while the panel is hidden. */
	detach();
}

void MusicClient_update(void) {
	int64_t now = monotonic_ms();
	if (client_fd < 0) {
		if (!attach(owner_path[0] != '\0'))
			return;
		next_poll_ms = 0;
	}
	if (now < next_poll_ms)
		return;
	next_poll_ms = now + MUSIC_CLIENT_POLL_INTERVAL_MS;
	(void)request(MUSIC_CMD_SNAPSHOT, NULL, 0, MUSIC_CLIENT_REQUEST_TIMEOUT_MS);
}

const MusicSnapshotWire* MusicClient_snapshot(void) {
	return &current_snapshot;
}

const char* MusicClient_error(void) {
	return current_error;
}

bool MusicClient_isConnected(void) {
	return client_fd >= 0;
}
bool MusicClient_isPlaying(void) {
	return current_snapshot.state == MUSIC_STATE_PLAYING;
}
bool MusicClient_isPaused(void) {
	return current_snapshot.state == MUSIC_STATE_PAUSED;
}
bool MusicClient_isStopped(void) {
	return current_snapshot.state == MUSIC_STATE_STOPPED;
}
bool MusicClient_isRadioActive(void) {
	return current_snapshot.source == MUSIC_SOURCE_RADIO &&
		   current_snapshot.source_state != MUSIC_RADIO_STOPPED &&
		   current_snapshot.source_state != MUSIC_RADIO_ERROR;
}
bool MusicClient_isPodcastActive(void) {
	return current_snapshot.source == MUSIC_SOURCE_PODCAST && current_snapshot.state != MUSIC_STATE_STOPPED;
}
int MusicClient_position(void) {
	return current_snapshot.position_ms;
}
int MusicClient_duration(void) {
	return current_snapshot.duration_ms;
}
float MusicClient_speed(void) {
	return current_snapshot.playback_speed;
}
int MusicClient_format(void) {
	return current_snapshot.format;
}

int MusicClient_load(const char* path) {
	MusicLoadRequest request_payload = {0};
	if (!path)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(request_payload.path, path, sizeof(request_payload.path) - 1);
	return request(MUSIC_CMD_LOAD, &request_payload, sizeof(request_payload), MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}

int MusicClient_loadFolder(const char* path, const char* selected_path) {
	MusicLoadRequest request_payload = {0};
	if (!path || !selected_path)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(request_payload.path, path, sizeof(request_payload.path) - 1);
	strncpy(request_payload.selected_path, selected_path, sizeof(request_payload.selected_path) - 1);
	return request(MUSIC_CMD_LOAD, &request_payload, sizeof(request_payload), MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}

int MusicClient_loadPlaylist(const char* path, int index) {
	MusicPlaylistLoadRequest request_payload = {0};
	if (!path || index < 0)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(request_payload.path, path, sizeof(request_payload.path) - 1);
	request_payload.index = index;
	return request(MUSIC_CMD_LOAD_PLAYLIST, &request_payload, sizeof(request_payload), MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}

int MusicClient_loadRadio(const char* url) {
	MusicRadioLoadRequest request_payload = {0};
	if (!url)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(request_payload.url, url, sizeof(request_payload.url) - 1);
	return request(MUSIC_CMD_RADIO_LOAD, &request_payload, sizeof(request_payload), MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}

int MusicClient_loadPodcast(const char* feed_url, const char* episode_guid) {
	MusicPodcastLoadRequest request_payload = {0};
	if (!feed_url || !episode_guid)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(request_payload.feed_url, feed_url, sizeof(request_payload.feed_url) - 1);
	strncpy(request_payload.episode_guid, episode_guid, sizeof(request_payload.episode_guid) - 1);
	return request(MUSIC_CMD_PODCAST_LOAD, &request_payload, sizeof(request_payload), MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}

int MusicClient_select(int index) {
	MusicIntRequest payload = {.value = index};
	return request(MUSIC_CMD_SELECT, &payload, sizeof(payload), MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}
int MusicClient_play(void) {
	return request(MUSIC_CMD_PLAY, NULL, 0, MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_pause(void) {
	return request(MUSIC_CMD_PAUSE, NULL, 0, MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_stop(void) {
	return request(MUSIC_CMD_STOP, NULL, 0, MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_toggle(void) {
	return request(MUSIC_CMD_TOGGLE, NULL, 0, MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_next(void) {
	return request(MUSIC_CMD_NEXT, NULL, 0, MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}
int MusicClient_previous(void) {
	return request(MUSIC_CMD_PREVIOUS, NULL, 0, MUSIC_CLIENT_LOAD_TIMEOUT_MS);
}
int MusicClient_seek(int position_ms) {
	MusicIntRequest payload = {.value = position_ms};
	return request(MUSIC_CMD_SEEK, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_setPodcastProgress(const char* feed_url, const char* episode_guid, int position_sec) {
	MusicPodcastProgressRequest payload = {.position_sec = position_sec};
	if (!feed_url || !episode_guid)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(payload.feed_url, feed_url, sizeof(payload.feed_url) - 1);
	strncpy(payload.episode_guid, episode_guid, sizeof(payload.episode_guid) - 1);
	return request(MUSIC_CMD_PODCAST_PROGRESS, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_markPodcastPlayed(const char* feed_url, const char* episode_guid, bool played) {
	MusicPodcastProgressRequest payload = {.position_sec = played ? -1 : 0};
	if (!feed_url || !episode_guid)
		return MUSIC_STATUS_BAD_REQUEST;
	strncpy(payload.feed_url, feed_url, sizeof(payload.feed_url) - 1);
	strncpy(payload.episode_guid, episode_guid, sizeof(payload.episode_guid) - 1);
	return request(MUSIC_CMD_PODCAST_MARK_PLAYED, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_setSpeed(float speed) {
	MusicSpeedRequest payload = {.speed = speed};
	return request(MUSIC_CMD_SET_SPEED, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_setVolume(int volume) {
	MusicIntRequest payload = {.value = volume};
	return request(MUSIC_CMD_SET_VOLUME, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_setAudioSettings(int bass_filter_hz, float soft_limiter_threshold,
								 int rate_mode_follow, int resampler_quality, int buffer_frames) {
	MusicAudioSettingsRequest payload = {
		.bass_filter_hz = bass_filter_hz,
		.soft_limiter_threshold = soft_limiter_threshold,
		.rate_mode_follow = rate_mode_follow,
		.resampler_quality = resampler_quality,
		.buffer_frames = buffer_frames};
	return request(MUSIC_CMD_AUDIO_SETTINGS, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_setRepeat(bool repeat) {
	MusicIntRequest payload = {.value = repeat ? 1 : 0};
	return request(MUSIC_CMD_REPEAT, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_setShuffle(bool shuffle) {
	MusicIntRequest payload = {.value = shuffle ? 1 : 0};
	return request(MUSIC_CMD_SET_SHUFFLE, &payload, sizeof(payload), MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
int MusicClient_shuffle(void) {
	return request(MUSIC_CMD_SHUFFLE, NULL, 0, MUSIC_CLIENT_COMMAND_TIMEOUT_MS);
}
