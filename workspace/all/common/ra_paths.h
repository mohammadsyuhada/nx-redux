#ifndef __RA_PATHS_H__
#define __RA_PATHS_H__

// Where RetroAchievements data lives on the card. Everything is under
// .userdata/shared (which Sync always copies; Roms is optional there):
//
//   .ra/cache/login.json, cache/sessions/, pending/, muted/   account data
//   .ra/cache/games/<hash>/{gameid,sets}.json, rom.txt        game data
//   .ra/cache/badges/<game_id>/   badges of roms outside Roms/<Folder>/
//
// Game data and badges are the same for every account (and the Android app on
// the same card). Rom fingerprints and badges live next to the roms, in the
// console folder of the game's rom:
//   Roms/<Folder>/.retroachievements/hashes.json   (ra_hash_cache.h)
//   Roms/<Folder>/.retroachievements/badges/<game_id>/<name>.png, <name>_lock.png
//                                                  (ra_badge_path.h)

#include "defines.h"

#define RA_USER_DIR SHARED_USERDATA_PATH "/.ra"
#define RA_GAMES_DIR RA_USER_DIR "/cache/games"
#define RA_BADGE_CACHE_DIR RA_USER_DIR "/cache/badges"

#endif // __RA_PATHS_H__
