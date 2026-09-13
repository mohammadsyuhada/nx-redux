// Host-compiled unit test for music_balance.c (no device toolchain).
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I. music_balance.c tests/test_music_balance.c -o /tmp/test_music_balance && /tmp/test_music_balance
#include "../music_balance.h"
#include "../music_service_protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int game_volume;
static int music_volume;
static int client_connected;
static int client_status;
static int client_volume;
static char order[16];
static int order_length;

int GetGameVolume(void) {
	return game_volume;
}
int GetMusicVolume(void) {
	return music_volume;
}
void SetGameVolume(int value) {
	game_volume = value;
	order[order_length++] = 'G';
}
void SetMusicVolume(int value) {
	music_volume = value;
	order[order_length++] = 'M';
}
bool MusicClient_isConnected(void) {
	return client_connected;
}
int MusicClient_setVolume(int value) {
	client_volume = value;
	order[order_length++] = 'C';
	return client_status;
}

static void check(int condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

int main(void) {
	game_volume = music_volume = 20;
	client_connected = 0;
	client_status = MUSIC_STATUS_OK;
	order_length = 0;
	check(MusicBalance_setValue(3) == MUSIC_STATUS_OK && music_volume == 12 && game_volume == 20 &&
			  order_length == 2 && order[0] == 'M' && order[1] == 'G',
		  "balance persists both gains without a daemon");

	client_connected = 1;
	game_volume = music_volume = 20;
	order_length = 0;
	check(MusicBalance_setValue(7) == MUSIC_STATUS_OK && music_volume == 20 && game_volume == 12 &&
			  client_volume == 20 && order_length == 2 && order[0] == 'G' && order[1] == 'C',
		  "connected balance leaves music persistence to daemon command");

	game_volume = music_volume = 20;
	client_status = MUSIC_STATUS_UNAVAILABLE;
	order_length = 0;
	check(MusicBalance_setValue(0) == MUSIC_STATUS_UNAVAILABLE && music_volume == 20 && game_volume == 20 &&
			  order_length == 4 && order[0] == 'G' && order[1] == 'C' && order[2] == 'G' && order[3] == 'M',
		  "daemon command failure restores both persisted gains");

	// Round trip across the whole slider (disconnected: setValue persists both
	// gains directly, getValue reconstructs the slider position from them).
	// Catches a broken mapping in either direction.
	client_connected = 0;
	client_status = MUSIC_STATUS_OK;
	for (int v = 0; v <= 10; v++) {
		game_volume = music_volume = 20;
		order_length = 0;
		check(MusicBalance_setValue(v) == MUSIC_STATUS_OK && MusicBalance_getValue() == v,
			  "balance value survives a set/get round trip");
	}

	// Direction: below centre favours the game, above centre favours music.
	// A sign inversion in the mapping flips these.
	game_volume = music_volume = 20;
	order_length = 0;
	MusicBalance_setValue(2);
	check(music_volume < game_volume, "below centre keeps music quieter than the game");
	game_volume = music_volume = 20;
	order_length = 0;
	MusicBalance_setValue(8);
	check(music_volume > game_volume, "above centre keeps music louder than the game");

	// Clamp: out-of-range values saturate to the endpoints. If the clamp is
	// removed these read back off the ends.
	game_volume = music_volume = 20;
	order_length = 0;
	MusicBalance_setValue(-3);
	check(MusicBalance_getValue() == 0 && game_volume == 20 && music_volume == 0,
		  "below-range balance clamps to the game endpoint");
	game_volume = music_volume = 20;
	order_length = 0;
	MusicBalance_setValue(13);
	check(MusicBalance_getValue() == 10 && game_volume == 0 && music_volume == 20,
		  "above-range balance clamps to the music endpoint");

	check(strcmp(MusicBalance_formatValue(5), "50/50") == 0, "formatValue centre reads 50/50");
	check(strcmp(MusicBalance_formatValue(8), "Music +3") == 0, "formatValue above centre reads Music +3");
	return failures ? 1 : 0;
}
