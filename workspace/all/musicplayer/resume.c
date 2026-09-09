#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(PLATFORM_TG5050)
#include "../../tg5050/platform/platform.h"
#else
#include "../../tg5040/platform/platform.h"
#endif
#include "resume.h"

#define RESUME_DIR SDCARD_PATH "/.userdata/shared/music-player"

static const char* resume_directory(void) {
	const char* override = getenv("NX_MUSIC_RESUME_DIR");
	return override && override[0] ? override : RESUME_DIR;
}

static ResumeState state = {.type = RESUME_TYPE_NONE};
static char label_buf[300];

static void save_to_disk(void) {
	const char* directory = resume_directory();
	char resume_file[512];
	char resume_temp_file[512];
	snprintf(resume_file, sizeof(resume_file), "%s/resume.cfg", directory);
	snprintf(resume_temp_file, sizeof(resume_temp_file), "%s/resume.cfg.tmp", directory);
	if (mkdir(directory, 493) != 0 && access(directory, F_OK) != 0)
		return;

	FILE* f = fopen(resume_temp_file, "w");
	if (!f)
		return;

	fprintf(f, "type=%d\n", (int)state.type);
	fprintf(f, "source=%d\n", (int)state.source);
	fprintf(f, "transport=%d\n", (int)state.transport);
	fprintf(f, "folder_path=%s\n", state.folder_path);
	fprintf(f, "playlist_path=%s\n", state.playlist_path);
	fprintf(f, "track_path=%s\n", state.track_path);
	fprintf(f, "track_name=%s\n", state.track_name);
	fprintf(f, "radio_url=%s\n", state.radio_url);
	fprintf(f, "podcast_feed_url=%s\n", state.podcast_feed_url);
	fprintf(f, "podcast_episode_guid=%s\n", state.podcast_episode_guid);
	fprintf(f, "track_index=%d\n", state.track_index);
	fprintf(f, "position_ms=%d\n", state.position_ms);
	fprintf(f, "repeat=%d\n", state.repeat ? 1 : 0);
	fprintf(f, "shuffle=%d\n", state.shuffle ? 1 : 0);
	bool write_ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
	if (fclose(f) != 0)
		write_ok = false;
	if (!write_ok) {
		unlink(resume_temp_file);
		return;
	}
	if (rename(resume_temp_file, resume_file) != 0)
		unlink(resume_temp_file);
}

void Resume_init(void) {
	memset(&state, 0, sizeof(state));
	state.type = RESUME_TYPE_NONE;
	state.source = RESUME_SOURCE_LOCAL;
	state.transport = RESUME_TRANSPORT_STOPPED;

	char resume_file[512];
	snprintf(resume_file, sizeof(resume_file), "%s/resume.cfg", resume_directory());
	FILE* f = fopen(resume_file, "r");
	if (!f)
		return;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		int ival;
		if (sscanf(line, "type=%d", &ival) == 1) {
			if (ival >= RESUME_TYPE_NONE && ival <= RESUME_TYPE_PLAYLIST)
				state.type = (ResumeType)ival;
		} else if (sscanf(line, "source=%d", &ival) == 1) {
			if (ival >= RESUME_SOURCE_LOCAL && ival <= RESUME_SOURCE_PODCAST)
				state.source = (ResumeSource)ival;
		} else if (sscanf(line, "transport=%d", &ival) == 1) {
			if (ival >= RESUME_TRANSPORT_STOPPED && ival <= RESUME_TRANSPORT_PAUSED)
				state.transport = (ResumeTransportState)ival;
		} else if (strncmp(line, "folder_path=", 12) == 0) {
			snprintf(state.folder_path, sizeof(state.folder_path), "%s", line + 12);
		} else if (strncmp(line, "playlist_path=", 14) == 0) {
			snprintf(state.playlist_path, sizeof(state.playlist_path), "%s", line + 14);
		} else if (strncmp(line, "track_path=", 11) == 0) {
			snprintf(state.track_path, sizeof(state.track_path), "%s", line + 11);
		} else if (strncmp(line, "track_name=", 11) == 0) {
			snprintf(state.track_name, sizeof(state.track_name), "%s", line + 11);
		} else if (strncmp(line, "radio_url=", 10) == 0) {
			snprintf(state.radio_url, sizeof(state.radio_url), "%s", line + 10);
		} else if (strncmp(line, "podcast_feed_url=", 17) == 0) {
			snprintf(state.podcast_feed_url, sizeof(state.podcast_feed_url), "%s", line + 17);
		} else if (strncmp(line, "podcast_episode_guid=", 21) == 0) {
			snprintf(state.podcast_episode_guid, sizeof(state.podcast_episode_guid), "%s", line + 21);
		} else if (sscanf(line, "track_index=%d", &ival) == 1) {
			state.track_index = ival;
		} else if (sscanf(line, "position_ms=%d", &ival) == 1) {
			state.position_ms = ival;
		} else if (sscanf(line, "repeat=%d", &ival) == 1) {
			state.repeat = ival != 0;
		} else if (sscanf(line, "shuffle=%d", &ival) == 1) {
			state.shuffle = ival != 0;
		}
	}
	fclose(f);

	/* Files written by older versions represented local playback implicitly. */
	if (state.source == RESUME_SOURCE_LOCAL && state.type != RESUME_TYPE_NONE &&
		state.track_path[0] == '\0')
		state.type = RESUME_TYPE_NONE;
	if (state.source == RESUME_SOURCE_RADIO && state.radio_url[0] == '\0')
		state.type = RESUME_TYPE_NONE;
	if (state.source == RESUME_SOURCE_PODCAST &&
		(!state.podcast_feed_url[0] || !state.podcast_episode_guid[0]))
		state.type = RESUME_TYPE_NONE;
}

bool Resume_isAvailable(void) {
	return state.type != RESUME_TYPE_NONE;
}

const ResumeState* Resume_getState(void) {
	return state.type == RESUME_TYPE_NONE ? NULL : &state;
}

const char* Resume_getLabel(void) {
	if (state.type == RESUME_TYPE_NONE)
		return NULL;
	if (state.source == RESUME_SOURCE_RADIO)
		snprintf(label_buf, sizeof(label_buf), "Resume: Radio");
	else if (state.source == RESUME_SOURCE_PODCAST)
		snprintf(label_buf, sizeof(label_buf), "Resume: %s",
				 state.track_name[0] ? state.track_name : state.podcast_episode_guid);
	else if (state.track_name[0])
		snprintf(label_buf, sizeof(label_buf), "Resume: %s", state.track_name);
	else {
		const char* slash = strrchr(state.track_path, '/');
		snprintf(label_buf, sizeof(label_buf), "Resume: %s", slash ? slash + 1 : state.track_path);
	}
	return label_buf;
}

static void save_local(ResumeType type, const char* folder_or_playlist, const char* track_path,
					   const char* track_name, int track_index, int position_ms,
					   ResumeTransportState transport, bool repeat, bool shuffle) {
	memset(&state, 0, sizeof(state));
	state.type = type;
	state.source = RESUME_SOURCE_LOCAL;
	state.transport = transport;
	if (type == RESUME_TYPE_FILES)
		snprintf(state.folder_path, sizeof(state.folder_path), "%s", folder_or_playlist ? folder_or_playlist : "");
	else
		snprintf(state.playlist_path, sizeof(state.playlist_path), "%s", folder_or_playlist ? folder_or_playlist : "");
	snprintf(state.track_path, sizeof(state.track_path), "%s", track_path ? track_path : "");
	snprintf(state.track_name, sizeof(state.track_name), "%s", track_name ? track_name : "");
	state.track_index = track_index;
	state.position_ms = position_ms;
	state.repeat = repeat;
	state.shuffle = shuffle;
	save_to_disk();
}

void Resume_saveFiles(const char* folder_path, const char* track_path, const char* track_name,
					  int track_index, int position_ms, ResumeTransportState transport,
					  bool repeat, bool shuffle) {
	save_local(RESUME_TYPE_FILES, folder_path, track_path, track_name, track_index,
			   position_ms, transport, repeat, shuffle);
}

void Resume_savePlaylist(const char* playlist_path, const char* track_path, const char* track_name,
						 int track_index, int position_ms, ResumeTransportState transport,
						 bool repeat, bool shuffle) {
	save_local(RESUME_TYPE_PLAYLIST, playlist_path, track_path, track_name, track_index,
			   position_ms, transport, repeat, shuffle);
}

void Resume_saveRadio(const char* url, const char* name, ResumeTransportState transport,
					  bool repeat, bool shuffle) {
	memset(&state, 0, sizeof(state));
	state.type = RESUME_TYPE_FILES;
	state.source = RESUME_SOURCE_RADIO;
	state.transport = transport;
	snprintf(state.radio_url, sizeof(state.radio_url), "%s", url ? url : "");
	snprintf(state.track_name, sizeof(state.track_name), "%s", name ? name : "");
	state.repeat = repeat;
	state.shuffle = shuffle;
	save_to_disk();
}

void Resume_savePodcast(const char* feed_url, const char* episode_guid, const char* name,
						int position_ms, ResumeTransportState transport, bool repeat, bool shuffle) {
	memset(&state, 0, sizeof(state));
	state.type = RESUME_TYPE_FILES;
	state.source = RESUME_SOURCE_PODCAST;
	state.transport = transport;
	snprintf(state.podcast_feed_url, sizeof(state.podcast_feed_url), "%s", feed_url ? feed_url : "");
	snprintf(state.podcast_episode_guid, sizeof(state.podcast_episode_guid), "%s", episode_guid ? episode_guid : "");
	snprintf(state.track_name, sizeof(state.track_name), "%s", name ? name : "");
	state.position_ms = position_ms;
	state.repeat = repeat;
	state.shuffle = shuffle;
	save_to_disk();
}

void Resume_updatePosition(int position_ms) {
	if (state.type == RESUME_TYPE_NONE)
		return;
	state.position_ms = position_ms;
	save_to_disk();
}

void Resume_clear(void) {
	memset(&state, 0, sizeof(state));
	state.type = RESUME_TYPE_NONE;
	char resume_file[512];
	char resume_temp_file[512];
	snprintf(resume_file, sizeof(resume_file), "%s/resume.cfg", resume_directory());
	snprintf(resume_temp_file, sizeof(resume_temp_file), "%s/resume.cfg.tmp", resume_directory());
	unlink(resume_file);
	unlink(resume_temp_file);
}
