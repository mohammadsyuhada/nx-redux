#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vp_defines.h"
#include "resume.h"

#define RESUME_DIR APP_DATA_DIR
#define RESUME_FILE APP_DATA_DIR "/resume.cfg"

// In-memory state
static ResumeState state = {.type = RESUME_TYPE_NONE};
static char label_buf[300];

// Write state to disk
static void save_to_disk(void) {
	mkdir(RESUME_DIR, 0755);

	FILE* f = fopen(RESUME_FILE, "w");
	if (!f)
		return;

	fprintf(f, "type=%d\n", (int)state.type);
	fprintf(f, "video_path=%s\n", state.video_path);
	fprintf(f, "video_name=%s\n", state.video_name);
	fprintf(f, "subtitle_path=%s\n", state.subtitle_path);
	fprintf(f, "position_sec=%d\n", state.position_sec);
	fclose(f);
}

void Resume_init(void) {
	memset(&state, 0, sizeof(state));
	state.type = RESUME_TYPE_NONE;

	FILE* f = fopen(RESUME_FILE, "r");
	if (!f)
		return;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';

		int ival;
		if (sscanf(line, "type=%d", &ival) == 1) {
			if (ival >= RESUME_TYPE_NONE && ival <= RESUME_TYPE_IPTV)
				state.type = (ResumeType)ival;
		} else if (strncmp(line, "video_path=", 11) == 0) {
			snprintf(state.video_path, sizeof(state.video_path), "%s", line + 11);
		} else if (strncmp(line, "video_name=", 11) == 0) {
			snprintf(state.video_name, sizeof(state.video_name), "%s", line + 11);
		} else if (strncmp(line, "subtitle_path=", 14) == 0) {
			snprintf(state.subtitle_path, sizeof(state.subtitle_path), "%s", line + 14);
		} else if (sscanf(line, "position_sec=%d", &ival) == 1) {
			state.position_sec = ival;
		}
	}
	fclose(f);

	// Validate: must have a video path
	if (state.type != RESUME_TYPE_NONE && state.video_path[0] == '\0') {
		state.type = RESUME_TYPE_NONE;
	}
}

bool Resume_isAvailable(void) {
	return state.type != RESUME_TYPE_NONE;
}

const ResumeState* Resume_getState(void) {
	if (state.type == RESUME_TYPE_NONE)
		return NULL;
	return &state;
}

const char* Resume_getLabel(void) {
	if (state.type == RESUME_TYPE_NONE)
		return NULL;

	if (state.video_name[0]) {
		snprintf(label_buf, sizeof(label_buf), "Resume: %s", state.video_name);
	} else {
		// Fallback: extract filename from path
		const char* slash = strrchr(state.video_path, '/');
		const char* name = slash ? slash + 1 : state.video_path;
		snprintf(label_buf, sizeof(label_buf), "Resume: %s", name);
	}
	return label_buf;
}

void Resume_saveLocal(const char* video_path, const char* video_name,
					  const char* subtitle_path, int position_sec) {
	state.type = RESUME_TYPE_LOCAL;
	snprintf(state.video_path, sizeof(state.video_path), "%s", video_path ? video_path : "");
	snprintf(state.video_name, sizeof(state.video_name), "%s", video_name ? video_name : "");
	snprintf(state.subtitle_path, sizeof(state.subtitle_path), "%s", subtitle_path ? subtitle_path : "");
	state.position_sec = position_sec;
	save_to_disk();
}

void Resume_saveIPTV(const char* url, const char* channel_name) {
	state.type = RESUME_TYPE_IPTV;
	snprintf(state.video_path, sizeof(state.video_path), "%s", url ? url : "");
	snprintf(state.video_name, sizeof(state.video_name), "%s", channel_name ? channel_name : "");
	state.subtitle_path[0] = '\0';
	state.position_sec = 0; // IPTV is live, no position
	save_to_disk();
}

void Resume_clear(void) {
	memset(&state, 0, sizeof(state));
	state.type = RESUME_TYPE_NONE;
	remove(RESUME_FILE);
}
