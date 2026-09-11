#include "music_service_protocol.h"
#include "player.h"
#include "playlist.h"
#include "playlist_m3u.h"
#include "podcast.h"
#include "radio.h"
#include "settings.h"
#include "resume.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_surface.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

void InitSettings(void);
void QuitSettings(void);
int GetMusicVolume(void);
void SetMusicVolume(int volume);

#define MAX_CLIENTS 8
#define CLIENT_FRAME_TIMEOUT_MS 2000
#define WAKE_ROUTE_DEADLINE_MS 5000

typedef struct {
	int fd;
	unsigned char input[MUSIC_SERVICE_MAX_FRAME];
	size_t input_length;
	int64_t frame_deadline_ms;
} Client;

static volatile sig_atomic_t quit;
static PlaylistContext queue;
static int listen_fd = -1;
static char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
static char lock_path[sizeof(socket_path) + 8];
static bool eof_advanced;
static bool shuffle_enabled;
static MusicSource active_source;
static bool has_active_source(void);
static int queue_kind;
static char queue_path[MUSIC_SERVICE_MAX_PATH];
static int podcast_feed_index = -1;
static int podcast_episode_index = -1;
static bool podcast_waiting_for_seek;
static int64_t podcast_last_progress_ms;
static int64_t local_last_resume_ms;
static char artwork_source[MUSIC_SERVICE_MAX_PATH];
static char artwork_path[MUSIC_SERVICE_MAX_PATH];
static unsigned int artwork_generation;
static bool sleeping;
static bool wake_pending;
static bool sleep_resume_intent;
static bool sleep_audio_open;
static MusicSource sleep_source;
static char sleep_radio_url[MUSIC_SERVICE_MAX_PATH];
static int64_t wake_deadline_ms;
static bool wake_failed;
static bool radio_paused;
static ResumeTransportState podcast_resume_transport = RESUME_TRANSPORT_PLAYING;
static bool restore_pending;
static int64_t restore_deadline_ms;
static char service_error[MUSIC_SERVICE_MAX_ERROR];
static int applied_music_volume = -1;

static void save_resume_state(void);
static void attempt_restore(void);

/* Podcast downloads use this hook to keep the foreground app's autosleep
 * policy unchanged. The service has no UI or autosleep state to manage. */
void ModuleCommon_setAutosleepDisabled(bool disabled) {
	(void)disabled;
}

void Podcast_clearThumbnailCache(void) {
}

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sync_music_volume(void) {
	int volume = GetMusicVolume();
	if (volume < 0)
		volume = 0;
	if (volume > 20)
		volume = 20;
	if (volume != applied_music_volume) {
		Player_setVolume(volume / 20.0f);
		applied_music_volume = volume;
	}
}

static void on_signal(int sig) {
	if (sig == SIGINT || sig == SIGTERM)
		quit = 1;
}

static void close_client(Client* client) {
	if (client->fd >= 0)
		close(client->fd);
	client->fd = -1;
	client->input_length = 0;
	client->frame_deadline_ms = 0;
}

static int acquire_lock(void) {
	int fd = open(lock_path, O_WRONLY | O_CREAT | O_EXCL, 384);
	if (fd >= 0) {
		char pid[32];
		int n = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
		(void)write(fd, pid, (size_t)n);
		close(fd);
		return 0;
	}
	if (errno != EEXIST)
		return -1;
	fd = open(lock_path, O_RDONLY);
	char pid[32] = {0};
	ssize_t n = fd >= 0 ? read(fd, pid, sizeof(pid) - 1) : -1;
	if (fd >= 0)
		close(fd);
	if (n > 0) {
		pid_t owner = (pid_t)strtol(pid, NULL, 10);
		if (owner > 0 && kill(owner, 0) == 0)
			return -1;
	}
	unlink(lock_path);
	fd = open(lock_path, O_WRONLY | O_CREAT | O_EXCL, 384);
	if (fd < 0)
		return -1;
	char own_pid[32];
	n = snprintf(own_pid, sizeof(own_pid), "%ld\n", (long)getpid());
	(void)write(fd, own_pid, (size_t)n);
	close(fd);
	return 0;
}

static int open_control_socket(void) {
	const char* configured = getenv(MUSIC_SERVICE_SOCKET_ENV);
	if (!configured || !configured[0])
		configured = MUSIC_SERVICE_DEFAULT_SOCKET;
	if (strlen(configured) >= sizeof(socket_path))
		return -1;
	strcpy(socket_path, configured);
	snprintf(lock_path, sizeof(lock_path), "%s.lock", socket_path);
	char parent[sizeof(socket_path)];
	strcpy(parent, socket_path);
	char* slash = strrchr(parent, '/');
	if (slash) {
		*slash = '\0';
		if (parent[0])
			mkdir(parent, 493);
	}
	if (acquire_lock() != 0)
		return -1;
	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0)
		return -1;
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
	if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
		/* The lock is authoritative. A socket left by an unclean old owner is stale. */
		if (errno == EADDRINUSE) {
			int probe = socket(AF_UNIX, SOCK_STREAM, 0);
			int alive = probe >= 0 && connect(probe, (struct sockaddr*)&address, sizeof(address)) == 0;
			if (probe >= 0)
				close(probe);
			if (alive) {
				unlink(lock_path);
				return -1;
			}
			unlink(socket_path);
			if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
				unlink(lock_path);
				return -1;
			}
		} else {
			unlink(lock_path);
			return -1;
		}
	}
	if (listen(listen_fd, MAX_CLIENTS) != 0)
		return -1;
	fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK);
	return 0;
}

static void close_control_socket(void) {
	if (listen_fd >= 0)
		close(listen_fd);
	listen_fd = -1;
	if (socket_path[0])
		unlink(socket_path);
	if (lock_path[0])
		unlink(lock_path);
}

static void update_artwork(const PlayerSnapshot* snapshot) {
	if (!snapshot->current_file[0]) {
		artwork_source[0] = '\0';
		artwork_path[0] = '\0';
		return;
	}
	if (strcmp(artwork_source, snapshot->current_file) == 0)
		return;
	artwork_source[0] = '\0';
	artwork_path[0] = '\0';
	SDL_Surface* art = Player_getAlbumArt();
	if (!art)
		return;

	/* Artwork is transient owner output. Save beside the final path and rename
	 * only after SDL has finished writing, so a detached UI never reads a
	 * partially replaced bitmap. A generation-qualified path also prevents a
	 * UI cache from treating a new track's artwork as the old surface. */
	const char* artwork_dir = "/tmp/trimui_music";
	mkdir(artwork_dir, 493);
	unsigned int generation = ++artwork_generation;
	char temporary[MUSIC_SERVICE_MAX_PATH];
	char final_path[MUSIC_SERVICE_MAX_PATH];
	snprintf(temporary, sizeof(temporary), "%s/owner-art-%u.tmp", artwork_dir, generation);
	snprintf(final_path, sizeof(final_path), "%s/owner-art-%u.bmp", artwork_dir, generation);
	if (SDL_SaveBMP(art, temporary) == 0 && rename(temporary, final_path) == 0) {
		strncpy(artwork_path, final_path, sizeof(artwork_path) - 1);
		artwork_path[sizeof(artwork_path) - 1] = '\0';
		strncpy(artwork_source, snapshot->current_file, sizeof(artwork_source) - 1);
		artwork_source[sizeof(artwork_source) - 1] = '\0';
	} else {
		unlink(temporary);
	}
}

static void fill_snapshot(MusicResponseWire* response) {
	PlayerSnapshot snapshot;
	memset(&response->snapshot, 0, sizeof(response->snapshot));
	if (service_error[0])
		strncpy(response->error, service_error, sizeof(response->error) - 1);
	if (wake_failed)
		strncpy(response->error, "audio wake timed out", sizeof(response->error) - 1);
	response->snapshot.source = active_source;
	response->snapshot.loaded = active_source != MUSIC_SOURCE_NONE;
	if (!response->snapshot.loaded || Player_getSnapshot(&snapshot) != 0)
		return;
	update_artwork(&snapshot);
	response->snapshot.state = snapshot.state;
	response->snapshot.format = snapshot.format;
	response->snapshot.position_ms = snapshot.position_ms;
	response->snapshot.duration_ms = snapshot.track_info.duration_ms;
	response->snapshot.volume = snapshot.volume;
	response->snapshot.repeat = snapshot.repeat ? 1 : 0;
	response->snapshot.shuffle = shuffle_enabled ? 1 : 0;
	response->snapshot.playback_speed = snapshot.playback_speed;
	response->snapshot.source_sample_rate = snapshot.source_sample_rate;
	response->snapshot.output_sample_rate = snapshot.output_sample_rate;
	response->snapshot.audio_open = snapshot.audio_open ? 1 : 0;
	response->snapshot.stream_eof = snapshot.stream_eof ? 1 : 0;
	response->snapshot.source = active_source;
	response->snapshot.source_state = snapshot.state;
	response->snapshot.queue_count = Playlist_getCount(&queue);
	response->snapshot.queue_index = Playlist_getCurrentIndex(&queue);
	response->snapshot.queue_kind = queue_kind;
	response->snapshot.capabilities = 0;
	if (active_source == MUSIC_SOURCE_LOCAL || active_source == MUSIC_SOURCE_PODCAST)
		response->snapshot.capabilities = MUSIC_CAP_PLAY | MUSIC_CAP_PAUSE | MUSIC_CAP_SEEK;
	else if (active_source == MUSIC_SOURCE_RADIO)
		response->snapshot.capabilities = MUSIC_CAP_PLAY | MUSIC_CAP_PAUSE;
	if (snapshot.state == PLAYER_STATE_PLAYING) {
		/* Copy a bounded stereo window. The UI owns the presentation FFT; the
		 * service only exports decoder samples, never an engine or SDL pointer. */
		int16_t samples[MUSIC_VIS_SAMPLE_COUNT];
		int sample_count = Player_getVisBuffer(samples, MUSIC_VIS_SAMPLE_COUNT);
		if (sample_count > 0) {
			memcpy(response->snapshot.visualization_samples, samples, sizeof(samples));
			for (int i = 0; i < MUSIC_VIS_BARS; i++) {
				int start = i * sample_count / MUSIC_VIS_BARS;
				int end = (i + 1) * sample_count / MUSIC_VIS_BARS;
				int64_t total = 0;
				int count = 0;
				for (int j = start; j < end; j++) {
					total += abs(samples[j]);
					count++;
				}
				int level = count ? (int)(total / count) : 0;
				if (level > 32767)
					level = 32767;
				response->snapshot.visualization[i] = (uint16_t)(level * 2);
			}
		}
	}
	if (active_source == MUSIC_SOURCE_RADIO) {
		RadioStation* stations;
		if (Radio_getStations(&stations) > 1)
			response->snapshot.capabilities |= MUSIC_CAP_NEXT | MUSIC_CAP_PREVIOUS;
	} else if (active_source == MUSIC_SOURCE_LOCAL) {
		if (response->snapshot.queue_index + 1 < response->snapshot.queue_count)
			response->snapshot.capabilities |= MUSIC_CAP_NEXT;
		if (response->snapshot.queue_index > 0)
			response->snapshot.capabilities |= MUSIC_CAP_PREVIOUS;
	}
	strncpy(response->snapshot.artwork_path, artwork_path, sizeof(response->snapshot.artwork_path) - 1);
	memcpy(response->snapshot.current_file, snapshot.current_file, sizeof(response->snapshot.current_file));
	memcpy(response->snapshot.queue_path, queue_path, sizeof(response->snapshot.queue_path));
	memcpy(response->snapshot.title, snapshot.track_info.title, sizeof(response->snapshot.title));
	memcpy(response->snapshot.artist, snapshot.track_info.artist, sizeof(response->snapshot.artist));
	memcpy(response->snapshot.album, snapshot.track_info.album, sizeof(response->snapshot.album));
	if (active_source == MUSIC_SOURCE_RADIO) {
		const RadioMetadata* metadata = Radio_getMetadata();
		RadioState radio_state = Radio_getState();
		response->snapshot.source_state = radio_state;
		/* Radio owns transport while the generic Player is stopped. Publish a
		 * copied effective state so background policy and OSD controls agree with
		 * the stream instead of exposing Player's unrelated state. */
		if (radio_state == RADIO_STATE_PLAYING)
			response->snapshot.state = MUSIC_STATE_PLAYING;
		else if (radio_state == RADIO_STATE_CONNECTING || radio_state == RADIO_STATE_BUFFERING)
			response->snapshot.state = MUSIC_STATE_PAUSED;
		else if (radio_paused)
			response->snapshot.state = MUSIC_STATE_PAUSED;
		else
			response->snapshot.state = MUSIC_STATE_STOPPED;
		response->snapshot.current_file[0] = '\0';
		strncpy(response->snapshot.current_file, Radio_getCurrentUrl(), sizeof(response->snapshot.current_file) - 1);
		response->snapshot.duration_ms = -1;
		response->snapshot.radio_bitrate = metadata->bitrate;
		response->snapshot.radio_buffer_percent = (int)(Radio_getBufferLevel() * 100.0f);
		strncpy(response->snapshot.title, metadata->title, sizeof(response->snapshot.title) - 1);
		strncpy(response->snapshot.artist, metadata->artist, sizeof(response->snapshot.artist) - 1);
		strncpy(response->snapshot.album, metadata->station_name, sizeof(response->snapshot.album) - 1);
		if (Radio_getState() == RADIO_STATE_ERROR)
			strncpy(response->error, Radio_getError(), sizeof(response->error) - 1);
	} else if (active_source == MUSIC_SOURCE_PODCAST) {
		PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
		PodcastEpisode episode;
		response->snapshot.source_state = Podcast_isActive() ? snapshot.state : PODCAST_STATE_IDLE;
		response->snapshot.podcast_progress_sec = snapshot.position_ms / 1000;
		if (feed) {
			strncpy(response->snapshot.podcast_feed_url, feed->feed_url,
					sizeof(response->snapshot.podcast_feed_url) - 1);
			if (Podcast_getEpisode(podcast_feed_index, podcast_episode_index, &episode)) {
				strncpy(response->snapshot.podcast_episode_guid, episode.guid,
						sizeof(response->snapshot.podcast_episode_guid) - 1);
				strncpy(response->snapshot.title, episode.title, sizeof(response->snapshot.title) - 1);
				response->snapshot.duration_ms = Podcast_getDuration();
			}
		}
	}
}

static void response_error(MusicResponseWire* response, int status, const char* error) {
	response->status = status;
	if (error)
		strncpy(response->error, error, sizeof(response->error) - 1);
	fill_snapshot(response);
}

static void clear_queue_identity(void) {
	queue_kind = MUSIC_QUEUE_NONE;
	queue_path[0] = '\0';
}

static int save_podcast_progress(void) {
	if (active_source != MUSIC_SOURCE_PODCAST || podcast_feed_index < 0 || podcast_episode_index < 0)
		return 0;
	PlayerSnapshot snapshot;
	PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
	PodcastEpisode episode;
	if (!feed || !Podcast_getEpisode(podcast_feed_index, podcast_episode_index, &episode) ||
		Player_getSnapshot(&snapshot) != 0)
		return -1;
	int progress_sec = snapshot.position_ms / 1000;
	Podcast_setEpisodeProgress(podcast_feed_index, podcast_episode_index, progress_sec);
	Podcast_saveProgress(feed->feed_url, episode.guid, progress_sec);
	Podcast_flushProgress();
	return 0;
}

static void stop_active_source(void) {
	/* Persist intent before stopping the decoder or network source. */
	save_resume_state();
	if (active_source == MUSIC_SOURCE_PODCAST)
		save_podcast_progress();
	if (active_source == MUSIC_SOURCE_RADIO) {
		Radio_stop();
	} else if (active_source == MUSIC_SOURCE_PODCAST) {
		Podcast_stop();
		Podcast_flushProgress();
		podcast_feed_index = -1;
		podcast_episode_index = -1;
		podcast_waiting_for_seek = false;
	} else if (active_source == MUSIC_SOURCE_LOCAL) {
		Player_stop();
	}
	active_source = MUSIC_SOURCE_NONE;
}

static void clear_sleep_resume_intent(void) {
	wake_pending = false;
	sleep_resume_intent = false;
	sleep_audio_open = false;
	sleep_source = MUSIC_SOURCE_NONE;
	sleep_radio_url[0] = '\0';
	wake_deadline_ms = 0;
	wake_failed = false;
}

static int enter_sleep(void) {
	if (sleeping)
		return MUSIC_STATUS_OK;
	PlayerSnapshot snapshot;
	sleep_source = active_source;
	sleep_resume_intent = false;
	sleep_audio_open = false;
	sleep_radio_url[0] = '\0';
	if (Player_getSnapshot(&snapshot) == 0) {
		sleep_resume_intent = snapshot.state == PLAYER_STATE_PLAYING;
		sleep_audio_open = snapshot.audio_open;
	}
	if (active_source == MUSIC_SOURCE_RADIO) {
		RadioState radio_state = Radio_getState();
		sleep_resume_intent = radio_state == RADIO_STATE_CONNECTING ||
							  radio_state == RADIO_STATE_BUFFERING || radio_state == RADIO_STATE_PLAYING;
	}
	save_resume_state();
	if (active_source == MUSIC_SOURCE_PODCAST)
		save_podcast_progress();
	else if (active_source == MUSIC_SOURCE_RADIO)
		strncpy(sleep_radio_url, Radio_getCurrentUrl(), sizeof(sleep_radio_url) - 1);
	if (active_source == MUSIC_SOURCE_RADIO)
		Radio_stop();
	Player_closeAudioDevice();
	sleeping = true;
	wake_pending = false;
	wake_deadline_ms = 0;
	wake_failed = false;
	return MUSIC_STATUS_OK;
}

static int wake_from_sleep(void) {
	if (!sleeping)
		return MUSIC_STATUS_OK;
	bool needs_audio = sleep_audio_open || sleep_resume_intent;
	if (!needs_audio) {
		sleeping = false;
		wake_pending = false;
		wake_deadline_ms = 0;
		wake_failed = false;
		return MUSIC_STATUS_OK;
	}
	if (wake_deadline_ms == 0)
		wake_deadline_ms = now_ms() + WAKE_ROUTE_DEADLINE_MS;
	if (now_ms() >= wake_deadline_ms)
		return MUSIC_STATUS_UNAVAILABLE;
	if (access("/tmp/nx_audio_sink", R_OK) != 0)
		return MUSIC_STATUS_UNAVAILABLE;
	if (Player_openAudioDevice() != 0)
		return MUSIC_STATUS_UNAVAILABLE;
	if (sleep_source == MUSIC_SOURCE_RADIO && sleep_resume_intent && sleep_radio_url[0]) {
		if (Radio_play(sleep_radio_url) != 0)
			return MUSIC_STATUS_UNAVAILABLE;
	}
	sleeping = false;
	wake_pending = false;
	wake_deadline_ms = 0;
	wake_failed = false;
	return MUSIC_STATUS_OK;
}

static int load_radio(const char* url) {
	if (!url || !url[0] || strlen(url) >= MUSIC_SERVICE_MAX_PATH)
		return MUSIC_STATUS_BAD_REQUEST;
	stop_active_source();
	clear_queue_identity();
	if (Radio_play(url) != 0)
		return MUSIC_STATUS_UNAVAILABLE;
	radio_paused = false;
	active_source = MUSIC_SOURCE_RADIO;
	return MUSIC_STATUS_OK;
}

static bool podcast_identity_exists(const char* feed_url, const char* episode_guid) {
	int feed_index = Podcast_findFeedIndex(feed_url);
	PodcastFeed* feed = Podcast_getSubscription(feed_index);
	if (!feed)
		return false;
	for (int i = 0; i < feed->episode_count; i++) {
		PodcastEpisode episode;
		if (Podcast_getEpisode(feed_index, i, &episode) &&
			strcmp(episode.guid, episode_guid) == 0)
			return true;
	}
	return false;
}

static int load_podcast(const MusicPodcastLoadRequest* request, bool autoplay) {
	if (!request || !request->feed_url[0] || !request->episode_guid[0] ||
		strlen(request->feed_url) >= MUSIC_SERVICE_MAX_PATH || strlen(request->episode_guid) >= sizeof(request->episode_guid))
		return MUSIC_STATUS_BAD_REQUEST;
	stop_active_source();
	clear_queue_identity();
	/* The UI owns subscriptions/downloads. Refresh the playback catalog only
	 * at an explicit source load, while preserving the decoder identity above. */
	Podcast_reloadPlaybackData();
	int result = Podcast_loadDownloaded(request->feed_url, request->episode_guid);
	if (result < 0) {
		Podcast_stop();
		return MUSIC_STATUS_NOT_FOUND;
	}
	podcast_feed_index = Podcast_findFeedIndex(request->feed_url);
	podcast_episode_index = -1;
	PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
	PodcastEpisode episode;
	if (feed) {
		for (int i = 0; i < feed->episode_count; i++) {
			if (Podcast_getEpisode(podcast_feed_index, i, &episode) &&
				strcmp(episode.guid, request->episode_guid) == 0) {
				podcast_episode_index = i;
				break;
			}
		}
	}
	if (podcast_episode_index < 0) {
		Podcast_stop();
		return MUSIC_STATUS_INTERNAL;
	}
	active_source = MUSIC_SOURCE_PODCAST;
	podcast_waiting_for_seek = result == 1;
	podcast_last_progress_ms = now_ms();
	if (autoplay && !podcast_waiting_for_seek && Player_play() != 0) {
		stop_active_source();
		return MUSIC_STATUS_UNAVAILABLE;
	}
	return MUSIC_STATUS_OK;
}

static ResumeTransportState resume_transport(PlayerState state) {
	if (state == PLAYER_STATE_PLAYING)
		return RESUME_TRANSPORT_PLAYING;
	if (state == PLAYER_STATE_PAUSED)
		return RESUME_TRANSPORT_PAUSED;
	return RESUME_TRANSPORT_STOPPED;
}

static void save_resume_state(void) {
	PlayerSnapshot snapshot;
	if (active_source == MUSIC_SOURCE_LOCAL) {
		if (Player_getSnapshot(&snapshot) != 0 || !snapshot.current_file[0])
			return;
		char folder[sizeof(snapshot.current_file)];
		strncpy(folder, snapshot.current_file, sizeof(folder) - 1);
		folder[sizeof(folder) - 1] = '\0';
		char* slash = strrchr(folder, '/');
		if (slash)
			*slash = '\0';
		ResumeTransportState transport = resume_transport(snapshot.state);
		const char* name = snapshot.track_info.title[0] ? snapshot.track_info.title : snapshot.current_file;
		if (queue_kind == MUSIC_QUEUE_M3U)
			Resume_savePlaylist(queue_path, snapshot.current_file, name,
								Playlist_getCurrentIndex(&queue), snapshot.position_ms, transport,
								snapshot.repeat, shuffle_enabled);
		else if (queue_kind == MUSIC_QUEUE_FOLDER)
			Resume_saveFiles(queue_path, snapshot.current_file, name,
							 Playlist_getCurrentIndex(&queue), snapshot.position_ms, transport,
							 snapshot.repeat, shuffle_enabled);
		else
			Resume_saveFiles(folder, snapshot.current_file, name,
							 Playlist_getCurrentIndex(&queue), snapshot.position_ms, transport,
							 snapshot.repeat, shuffle_enabled);
	} else if (active_source == MUSIC_SOURCE_RADIO) {
		const char* url = Radio_getCurrentUrl();
		if (!url[0])
			return;
		RadioState radio_state = Radio_getState();
		ResumeTransportState transport = radio_paused ? RESUME_TRANSPORT_PAUSED
										 : (radio_state == RADIO_STATE_PLAYING ||
											radio_state == RADIO_STATE_CONNECTING ||
											radio_state == RADIO_STATE_BUFFERING)
											 ? RESUME_TRANSPORT_PLAYING
											 : RESUME_TRANSPORT_STOPPED;
		Resume_saveRadio(url, Radio_getMetadata()->station_name, transport,
						 false, shuffle_enabled);
	} else if (active_source == MUSIC_SOURCE_PODCAST && Player_getSnapshot(&snapshot) == 0) {
		PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
		PodcastEpisode episode;
		if (!feed || !Podcast_getEpisode(podcast_feed_index, podcast_episode_index, &episode))
			return;
		ResumeTransportState transport = podcast_waiting_for_seek
											 ? podcast_resume_transport
											 : resume_transport(snapshot.state);
		Resume_savePodcast(feed->feed_url, episode.guid,
						   snapshot.track_info.title[0] ? snapshot.track_info.title : episode.title,
						   snapshot.position_ms, transport, snapshot.repeat, shuffle_enabled);
	}
}

static void restore_failed(const char* message) {
	active_source = MUSIC_SOURCE_NONE;
	Playlist_free(&queue);
	Playlist_init(&queue);
	clear_queue_identity();
	Resume_clear();
	restore_pending = false;
	snprintf(service_error, sizeof(service_error), "%s", message);
}

static int restore_local(const ResumeState* saved) {
	if (saved->type == RESUME_TYPE_FILES) {
		if (!saved->folder_path[0] || access(saved->folder_path, R_OK) != 0)
			return MUSIC_STATUS_NOT_FOUND;
		Playlist_free(&queue);
		Playlist_init(&queue);
		if (Playlist_buildFromDirectory(&queue, saved->folder_path, NULL) <= 0)
			return MUSIC_STATUS_NOT_FOUND;
		queue_kind = MUSIC_QUEUE_FOLDER;
		strncpy(queue_path, saved->folder_path, sizeof(queue_path) - 1);
		queue_path[sizeof(queue_path) - 1] = '\0';
	} else if (saved->type == RESUME_TYPE_PLAYLIST) {
		if (!saved->playlist_path[0] || access(saved->playlist_path, R_OK) != 0)
			return MUSIC_STATUS_NOT_FOUND;
		Playlist_free(&queue);
		Playlist_init(&queue);
		int count = 0;
		if (!queue.tracks || M3U_loadTracks(saved->playlist_path, queue.tracks, PLAYLIST_MAX_TRACKS, &count) != 0 || count <= 0)
			return MUSIC_STATUS_NOT_FOUND;
		queue.track_count = count;
		queue_kind = MUSIC_QUEUE_M3U;
		strncpy(queue_path, saved->playlist_path, sizeof(queue_path) - 1);
		queue_path[sizeof(queue_path) - 1] = '\0';
	} else {
		return MUSIC_STATUS_NOT_FOUND;
	}

	int index = -1;
	for (int i = 0; i < queue.track_count; i++) {
		if (strcmp(queue.tracks[i].path, saved->track_path) == 0) {
			index = i;
			break;
		}
	}
	if (index < 0 || Player_load(saved->track_path) != 0)
		return MUSIC_STATUS_NOT_FOUND;
	queue.current_index = index;
	active_source = MUSIC_SOURCE_LOCAL;
	shuffle_enabled = saved->shuffle;
	Player_setRepeat(saved->repeat);
	if (saved->position_ms > 0)
		Player_seek(saved->position_ms);
	if (saved->transport == RESUME_TRANSPORT_PLAYING) {
		if (Player_play() != 0)
			return MUSIC_STATUS_UNAVAILABLE;
	} else if (saved->transport == RESUME_TRANSPORT_PAUSED) {
		/* Player_pause preserves the loaded decoder and position without
		 * opening the PCM just to establish paused transport state. */
		Player_pause();
	}
	return MUSIC_STATUS_OK;
}

static int restore_saved_playback(void) {
	const ResumeState* saved = Resume_getState();
	if (!saved)
		return MUSIC_STATUS_NOT_FOUND;
	if (saved->source == RESUME_SOURCE_LOCAL)
		return restore_local(saved);
	if (saved->source == RESUME_SOURCE_RADIO) {
		if (!saved->radio_url[0])
			return MUSIC_STATUS_NOT_FOUND;
		radio_paused = saved->transport == RESUME_TRANSPORT_PAUSED;
		if (saved->transport == RESUME_TRANSPORT_STOPPED || radio_paused) {
			if (Radio_prepare(saved->radio_url) != 0)
				return MUSIC_STATUS_NOT_FOUND;
		} else {
			if (Radio_play(saved->radio_url) != 0)
				return MUSIC_STATUS_UNAVAILABLE;
		}
		active_source = MUSIC_SOURCE_RADIO;
		return MUSIC_STATUS_OK;
	}
	if (saved->source == RESUME_SOURCE_PODCAST) {
		MusicPodcastLoadRequest request = {0};
		strncpy(request.feed_url, saved->podcast_feed_url, sizeof(request.feed_url) - 1);
		strncpy(request.episode_guid, saved->podcast_episode_guid, sizeof(request.episode_guid) - 1);
		podcast_resume_transport = saved->transport;
		int status = load_podcast(&request, false);
		if (status == MUSIC_STATUS_OK) {
			shuffle_enabled = saved->shuffle;
			Player_setRepeat(saved->repeat);
			/* Podcast_loadAndSeek uses catalog progress, which may lag a seek
			 * persisted by the owner. Queue the authoritative resume position
			 * before the decoder is allowed to advance or start playback. */
			Player_seek(saved->position_ms);
			/* Loading is deliberately stopped until the authoritative persisted
			 * position has been applied and the requested transport is known. */
			podcast_waiting_for_seek = true;
		}
		return status;
	}
	return MUSIC_STATUS_NOT_FOUND;
}

static void attempt_restore(void) {
	if (!restore_pending)
		return;
	int status = restore_saved_playback();
	if (status == MUSIC_STATUS_OK) {
		restore_pending = false;
		service_error[0] = '\0';
	} else if (status == MUSIC_STATUS_NOT_FOUND) {
		restore_failed("Saved playback source is unavailable");
	} else if (restore_deadline_ms > 0 && now_ms() >= restore_deadline_ms) {
		restore_failed("Audio route unavailable for saved playback");
	}
}

static int load_path(const char* path) {
	if (!path || !path[0] || strlen(path) >= MUSIC_SERVICE_MAX_PATH)
		return MUSIC_STATUS_BAD_REQUEST;
	struct stat st;
	if (stat(path, &st) != 0)
		return MUSIC_STATUS_NOT_FOUND;
	char file_path[MUSIC_SERVICE_MAX_PATH];
	if (S_ISDIR(st.st_mode)) {
		snprintf(file_path, sizeof(file_path), "%s", path);
		if (Playlist_buildFromDirectory(&queue, path, NULL) <= 0)
			return MUSIC_STATUS_NOT_FOUND;
		const PlaylistTrack* first = Playlist_getCurrentTrack(&queue);
		if (!first)
			return MUSIC_STATUS_NOT_FOUND;
		strncpy(file_path, first->path, sizeof(file_path) - 1);
	} else {
		strncpy(file_path, path, sizeof(file_path) - 1);
		char directory[MUSIC_SERVICE_MAX_PATH];
		strncpy(directory, path, sizeof(directory) - 1);
		directory[sizeof(directory) - 1] = '\0';
		char* slash = strrchr(directory, '/');
		if (slash)
			*slash = '\0';
		else
			strcpy(directory, ".");
		(void)Playlist_buildFromDirectory(&queue, directory, path);
	}
	file_path[sizeof(file_path) - 1] = '\0';
	stop_active_source();
	clear_queue_identity();
	if (S_ISDIR(st.st_mode)) {
		queue_kind = MUSIC_QUEUE_FOLDER;
		strncpy(queue_path, path, sizeof(queue_path) - 1);
		queue_path[sizeof(queue_path) - 1] = '\0';
	}
	if (Player_load(file_path) != 0)
		return MUSIC_STATUS_INTERNAL;
	active_source = MUSIC_SOURCE_LOCAL;
	eof_advanced = false;
	return MUSIC_STATUS_OK;
}

static int select_index(int index);

static int load_playlist(const MusicPlaylistLoadRequest* request) {
	if (!request || !request->path[0] || request->index < 0 ||
		strlen(request->path) >= MUSIC_SERVICE_MAX_PATH)
		return MUSIC_STATUS_BAD_REQUEST;
	PlaylistContext loaded = {0};
	Playlist_init(&loaded);
	int count = 0;
	if (loaded.tracks && M3U_loadTracks(request->path, loaded.tracks, PLAYLIST_MAX_TRACKS, &count) != 0)
		count = 0;
	if (count <= 0 || request->index >= count) {
		Playlist_free(&loaded);
		return MUSIC_STATUS_NOT_FOUND;
	}
	loaded.track_count = count;
	loaded.current_index = request->index;
	Playlist_free(&queue);
	queue = loaded;
	queue_kind = MUSIC_QUEUE_M3U;
	strncpy(queue_path, request->path, sizeof(queue_path) - 1);
	queue_path[sizeof(queue_path) - 1] = '\0';
	return select_index(request->index);
}

static int select_index(int index) {
	const PlaylistTrack* track = Playlist_getTrack(&queue, index);
	if (!track)
		return MUSIC_STATUS_NOT_FOUND;
	queue.current_index = index;
	stop_active_source();
	if (Player_load(track->path) != 0)
		return MUSIC_STATUS_INTERNAL;
	active_source = MUSIC_SOURCE_LOCAL;
	eof_advanced = false;
	local_last_resume_ms = now_ms();
	save_resume_state();
	return MUSIC_STATUS_OK;
}

static bool has_active_source(void) {
	return active_source != MUSIC_SOURCE_NONE;
}


static int advance(int direction) {
	PlayerSnapshot snapshot;
	Player_getSnapshot(&snapshot);
	int index = direction > 0 && shuffle_enabled
					? Playlist_shuffle(&queue)
					: (direction > 0 ? Playlist_next(&queue) : Playlist_prev(&queue));
	if (index < 0)
		return MUSIC_STATUS_NOT_FOUND;
	int status = select_index(index);
	if (status == MUSIC_STATUS_OK && snapshot.state == PLAYER_STATE_PLAYING && Player_play() != 0)
		return MUSIC_STATUS_UNAVAILABLE;
	return status;
}

static int toggle_active_source(void) {
	if (!has_active_source())
		return MUSIC_STATUS_NOT_FOUND;

	if (active_source == MUSIC_SOURCE_RADIO) {
		if (Radio_isActive()) {
			Radio_stop();
			radio_paused = true;
			return MUSIC_STATUS_OK;
		}
		return load_radio(Radio_getCurrentUrl());
	}
	if (active_source != MUSIC_SOURCE_LOCAL && active_source != MUSIC_SOURCE_PODCAST)
		return MUSIC_STATUS_NOT_FOUND;

	PlayerSnapshot snapshot;
	if (Player_getSnapshot(&snapshot) != 0)
		return MUSIC_STATUS_INTERNAL;
	if (!snapshot.current_file[0])
		return MUSIC_STATUS_NOT_FOUND;
	if (snapshot.state == PLAYER_STATE_PLAYING || snapshot.state == PLAYER_STATE_PAUSED) {
		Player_togglePause();
		return MUSIC_STATUS_OK;
	}
	if (snapshot.state == PLAYER_STATE_STOPPED)
		return Player_play() == 0 ? MUSIC_STATUS_OK : MUSIC_STATUS_UNAVAILABLE;
	return MUSIC_STATUS_INTERNAL;
}

static int handle_command(uint16_t command, const unsigned char* payload, size_t length,
						  MusicResponseWire* response) {
	memset(response, 0, sizeof(*response));
	int status = MUSIC_STATUS_OK;
	if (command != MUSIC_CMD_SNAPSHOT)
		restore_pending = false;
	switch (command) {
	case MUSIC_CMD_SNAPSHOT:
		break;
	case MUSIC_CMD_LOAD:
		if (length != sizeof(MusicLoadRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else
			status = load_path(((const MusicLoadRequest*)payload)->path);
		break;
	case MUSIC_CMD_LOAD_PLAYLIST:
		if (length != sizeof(MusicPlaylistLoadRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else
			status = load_playlist((const MusicPlaylistLoadRequest*)payload);
		break;
	case MUSIC_CMD_RADIO_LOAD:
		if (length != sizeof(MusicRadioLoadRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else
			status = load_radio(((const MusicRadioLoadRequest*)payload)->url);
		break;
	case MUSIC_CMD_PODCAST_LOAD:
		if (length != sizeof(MusicPodcastLoadRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else {
			podcast_resume_transport = RESUME_TRANSPORT_PLAYING;
			status = load_podcast((const MusicPodcastLoadRequest*)payload, true);
		}
		break;
	case MUSIC_CMD_SELECT:
	case MUSIC_CMD_SEEK:
	case MUSIC_CMD_REPEAT:
		if (length != sizeof(MusicIntRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else if (command != MUSIC_CMD_REPEAT && !has_active_source())
			status = MUSIC_STATUS_NOT_FOUND;
		else if (command == MUSIC_CMD_SELECT)
			status = select_index(((const MusicIntRequest*)payload)->value);
		else if (command == MUSIC_CMD_SEEK)
			Player_seek(((const MusicIntRequest*)payload)->value);
		else {
			bool repeat = ((const MusicIntRequest*)payload)->value != 0;
			Player_setRepeat(repeat);
		}
		break;
	case MUSIC_CMD_SET_SHUFFLE:
		if (length != sizeof(MusicIntRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else
			shuffle_enabled = ((const MusicIntRequest*)payload)->value != 0;
		break;
	case MUSIC_CMD_PODCAST_PROGRESS:
	case MUSIC_CMD_PODCAST_MARK_PLAYED:
		if (length != sizeof(MusicPodcastProgressRequest)) {
			status = MUSIC_STATUS_BAD_REQUEST;
		} else {
			const MusicPodcastProgressRequest* progress = (const MusicPodcastProgressRequest*)payload;
			Podcast_reloadPlaybackData();
			if (progress->position_sec < -1) {
				status = MUSIC_STATUS_BAD_REQUEST;
				break;
			}
			if (!podcast_identity_exists(progress->feed_url, progress->episode_guid)) {
				status = MUSIC_STATUS_NOT_FOUND;
				break;
			}
			if (progress->position_sec == -1) {
				Podcast_markAsPlayed(progress->feed_url, progress->episode_guid);
				Podcast_removeContinueListening(progress->feed_url, progress->episode_guid);
			} else {
				Podcast_saveProgress(progress->feed_url, progress->episode_guid, progress->position_sec);
				if (active_source == MUSIC_SOURCE_PODCAST)
					podcast_last_progress_ms = now_ms();
			}
			Podcast_flushProgress();
		}
		break;
	case MUSIC_CMD_SET_VOLUME:
		if (length != sizeof(MusicIntRequest) || ((const MusicIntRequest*)payload)->value < 0 ||
			((const MusicIntRequest*)payload)->value > 20)
			status = MUSIC_STATUS_BAD_REQUEST;
		else {
			int volume = ((const MusicIntRequest*)payload)->value;
			SetMusicVolume(volume);
			Player_setVolume(volume / 20.0f);
		}
		break;
	case MUSIC_CMD_SET_SPEED:
		if (length != sizeof(MusicSpeedRequest))
			status = MUSIC_STATUS_BAD_REQUEST;
		else if (((const MusicSpeedRequest*)payload)->speed < 0.5f ||
				 ((const MusicSpeedRequest*)payload)->speed > 2.0f)
			status = MUSIC_STATUS_BAD_REQUEST;
		else
			Player_setPlaybackSpeed(((const MusicSpeedRequest*)payload)->speed);
		break;
	case MUSIC_CMD_AUDIO_SETTINGS: {
		const MusicAudioSettingsRequest* settings = (const MusicAudioSettingsRequest*)payload;
		if (length != sizeof(*settings) || settings->bass_filter_hz < 0 ||
			settings->soft_limiter_threshold < 0.0f || settings->soft_limiter_threshold > 1.0f ||
			settings->resampler_quality < 0 || settings->resampler_quality > 2 ||
			(settings->buffer_frames != 1024 && settings->buffer_frames != 2048 &&
			 settings->buffer_frames != 4096) ||
			settings->rate_mode_follow < 0 ||
			settings->rate_mode_follow > 1)
			status = MUSIC_STATUS_BAD_REQUEST;
		else {
			Settings_setAudioValues(settings->bass_filter_hz, settings->soft_limiter_threshold,
									settings->rate_mode_follow, settings->resampler_quality, settings->buffer_frames);
			(void)Player_reopenAudioDevice();
		}
		break;
	}
	case MUSIC_CMD_SHUFFLE:
		if (length != 0)
			status = MUSIC_STATUS_BAD_REQUEST;
		else if (!has_active_source())
			status = MUSIC_STATUS_NOT_FOUND;
		else {
			int index = Playlist_shuffle(&queue);
			status = index < 0 ? MUSIC_STATUS_NOT_FOUND : select_index(index);
		}
		break;
	case MUSIC_CMD_PLAY:
		if (!has_active_source())
			status = MUSIC_STATUS_NOT_FOUND;
		else {
			if (active_source == MUSIC_SOURCE_RADIO)
				radio_paused = false;
			if (active_source == MUSIC_SOURCE_RADIO && !Radio_isActive())
				status = load_radio(Radio_getCurrentUrl());
			else if (active_source == MUSIC_SOURCE_PODCAST && podcast_waiting_for_seek)
				status = MUSIC_STATUS_OK;
			else if (Player_play() != 0)
				status = MUSIC_STATUS_UNAVAILABLE;
		}
		break;
	case MUSIC_CMD_PAUSE:
		if (!has_active_source())
			status = MUSIC_STATUS_NOT_FOUND;
		else if (active_source == MUSIC_SOURCE_RADIO) {
			radio_paused = true;
			Radio_stop();
		} else
			Player_pause();
		break;
	case MUSIC_CMD_STOP:
		stop_active_source();
		Resume_clear();
		clear_sleep_resume_intent();
		radio_paused = false;
		break;
	case MUSIC_CMD_TOGGLE:
		status = toggle_active_source();
		break;
	case MUSIC_CMD_NEXT:
		if (active_source == MUSIC_SOURCE_RADIO) {
			RadioStation* stations;
			int count = Radio_getStations(&stations);
			int index = Radio_findCurrentStationIndex();
			if (count <= 0 || index < 0)
				status = MUSIC_STATUS_NOT_FOUND;
			else
				status = load_radio(stations[(index + 1) % count].url);
		} else if (active_source == MUSIC_SOURCE_LOCAL) {
			status = advance(1);
		} else {
			status = MUSIC_STATUS_NOT_FOUND;
		}
		break;
	case MUSIC_CMD_PREVIOUS:
		if (active_source == MUSIC_SOURCE_RADIO) {
			RadioStation* stations;
			int count = Radio_getStations(&stations);
			int index = Radio_findCurrentStationIndex();
			if (count <= 0 || index < 0)
				status = MUSIC_STATUS_NOT_FOUND;
			else
				status = load_radio(stations[(index - 1 + count) % count].url);
		} else if (active_source == MUSIC_SOURCE_LOCAL) {
			status = advance(-1);
		} else {
			status = MUSIC_STATUS_NOT_FOUND;
		}
		break;
	case MUSIC_CMD_SLEEP:
		status = enter_sleep();
		break;
	case MUSIC_CMD_WAKE:
		/* An explicit wake starts a fresh bounded routing wait after a prior
		 * timeout, while repeated polling remains non-blocking. */
		wake_failed = false;
		wake_deadline_ms = sleeping ? now_ms() + WAKE_ROUTE_DEADLINE_MS : 0;
		status = wake_from_sleep();
		wake_pending = status != MUSIC_STATUS_OK && sleeping;
		if (wake_pending && wake_deadline_ms == 0)
			wake_deadline_ms = now_ms() + WAKE_ROUTE_DEADLINE_MS;
		break;
	case MUSIC_CMD_SHUTDOWN:
		stop_active_source();
		Player_closeAudioDevice();
		quit = 1;
		break;
	default:
		status = MUSIC_STATUS_BAD_REQUEST;
		break;
	}
	if (status == MUSIC_STATUS_OK && command != MUSIC_CMD_SNAPSHOT) {
		service_error[0] = '\0';
		if (command != MUSIC_CMD_STOP)
			save_resume_state();
	}
	response_error(response, status, status == MUSIC_STATUS_OK ? NULL : "command failed");
	return 0;
}

static void send_response(Client* client, uint16_t command, uint32_t request_id,
						  MusicResponseWire* response) {
	MusicFrameHeader header = {
		.magic = MUSIC_SERVICE_MAGIC, .version = MUSIC_SERVICE_PROTOCOL_VERSION, .command = command, .request_id = request_id, .payload_length = sizeof(*response)};
	unsigned char frame[sizeof(header) + sizeof(*response)];
	memcpy(frame, &header, sizeof(header));
	memcpy(frame + sizeof(header), response, sizeof(*response));
	size_t total = sizeof(frame), sent = 0;
	while (sent < total) {
		ssize_t n = send(client->fd, frame + sent, total - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (n <= 0) {
			close_client(client);
			return;
		}
		sent += (size_t)n;
	}
}

static void process_client(Client* client) {
	for (;;) {
		if (client->input_length < sizeof(MusicFrameHeader))
			return;
		MusicFrameHeader header;
		memcpy(&header, client->input, sizeof(header));
		if (header.magic != MUSIC_SERVICE_MAGIC || header.version != MUSIC_SERVICE_PROTOCOL_VERSION ||
			header.payload_length > MUSIC_SERVICE_MAX_FRAME - sizeof(MusicFrameHeader)) {
			close_client(client);
			return;
		}
		size_t frame_size = sizeof(header) + header.payload_length;
		if (client->input_length < frame_size)
			return;
		MusicResponseWire response;
		handle_command(header.command, client->input + sizeof(header), header.payload_length, &response);
		size_t remaining = client->input_length - frame_size;
		memmove(client->input, client->input + frame_size, remaining);
		client->input_length = remaining;
		client->frame_deadline_ms = remaining ? now_ms() + CLIENT_FRAME_TIMEOUT_MS : 0;
		send_response(client, header.command, header.request_id, &response);
		if (client->fd < 0)
			return;
	}
}

static void accept_clients(Client clients[MAX_CLIENTS]) {
	for (;;) {
		int fd = accept(listen_fd, NULL, NULL);
		if (fd < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			return;
		}
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		int slot = -1;
		for (int i = 0; i < MAX_CLIENTS; i++)
			if (clients[i].fd < 0) {
				slot = i;
				break;
			}
		if (slot < 0) {
			close(fd);
			continue;
		}
		clients[slot].fd = fd;
		clients[slot].input_length = 0;
		clients[slot].frame_deadline_ms = 0;
	}
}

static void service_tick(void) {
	attempt_restore();
	sync_music_volume();
	if (wake_pending) {
		int wake_status = wake_from_sleep();
		if (wake_status == MUSIC_STATUS_OK) {
			wake_pending = false;
		} else if (wake_deadline_ms > 0 && now_ms() >= wake_deadline_ms) {
			wake_pending = false;
			wake_failed = true;
		}
	}
	Radio_update();
	Podcast_update();
	if (!sleeping)
		Player_update();
	int64_t now = now_ms();

	if ((active_source == MUSIC_SOURCE_LOCAL || active_source == MUSIC_SOURCE_RADIO ||
		 active_source == MUSIC_SOURCE_PODCAST) &&
		now - local_last_resume_ms >= 5000) {
		save_resume_state();
		local_last_resume_ms = now;
	}
	if (active_source == MUSIC_SOURCE_PODCAST) {
		PlayerSnapshot snapshot;
		if (podcast_waiting_for_seek && !Player_resume()) {
			podcast_waiting_for_seek = false;
			if (podcast_resume_transport == RESUME_TRANSPORT_PLAYING) {
				if (Player_play() != 0)
					stop_active_source();
			} else if (podcast_resume_transport == RESUME_TRANSPORT_PAUSED) {
				/* A completed seek leaves the decoder stopped. Establish paused
				 * state directly; opening then closing the PCM is unnecessary. */
				Player_pause();
			}
		}
		if (active_source != MUSIC_SOURCE_PODCAST || Player_getSnapshot(&snapshot) != 0)
			return;
		if (snapshot.stream_eof && snapshot.state == PLAYER_STATE_STOPPED) {
			PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
			PodcastEpisode episode;
			if (feed && Podcast_getEpisode(podcast_feed_index, podcast_episode_index, &episode)) {
				Podcast_setEpisodeProgress(podcast_feed_index, podcast_episode_index, -1);
				Podcast_markAsPlayed(feed->feed_url, episode.guid);
				Podcast_flushProgress();
			}
			stop_active_source();
			return;
		}
		if (now - podcast_last_progress_ms >= 30000 && snapshot.position_ms > 0) {
			PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
			PodcastEpisode episode;
			if (feed && Podcast_getEpisode(podcast_feed_index, podcast_episode_index, &episode)) {
				int progress_sec = snapshot.position_ms / 1000;
				Podcast_setEpisodeProgress(podcast_feed_index, podcast_episode_index, progress_sec);
				Podcast_saveProgress(feed->feed_url, episode.guid, progress_sec);
				Podcast_flushProgress();
			}
			podcast_last_progress_ms = now;
		}
	}
}

static void poll_hid(void) {
	USBHIDEvent event;
	while ((event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
		MusicResponseWire response;
		if (event == USB_HID_EVENT_PLAY_PAUSE)
			(void)handle_command(MUSIC_CMD_TOGGLE, NULL, 0, &response);
		else if (event == USB_HID_EVENT_NEXT_TRACK)
			(void)handle_command(MUSIC_CMD_NEXT, NULL, 0, &response);
		else if (event == USB_HID_EVENT_PREV_TRACK)
			(void)handle_command(MUSIC_CMD_PREVIOUS, NULL, 0, &response);
	}
}

int main(void) {
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);
	if (open_control_socket() != 0)
		return EXIT_FAILURE;
	Playlist_init(&queue);
	Resume_init();
	InitSettings();
	Settings_setOwnerMode(true);
	Settings_init();
	int radio_ready = Radio_init() == 0;
	int podcast_ready = radio_ready && Podcast_initPlayback() == 0;
	int player_ready = podcast_ready && Player_coreInit() == 0;
	if (player_ready)
		sync_music_volume();
	if (!player_ready) {
		if (podcast_ready)
			Podcast_cleanupPlayback();
		if (radio_ready)
			Radio_quit();
		Settings_quit();
		QuitSettings();
		Playlist_free(&queue);
		close_control_socket();
		return EXIT_FAILURE;
	}
	restore_pending = Resume_isAvailable();
	restore_deadline_ms = restore_pending ? now_ms() + 5000 : 0;

	Client clients[MAX_CLIENTS];
	for (int i = 0; i < MAX_CLIENTS; i++)
		clients[i].fd = -1;
	while (!quit) {
		struct pollfd pfds[1 + MAX_CLIENTS];
		int map[1 + MAX_CLIENTS];
		int count = 1;
		pfds[0] = (struct pollfd){.fd = listen_fd, .events = POLLIN};
		map[0] = -1;
		for (int i = 0; i < MAX_CLIENTS; i++)
			if (clients[i].fd >= 0) {
				pfds[count] = (struct pollfd){.fd = clients[i].fd, .events = POLLIN};
				map[count++] = i;
			}
		(void)poll(pfds, count, 10);
		attempt_restore();
		if (pfds[0].revents & POLLIN)
			accept_clients(clients);
		int64_t now = now_ms();
		for (int p = 1; p < count; p++) {
			Client* client = &clients[map[p]];
			if (client->fd < 0)
				continue;
			if (pfds[p].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				close_client(client);
				continue;
			}
			if (pfds[p].revents & POLLIN) {
				unsigned char buf[512];
				ssize_t n = recv(client->fd, buf, sizeof(buf), MSG_DONTWAIT);
				if (n <= 0) {
					close_client(client);
					continue;
				}
				if (client->input_length + (size_t)n > MUSIC_SERVICE_MAX_FRAME) {
					close_client(client);
					continue;
				}
				if (client->input_length == 0)
					client->frame_deadline_ms = now + CLIENT_FRAME_TIMEOUT_MS;
				memcpy(client->input + client->input_length, buf, (size_t)n);
				client->input_length += (size_t)n;
				process_client(client);
			}
			if (client->fd >= 0 && client->input_length && client->frame_deadline_ms > 0 &&
				now >= client->frame_deadline_ms)
				close_client(client);
		}
		poll_hid();
		service_tick();
		PlayerSnapshot snapshot;
		if (active_source == MUSIC_SOURCE_LOCAL && !eof_advanced &&
			Player_getSnapshot(&snapshot) == 0 && snapshot.stream_eof &&
			snapshot.state == PLAYER_STATE_STOPPED) {
			/* Player handles repeat at the decoder boundary. Otherwise the owner,
			 * not the detached UI, advances its queue (including shuffle). */
			if (advance(1) == MUSIC_STATUS_OK) {
				(void)Player_play();
			} else {
				/* A naturally exhausted queue is not a boot-resume request. */
				stop_active_source();
				Resume_clear();
				clear_queue_identity();
			}
			eof_advanced = true;
		}
		if (Player_getSnapshot(&snapshot) == 0 && !snapshot.stream_eof)
			eof_advanced = false;
	}
	if (active_source == MUSIC_SOURCE_PODCAST)
		save_podcast_progress();
	save_resume_state();
	stop_active_source();
	Radio_quit();
	Podcast_cleanupPlayback();
	Player_quit();
	Settings_quit();
	QuitSettings();
	Playlist_free(&queue);
	close_control_socket();
	return EXIT_SUCCESS;
}
