#include "music_balance.h"
#include "music_client.h"
#if defined(PLATFORM_TG5050)
#include "../../tg5050/libmsettings/msettings.h"
#else
#include "../../tg5040/libmsettings/msettings.h"
#endif
#include <stdio.h>

#define MUSIC_BALANCE_MAX 10
#define MUSIC_BALANCE_CENTER 5
#define MUSIC_BALANCE_SIDE_STEP 4
#define MUSIC_GAIN_MAX 20

static int clamp_gain(int gain) {
	if (gain < 0)
		return 0;
	if (gain > MUSIC_GAIN_MAX)
		return MUSIC_GAIN_MAX;
	return gain;
}

int MusicBalance_getValue(void) {
	int game = clamp_gain(GetGameVolume());
	int music = clamp_gain(GetMusicVolume());
	int stronger = game > music ? game : music;
	if (stronger == 0)
		return MUSIC_BALANCE_CENTER;

	int offset = (music - game) * MUSIC_BALANCE_CENTER;
	if (offset >= 0)
		offset = (offset + stronger / 2) / stronger;
	else
		offset = (offset - stronger / 2) / stronger;
	int value = MUSIC_BALANCE_CENTER + offset;
	if (value < 0)
		return 0;
	if (value > MUSIC_BALANCE_MAX)
		return MUSIC_BALANCE_MAX;
	return value;
}

int MusicBalance_setValue(int value) {
	if (value < 0)
		value = 0;
	if (value > MUSIC_BALANCE_MAX)
		value = MUSIC_BALANCE_MAX;

	int music = value <= MUSIC_BALANCE_CENTER ? value * MUSIC_BALANCE_SIDE_STEP : MUSIC_GAIN_MAX;
	int game = value <= MUSIC_BALANCE_CENTER ? MUSIC_GAIN_MAX : (MUSIC_BALANCE_MAX - value) * MUSIC_BALANCE_SIDE_STEP;
	if (!MusicClient_isConnected()) {
		SetMusicVolume(music);
		SetGameVolume(game);
		return MUSIC_STATUS_OK;
	}
	int old_music = GetMusicVolume();
	int old_game = GetGameVolume();
	SetGameVolume(game);
	int status = MusicClient_setVolume(music);
	if (status != MUSIC_STATUS_OK) {
		SetGameVolume(old_game);
		SetMusicVolume(old_music);
	}
	return status;
}

const char* MusicBalance_formatValue(int value) {
	static char display[24];
	int offset = value - MUSIC_BALANCE_CENTER;
	if (offset == 0)
		snprintf(display, sizeof(display), "50/50");
	else
		snprintf(display, sizeof(display), "%s +%d", offset < 0 ? "Game" : "Music", offset < 0 ? -offset : offset);
	return display;
}

const char* MusicBalance_getDisplayString(void) {
	return MusicBalance_formatValue(MusicBalance_getValue());
}
