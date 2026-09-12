#include "music_request_validation.h"
#include <string.h>

static bool bounded_string(const char* value, size_t size) {
	return memchr(value, '\0', size) != NULL;
}

bool MusicRequest_isValidPayload(uint16_t command, const void* payload, size_t length) {
	switch (command) {
	case MUSIC_CMD_LOAD: {
		if (!payload || length != sizeof(MusicLoadRequest))
			return false;
		const MusicLoadRequest* request = payload;
		return bounded_string(request->path, sizeof(request->path)) &&
			bounded_string(request->selected_path, sizeof(request->selected_path));
	}
	case MUSIC_CMD_RADIO_LOAD: {
		if (!payload || length != sizeof(MusicRadioLoadRequest))
			return false;
		const MusicRadioLoadRequest* request = payload;
		return bounded_string(request->url, sizeof(request->url));
	}
	case MUSIC_CMD_LOAD_PLAYLIST: {
		if (!payload || length != sizeof(MusicPlaylistLoadRequest))
			return false;
		const MusicPlaylistLoadRequest* request = payload;
		return bounded_string(request->path, sizeof(request->path));
	}
	case MUSIC_CMD_PODCAST_LOAD: {
		if (!payload || length != sizeof(MusicPodcastLoadRequest))
			return false;
		const MusicPodcastLoadRequest* request = payload;
		return bounded_string(request->feed_url, sizeof(request->feed_url)) &&
			bounded_string(request->episode_guid, sizeof(request->episode_guid));
	}
	case MUSIC_CMD_PODCAST_PROGRESS:
	case MUSIC_CMD_PODCAST_MARK_PLAYED: {
		if (!payload || length != sizeof(MusicPodcastProgressRequest))
			return false;
		const MusicPodcastProgressRequest* request = payload;
		return bounded_string(request->feed_url, sizeof(request->feed_url)) &&
			bounded_string(request->episode_guid, sizeof(request->episode_guid));
	}
	case MUSIC_CMD_SELECT:
	case MUSIC_CMD_SEEK:
	case MUSIC_CMD_REPEAT:
	case MUSIC_CMD_SET_SHUFFLE:
	case MUSIC_CMD_SET_VOLUME:
		return payload && length == sizeof(MusicIntRequest);
	case MUSIC_CMD_SET_SPEED:
		return payload && length == sizeof(MusicSpeedRequest);
	case MUSIC_CMD_AUDIO_SETTINGS:
		return payload && length == sizeof(MusicAudioSettingsRequest);
	case MUSIC_CMD_SNAPSHOT:
	case MUSIC_CMD_PLAY:
	case MUSIC_CMD_PAUSE:
	case MUSIC_CMD_STOP:
	case MUSIC_CMD_TOGGLE:
	case MUSIC_CMD_NEXT:
	case MUSIC_CMD_PREVIOUS:
	case MUSIC_CMD_SHUFFLE:
	case MUSIC_CMD_SHUTDOWN:
	case MUSIC_CMD_SLEEP:
	case MUSIC_CMD_WAKE:
		return length == 0;
	default:
		return false;
	}
}
