#include "../../nextui/romkey.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROOT "/mnt/SDCARD/Roms"

static bool source(const char* rom, const char* want_dir, const char* want_key) {
	char dir[512], key[256];
	bool folder = RomKey_aliasSource(rom, ROOT, dir, sizeof(dir), key, sizeof(key));
	assert(strcmp(dir, want_dir) == 0);
	assert(strcmp(key, want_key) == 0);
	return folder;
}

static void romkey_plain_roms(void) {
	assert(!source(ROOT "/Game Boy (GB)/Tetris.gb", ROOT "/Game Boy (GB)", "Tetris.gb"));
	// a loose cue (not named after its folder) is a plain ROM of that folder
	assert(!source(ROOT "/Sony PlayStation (PS)/Crash.cue", ROOT "/Sony PlayStation (PS)", "Crash.cue"));
	assert(!source(ROOT "/Sony PlayStation (PS)/Game/Other.cue", ROOT "/Sony PlayStation (PS)/Game", "Other.cue"));
	// a disc beside the folder's cue is not the game file
	assert(!source(ROOT "/Sony PlayStation (PS)/Game/Game (Disc 1).bin", ROOT "/Sony PlayStation (PS)/Game", "Game (Disc 1).bin"));
	// a console folder is a library: its own playlist is not a folder game
	assert(!source(ROOT "/Sega Dreamcast (DC)/Sega Dreamcast (DC).m3u", ROOT "/Sega Dreamcast (DC)", "Sega Dreamcast (DC).m3u"));
	// outside Roms (a Tools pak line in a collection) the folder rule does not apply
	assert(!source("/mnt/SDCARD/Tools/X/X.cue", "/mnt/SDCARD/Tools/X", "X.cue"));
	// a folder game file must match the folder exactly ("Game.cue" in "Game2", case)
	assert(!source(ROOT "/PS/Game2/Game.cue", ROOT "/PS/Game2", "Game.cue"));
	assert(!source(ROOT "/PS/Game/game.cue", ROOT "/PS/Game", "game.cue"));
	// a folder game's own directory row: keyed by its name in the console folder
	assert(!source(ROOT "/PS/Zz Test Disc", ROOT "/PS", "Zz Test Disc"));
}

static void romkey_folder_games(void) {
	assert(source(ROOT "/Sony PlayStation (PS)/Zz Test Disc/Zz Test Disc.cue", ROOT "/Sony PlayStation (PS)", "Zz Test Disc"));
	assert(source(ROOT "/PS/FF7/FF7.m3u", ROOT "/PS", "FF7"));
	// a dot in the folder name stays in the key
	assert(source(ROOT "/PS/Vol. 2/Vol. 2.cue", ROOT "/PS", "Vol. 2"));
	// nested in a console's subfolder: that subfolder's map.txt lists it
	assert(source(ROOT "/PS/RPG/FF7/FF7.cue", ROOT "/PS/RPG", "FF7"));
}

static void romkey_art_reserved(void) {
	assert(RomKey_artReserved(ROOT "/GB/.media/bg.png"));
	assert(RomKey_artReserved(ROOT "/GB/.media/bglist.png"));
	assert(!RomKey_artReserved(ROOT "/GB/.media/bg2.png"));
	assert(!RomKey_artReserved(ROOT "/GB/.media/screenshot/bg.png")); // the game "bg.gb"'s screenshot
	assert(!RomKey_artReserved(ROOT "/GB/.media/Tetris.png"));
	assert(!RomKey_artReserved("bg.png"));
}

int main(void) {
	romkey_plain_roms();
	romkey_folder_games();
	romkey_art_reserved();
	printf("test_romkey: all passed\n");
	return 0;
}
