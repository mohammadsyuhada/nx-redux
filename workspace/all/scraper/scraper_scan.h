// scraper_scan.h - game discovery for the Artwork Manager.
//
// Walks a Roms system folder the way nextui's game list does, instead of
// matching a fixed extension list: every non-hidden regular file is a game,
// a directory holding a folder-named .cue/.m3u is ONE game (multi-disc
// folder game), and any other directory is recursed into. Pure POSIX so the
// host test in common/tests can compile it standalone.
#ifndef SCRAPER_SCAN_H
#define SCRAPER_SCAN_H

#include <stdbool.h>
#include <stddef.h>

#define SCAN_PATH_MAX 512
#define SCAN_NAME_MAX 256
#define SCAN_MAX_DEPTH 8

typedef struct {
	char path[SCAN_PATH_MAX];	  // file to search/hash: the ROM, or the folder game's .cue/.m3u
	char filename[SCAN_NAME_MAX]; // basename of path (what goes out as romnom)
	char label[SCAN_NAME_MAX];	  // display name: path relative to the system root, extension stripped
	char art_png[SCAN_PATH_MAX];  // where the mix image lives (nextui's ROM_mediaArtPath convention)
	bool folder_game;
} ScanGame;

// Return false to stop the walk early (e.g. a full table).
typedef bool (*ScanGameCb)(const ScanGame* game, void* userdata);

// Same rule as nextui's hide(): dot-prefixed, ".disabled", or map.txt.
bool Scan_hidden(const char* name);

// True when dir_path holds <dirname>.cue or <dirname>.m3u (cue wins), the
// file that makes it a folder game; out receives that file's path.
bool Scan_folderGameFile(const char* dir_path, char* out, size_t out_size);

// Walk root recursively, calling cb for each game. Returns the number of
// games visited (including the one that stopped the walk).
int Scan_walk(const char* root, ScanGameCb cb, void* userdata);

#endif
