#include "../music_balance.h"
#include "../music_service_protocol.h"
#include <stdbool.h>
#include <stdio.h>

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
	return failures ? 1 : 0;
}
