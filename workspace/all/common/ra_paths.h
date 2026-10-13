#ifndef __RA_PATHS_H__
#define __RA_PATHS_H__

// Where RetroAchievements data lives on the card.
//
// Account data (login, per-game sessions, the offline unlock journal, mutes,
// badge-set markers) stays in RA_USER_DIR. Game data that is the same for
// every account - cached achievement definitions, rom.txt records and badge
// images - lives under Roms, shared with the Android app on the same card:
//
//   Roms/.retroachievements/games/<hash>/{gameid,sets}.json, rom.txt
//   Roms/.retroachievements/badges/<name>.png, <name>_lock.png
//   Roms/<Folder>/.retroachievements/hashes.json   (ra_hash_cache.h)

#include "defines.h"

#define RA_USER_DIR SHARED_USERDATA_PATH "/.ra"
#define RA_SHARED_DIR ROMS_PATH "/.retroachievements"
#define RA_GAMES_DIR RA_SHARED_DIR "/games"
#define RA_BADGE_CACHE_DIR RA_SHARED_DIR "/badges"

#endif // __RA_PATHS_H__
