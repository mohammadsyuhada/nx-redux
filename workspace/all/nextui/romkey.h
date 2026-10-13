#ifndef ROMKEY_H
#define ROMKEY_H

#include <stdbool.h>
#include <stddef.h>

// Where a game's display alias lives, as the console list reads it (content.c Directory_index): the map.txt in the
// folder that lists the game, keyed by the listed row's name. A plain ROM (Roms/GB/Tetris.gb) is listed by its file
// name in its own folder (Roms/GB/map.txt, "Tetris.gb"); a folder game's cue/m3u (Roms/PS/Game/Game.cue: the file
// named after its folder) is listed as the folder, one level up (Roms/PS/map.txt, "Game"). A console folder is a
// library, never a game: Roms/DC/DC.m3u stays a plain ROM. Pure: no file system. Returns true for the folder-game form.
bool RomKey_aliasSource(const char* rom_path, const char* roms_root, char* map_dir, size_t map_dir_size, char* key,
						size_t key_size);

// True for a .media picture that is not one game's art: the console's own background (<console>/.media/bg.png and
// bglist.png) or logo (logo.png), which a ROM named "bg.gb" or "logo.gb" would otherwise name. Pure.
bool RomKey_artReserved(const char* png_path);

#endif
