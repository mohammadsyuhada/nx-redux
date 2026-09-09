#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../resume.h"

static int failures;

static void check(bool condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static void check_state(ResumeTransportState transport, int position, bool repeat, bool shuffle) {
	const ResumeState* state = Resume_getState();
	check(state != NULL, "saved state is available after reload");
	if (!state)
		return;
	check(state->source == RESUME_SOURCE_LOCAL && state->type == RESUME_TYPE_FILES,
		  "local source identity persists");
	check(state->transport == transport, "transport intent persists");
	check(strcmp(state->folder_path, "/tmp/music-folder") == 0 &&
			  strcmp(state->track_path, "/tmp/music-folder/song.ogg") == 0 &&
			  state->track_index == 2,
		  "folder and track identity persist");
	check(state->position_ms == position && state->repeat == repeat && state->shuffle == shuffle,
		  "position and playback options persist");
}

int main(void) {
	const char* root = "/tmp/nx_resume_persistence_test";
	char resume_file[256];
	char resume_temp[256];
	snprintf(resume_file, sizeof(resume_file), "%s/resume.cfg", root);
	snprintf(resume_temp, sizeof(resume_temp), "%s/resume.cfg.tmp", root);
	mkdir(root, 493);
	unlink(resume_file);
	unlink(resume_temp);
	setenv("NX_MUSIC_RESUME_DIR", root, 1);

	Resume_init();
	check(!Resume_isAvailable(), "empty persistence starts unavailable");

	Resume_saveFiles("/tmp/music-folder", "/tmp/music-folder/song.ogg", "song", 2, 1234,
					 RESUME_TRANSPORT_PLAYING, true, false);
	Resume_init();
	check_state(RESUME_TRANSPORT_PLAYING, 1234, true, false);

	Resume_saveFiles("/tmp/music-folder", "/tmp/music-folder/song.ogg", "song", 2, 2345,
					 RESUME_TRANSPORT_PAUSED, false, true);
	Resume_init();
	check_state(RESUME_TRANSPORT_PAUSED, 2345, false, true);

	Resume_saveFiles("/tmp/music-folder", "/tmp/music-folder/song.ogg", "song", 2, 0,
					 RESUME_TRANSPORT_STOPPED, false, false);
	Resume_init();
	check_state(RESUME_TRANSPORT_STOPPED, 0, false, false);

	Resume_saveRadio("http://example.test/radio", "Example Radio", RESUME_TRANSPORT_PAUSED,
					 false, true);
	Resume_init();
	const ResumeState* radio = Resume_getState();
	check(radio && radio->source == RESUME_SOURCE_RADIO &&
			  radio->transport == RESUME_TRANSPORT_PAUSED &&
			  strcmp(radio->radio_url, "http://example.test/radio") == 0 && radio->shuffle,
		  "radio identity and paused transport persist");

	Resume_savePodcast("https://example.test/feed", "episode-1", "Episode", 4567,
					   RESUME_TRANSPORT_STOPPED, true, false);
	Resume_init();
	const ResumeState* podcast = Resume_getState();
	check(podcast && podcast->source == RESUME_SOURCE_PODCAST &&
			  podcast->transport == RESUME_TRANSPORT_STOPPED &&
			  strcmp(podcast->podcast_feed_url, "https://example.test/feed") == 0 &&
			  strcmp(podcast->podcast_episode_guid, "episode-1") == 0 &&
			  podcast->position_ms == 4567,
		  "podcast identity, position, and stopped transport persist");

	Resume_clear();
	Resume_init();
	check(!Resume_isAvailable() && access(resume_file, F_OK) != 0,
		  "clearing intent survives reload");
	unsetenv("NX_MUSIC_RESUME_DIR");
	rmdir(root);
	if (!failures)
		puts("resume persistence test passed");
	return failures ? 1 : 0;
}
