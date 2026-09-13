// Host-compiled unit test for music_request_validation.c (no device toolchain).
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I. music_request_validation.c tests/test_music_request_validation.c -o /tmp/test_music_request_validation && /tmp/test_music_request_validation
#include "../music_request_validation.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

int main(void) {
	MusicLoadRequest load;
	MusicRadioLoadRequest radio;
	MusicPlaylistLoadRequest playlist;
	MusicPodcastLoadRequest podcast;
	MusicPodcastProgressRequest progress;
	memset(&load, 'x', sizeof(load));
	memset(&radio, 'x', sizeof(radio));
	memset(&playlist, 'x', sizeof(playlist));
	memset(&podcast, 'x', sizeof(podcast));
	memset(&progress, 'x', sizeof(progress));

	check(!MusicRequest_isValidPayload(MUSIC_CMD_LOAD, &load, sizeof(load)),
		  "load rejects unterminated fixed strings");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_RADIO_LOAD, &radio, sizeof(radio)),
		  "radio rejects unterminated URL");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_LOAD_PLAYLIST, &playlist, sizeof(playlist)),
		  "playlist rejects unterminated path");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_PODCAST_LOAD, &podcast, sizeof(podcast)),
		  "podcast rejects unterminated identity");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_PODCAST_PROGRESS, &progress, sizeof(progress)),
		  "podcast progress rejects unterminated identity");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_PODCAST_MARK_PLAYED, &progress, sizeof(progress)),
		  "podcast mark-played rejects unterminated identity");

	memset(&load, 'x', sizeof(load));
	load.path[sizeof(load.path) - 1] = '\0';
	load.selected_path[sizeof(load.selected_path) - 1] = '\0';
	check(MusicRequest_isValidPayload(MUSIC_CMD_LOAD, &load, sizeof(load)),
		  "load accepts terminated fixed strings");
	check(MusicRequest_isValidPayload(MUSIC_CMD_SNAPSHOT, NULL, 0),
		  "commands without payload pass validation");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_SNAPSHOT, &load, 1),
		  "payload-free commands reject data");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_SET_VOLUME, &load, sizeof(load)),
		  "fixed-size commands reject the wrong payload size");
	check(!MusicRequest_isValidPayload(UINT16_MAX, NULL, 0),
		  "unknown commands fail closed");
	check(!MusicRequest_isValidPayload(MUSIC_CMD_LOAD, &load, sizeof(load) - 1),
		  "wrong-sized load fails validation");

	return failures ? 1 : 0;
}
