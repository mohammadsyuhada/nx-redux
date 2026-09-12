#include "music_service_protocol.h"
#include "music_request_validation.h"
#include "music_service_server.h"
#include "player.h"
#include "playlist.h"
#include "playlist_m3u.h"
#include "podcast.h"
#include "radio.h"
#include "settings.h"
#include "resume.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_surface.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void InitSettings(void);
void QuitSettings(void);
int GetMusicVolume(void);
void SetMusicVolume(int volume);

#define WAKE_ROUTE_DEADLINE_MS 5000

static volatile sig_atomic_t quit;
static PlaylistContext queue;
static bool eof_advanced;
static bool shuffle_enabled;
#define SHUFFLE_HISTORY_MAX 32
static int shuffle_history[SHUFFLE_HISTORY_MAX];
static int shuffle_history_count;
static MusicSource active_source;
static bool has_active_source(void);
static int queue_kind;
static char queue_path[MUSIC_SERVICE_MAX_PATH];
static int podcast_feed_index = -1;
static int podcast_episode_index = -1;
static char podcast_feed_url[MUSIC_SERVICE_MAX_PATH];
static char podcast_episode_guid[128];
static bool podcast_waiting_for_seek;
static int64_t podcast_last_progress_ms;
static int64_t local_last_resume_ms;
static char artwork_source[MUSIC_SERVICE_MAX_PATH];
static char artwork_path[MUSIC_SERVICE_MAX_PATH];
static char retired_artwork_path[MUSIC_SERVICE_MAX_PATH];
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

#define OWNED_ARTWORK_DIR "/tmp/trimui_music"

static void cleanup_owned_artwork(void) {
	if (artwork_path[0])
		unlink(artwork_path);
	if (retired_artwork_path[0] && strcmp(retired_artwork_path, artwork_path) != 0)
		unlink(retired_artwork_path);
}

static void retire_artwork(void) {
	if (!artwork_path[0])
		return;
	if (retired_artwork_path[0])
		unlink(retired_artwork_path);
	snprintf(retired_artwork_path, sizeof(retired_artwork_path), "%s", artwork_path);
	artwork_path[0] = '\0';
}

static void update_artwork(const PlayerSnapshot* snapshot) {
	char source[sizeof(artwork_source)];
	SDL_Surface* art;
	if (active_source == MUSIC_SOURCE_RADIO) {
		const RadioMetadata* metadata = Radio_getMetadata();
		unsigned int revision = 0;
		art = Radio_getArtwork(metadata, &revision);
		snprintf(source, sizeof(source), "%s\x1f%s\x1f%s\x1f%u", Radio_getCurrentUrl(),
				 metadata->artist, metadata->title, revision);
	} else {
		if (!snapshot->current_file[0]) {
			retire_artwork();
			artwork_source[0] = '\0';
			return;
		}
		snprintf(source, sizeof(source), "%s", snapshot->current_file);
		art = Player_getAlbumArt();
	}
	if (strcmp(artwork_source, source) == 0)
		return;
	if (!art) {
		retire_artwork();
		return;
	}

	mkdir(OWNED_ARTWORK_DIR, 493);
	unsigned int generation = ++artwork_generation;
	char temporary[MUSIC_SERVICE_MAX_PATH];
	char final_path[MUSIC_SERVICE_MAX_PATH];
	snprintf(temporary, sizeof(temporary), "%s/owner-art-%ld-%u.tmp", OWNED_ARTWORK_DIR,
			 (long)getpid(), generation);
	snprintf(final_path, sizeof(final_path), "%s/owner-art-%ld-%u.bmp", OWNED_ARTWORK_DIR,
			 (long)getpid(), generation);
	if (SDL_SaveBMP(art, temporary) == 0 && rename(temporary, final_path) == 0) {
		retire_artwork();
		snprintf(artwork_path, sizeof(artwork_path), "%s", final_path);
		snprintf(artwork_source, sizeof(artwork_source), "%s", source);
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
		int16_t samples[MUSIC_VIS_SAMPLE_COUNT] = {0};
		int sample_count = Player_getVisBuffer(samples, MUSIC_VIS_SAMPLE_COUNT);
		if (sample_count > 0)
			memcpy(response->snapshot.visualization_samples, samples,
				   (size_t)sample_count * sizeof(samples[0]));
	}
	if (active_source == MUSIC_SOURCE_RADIO) {
		RadioStation* stations;
		if (Radio_getStations(&stations) > 1)
			response->snapshot.capabilities |= MUSIC_CAP_NEXT | MUSIC_CAP_PREVIOUS;
	} else if (active_source == MUSIC_SOURCE_LOCAL) {
		if ((shuffle_enabled && response->snapshot.queue_count > 1) ||
			response->snapshot.queue_index + 1 < response->snapshot.queue_count)
			response->snapshot.capabilities |= MUSIC_CAP_NEXT;
		if ((shuffle_enabled && shuffle_history_count > 0) || response->snapshot.queue_index > 0)
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
	if (active_source != MUSIC_SOURCE_PODCAST || !podcast_feed_url[0] || !podcast_episode_guid[0])
		return 0;
	PlayerSnapshot snapshot;
	if (Player_getSnapshot(&snapshot) != 0)
		return -1;
	Podcast_saveProgress(podcast_feed_url, podcast_episode_guid, snapshot.position_ms / 1000);
	Podcast_flushProgress();
	return 0;
}

static void complete_active_podcast(void) {
	char feed_url[MUSIC_SERVICE_MAX_PATH];
	char episode_guid[sizeof(podcast_episode_guid)];
	snprintf(feed_url, sizeof(feed_url), "%s", podcast_feed_url);
	snprintf(episode_guid, sizeof(episode_guid), "%s", podcast_episode_guid);
	Podcast_stop();
	if (feed_url[0] && episode_guid[0]) {
		Podcast_markAsPlayed(feed_url, episode_guid);
		Podcast_removeContinueListening(feed_url, episode_guid);
		Podcast_flushProgress();
	}
	podcast_feed_index = -1;
	podcast_episode_index = -1;
	podcast_feed_url[0] = '\0';
	podcast_episode_guid[0] = '\0';
	podcast_waiting_for_seek = false;
	active_source = MUSIC_SOURCE_NONE;
	Resume_clear();
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
		podcast_feed_url[0] = '\0';
		podcast_episode_guid[0] = '\0';
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
	if (active_source == MUSIC_SOURCE_PODCAST) {
		save_podcast_progress();
		if (podcast_waiting_for_seek)
			podcast_resume_transport = RESUME_TRANSPORT_STOPPED;
	} else if (active_source == MUSIC_SOURCE_RADIO)
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

static void resolve_active_podcast(void) {
	podcast_feed_index = Podcast_findFeedIndex(podcast_feed_url);
	podcast_episode_index = -1;
	PodcastFeed* feed = Podcast_getSubscription(podcast_feed_index);
	if (!feed)
		return;
	for (int i = 0; i < feed->episode_count; i++) {
		PodcastEpisode episode;
		if (Podcast_getEpisode(podcast_feed_index, i, &episode) &&
			strcmp(episode.guid, podcast_episode_guid) == 0) {
			podcast_episode_index = i;
			return;
		}
	}
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
	snprintf(podcast_feed_url, sizeof(podcast_feed_url), "%s", request->feed_url);
	snprintf(podcast_episode_guid, sizeof(podcast_episode_guid), "%s", request->episode_guid);
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
		podcast_feed_url[0] = '\0';
		podcast_episode_guid[0] = '\0';
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

static int restore_local(const ResumeState* saved, ResumeTransportState transport) {
	shuffle_history_count = 0;
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
	if (transport == RESUME_TRANSPORT_PAUSED) {
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
	/* A fresh boot never starts playback by itself: a record written while
	 * playing (including after a crash or power cut) lands paused, position
	 * kept, so the first sound is one the user asked for. Wake from sleep
	 * uses enter_sleep/wake_from_sleep, not this path. */
	const ResumeTransportState transport = saved->transport == RESUME_TRANSPORT_PLAYING
											   ? RESUME_TRANSPORT_PAUSED
											   : saved->transport;
	if (saved->source == RESUME_SOURCE_LOCAL)
		return restore_local(saved, transport);
	if (saved->source == RESUME_SOURCE_RADIO) {
		if (!saved->radio_url[0])
			return MUSIC_STATUS_NOT_FOUND;
		radio_paused = transport == RESUME_TRANSPORT_PAUSED;
		if (Radio_prepare(saved->radio_url) != 0)
			return MUSIC_STATUS_NOT_FOUND;
		active_source = MUSIC_SOURCE_RADIO;
		return MUSIC_STATUS_OK;
	}
	if (saved->source == RESUME_SOURCE_PODCAST) {
		MusicPodcastLoadRequest request = {0};
		strncpy(request.feed_url, saved->podcast_feed_url, sizeof(request.feed_url) - 1);
		strncpy(request.episode_guid, saved->podcast_episode_guid, sizeof(request.episode_guid) - 1);
		podcast_resume_transport = transport;
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

static int load_path(const MusicLoadRequest* request) {
	if (!request || !request->path[0] || strlen(request->path) >= MUSIC_SERVICE_MAX_PATH ||
		strlen(request->selected_path) >= MUSIC_SERVICE_MAX_PATH)
		return MUSIC_STATUS_BAD_REQUEST;
	const char* path = request->path;
	struct stat st;
	if (stat(path, &st) != 0)
		return MUSIC_STATUS_NOT_FOUND;
	PlaylistContext loaded = {0};
	Playlist_init(&loaded);
	if (!loaded.tracks)
		return MUSIC_STATUS_INTERNAL;
	char file_path[MUSIC_SERVICE_MAX_PATH];
	if (S_ISDIR(st.st_mode)) {
		if (Playlist_buildFromDirectory(&loaded, path, request->selected_path[0] ? request->selected_path : NULL) <= 0) {
			Playlist_free(&loaded);
			return MUSIC_STATUS_NOT_FOUND;
		}
		const PlaylistTrack* first = Playlist_getCurrentTrack(&loaded);
		if (!first || (request->selected_path[0] && strcmp(first->path, request->selected_path) != 0)) {
			Playlist_free(&loaded);
			return MUSIC_STATUS_NOT_FOUND;
		}
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
		(void)Playlist_buildFromDirectory(&loaded, directory, path);
	}
	file_path[sizeof(file_path) - 1] = '\0';
	if (Player_validate(file_path) != 0) {
		Playlist_free(&loaded);
		return MUSIC_STATUS_INTERNAL;
	}
	stop_active_source();
	if (Player_load(file_path) != 0) {
		Playlist_free(&loaded);
		return MUSIC_STATUS_INTERNAL;
	}
	Playlist_free(&queue);
	queue = loaded;
	clear_queue_identity();
	if (S_ISDIR(st.st_mode)) {
		queue_kind = MUSIC_QUEUE_FOLDER;
		strncpy(queue_path, path, sizeof(queue_path) - 1);
		queue_path[sizeof(queue_path) - 1] = '\0';
	}
	shuffle_history_count = 0;
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
	if (Player_validate(loaded.tracks[request->index].path) != 0) {
		Playlist_free(&loaded);
		return MUSIC_STATUS_INTERNAL;
	}
	stop_active_source();
	if (Player_load(loaded.tracks[request->index].path) != 0) {
		Playlist_free(&loaded);
		return MUSIC_STATUS_INTERNAL;
	}
	Playlist_free(&queue);
	queue = loaded;
	queue_kind = MUSIC_QUEUE_M3U;
	strncpy(queue_path, request->path, sizeof(queue_path) - 1);
	queue_path[sizeof(queue_path) - 1] = '\0';
	shuffle_history_count = 0;
	active_source = MUSIC_SOURCE_LOCAL;
	eof_advanced = false;
	local_last_resume_ms = now_ms();
	save_resume_state();
	return MUSIC_STATUS_OK;
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
	int index;
	if (shuffle_enabled && direction > 0) {
		int previous = queue.current_index;
		index = Playlist_shuffle(&queue);
		if (index >= 0 && index != previous) {
			if (shuffle_history_count == SHUFFLE_HISTORY_MAX) {
				memmove(shuffle_history, shuffle_history + 1,
						sizeof(shuffle_history[0]) * (SHUFFLE_HISTORY_MAX - 1));
				shuffle_history_count--;
			}
			shuffle_history[shuffle_history_count++] = previous;
		}
	} else if (shuffle_enabled && direction < 0 && shuffle_history_count) {
		index = shuffle_history[--shuffle_history_count];
		queue.current_index = index;
	} else {
		index = direction > 0 ? Playlist_next(&queue) : Playlist_prev(&queue);
	}
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

static void handle_command(uint16_t command, const unsigned char* payload, size_t length,
						   MusicResponseWire* response) {
	memset(response, 0, sizeof(*response));
	if (command != MUSIC_CMD_SNAPSHOT)
		restore_pending = false;
	if (!MusicRequest_isValidPayload(command, payload, length)) {
		response_error(response, MUSIC_STATUS_BAD_REQUEST, "command failed");
		return;
	}
	int status = MUSIC_STATUS_OK;
	switch (command) {
	case MUSIC_CMD_SNAPSHOT:
		break;
	case MUSIC_CMD_LOAD:
		status = load_path((const MusicLoadRequest*)payload);
		break;
	case MUSIC_CMD_LOAD_PLAYLIST:
		status = load_playlist((const MusicPlaylistLoadRequest*)payload);
		break;
	case MUSIC_CMD_RADIO_LOAD:
		status = load_radio(((const MusicRadioLoadRequest*)payload)->url);
		break;
	case MUSIC_CMD_PODCAST_LOAD:
		podcast_resume_transport = RESUME_TRANSPORT_PLAYING;
		status = load_podcast((const MusicPodcastLoadRequest*)payload, true);
		break;
	case MUSIC_CMD_SELECT:
	case MUSIC_CMD_SEEK:
	case MUSIC_CMD_REPEAT:
		if (command != MUSIC_CMD_REPEAT && !has_active_source())
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
		shuffle_enabled = ((const MusicIntRequest*)payload)->value != 0;
		if (!shuffle_enabled)
			shuffle_history_count = 0;
		break;
	case MUSIC_CMD_PODCAST_PROGRESS:
	case MUSIC_CMD_PODCAST_MARK_PLAYED: {
		const MusicPodcastProgressRequest* progress = (const MusicPodcastProgressRequest*)payload;
		Podcast_reloadPlaybackData();
		if (active_source == MUSIC_SOURCE_PODCAST)
			resolve_active_podcast();
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
	} break;
	case MUSIC_CMD_SET_VOLUME:
		if (((const MusicIntRequest*)payload)->value < 0 ||
			((const MusicIntRequest*)payload)->value > 20)
			status = MUSIC_STATUS_BAD_REQUEST;
		else {
			int volume = ((const MusicIntRequest*)payload)->value;
			SetMusicVolume(volume);
			sync_music_volume();
		}
		break;
	case MUSIC_CMD_SET_SPEED:
		if (((const MusicSpeedRequest*)payload)->speed < 0.5f ||
			((const MusicSpeedRequest*)payload)->speed > 2.0f)
			status = MUSIC_STATUS_BAD_REQUEST;
		else
			Player_setPlaybackSpeed(((const MusicSpeedRequest*)payload)->speed);
		break;
	case MUSIC_CMD_AUDIO_SETTINGS: {
		const MusicAudioSettingsRequest* settings = (const MusicAudioSettingsRequest*)payload;
		if (settings->bass_filter_hz < 0 ||
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
		if (!has_active_source())
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
			if (active_source == MUSIC_SOURCE_PODCAST && podcast_waiting_for_seek)
				podcast_resume_transport = RESUME_TRANSPORT_PLAYING;
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
		} else {
			if (active_source == MUSIC_SOURCE_PODCAST && podcast_waiting_for_seek)
				podcast_resume_transport = RESUME_TRANSPORT_PAUSED;
			Player_pause();
		}
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
	if (active_source == MUSIC_SOURCE_RADIO && Radio_getState() == RADIO_STATE_ERROR) {
		PlayerSnapshot snapshot;
		if (Player_getSnapshot(&snapshot) == 0 && snapshot.audio_open)
			Player_closeAudioDevice();
	}
	Podcast_update();
	if (!sleeping)
		Player_update();
	int64_t now = now_ms();

	if ((active_source == MUSIC_SOURCE_LOCAL || active_source == MUSIC_SOURCE_RADIO ||
		 active_source == MUSIC_SOURCE_PODCAST) &&
		now - local_last_resume_ms >= MUSIC_RESUME_SAVE_INTERVAL_MS) {
		save_resume_state();
		local_last_resume_ms = now;
	}
	if (!sleeping && active_source == MUSIC_SOURCE_PODCAST) {
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
			complete_active_podcast();
			return;
		}
		if (now - podcast_last_progress_ms >= 30000 && snapshot.position_ms > 0) {
			if (podcast_feed_url[0] && podcast_episode_guid[0]) {
				Podcast_saveProgress(podcast_feed_url, podcast_episode_guid, snapshot.position_ms / 1000);
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
	if (MusicServiceServer_open() != 0)
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
		MusicServiceServer_close();
		return EXIT_FAILURE;
	}
	restore_pending = Resume_isAvailable();
	restore_deadline_ms = restore_pending ? now_ms() + 5000 : 0;

	while (!quit) {
		attempt_restore();
		MusicServiceServer_poll(handle_command, 10);
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
	cleanup_owned_artwork();
	MusicServiceServer_close();
	return EXIT_SUCCESS;
}
