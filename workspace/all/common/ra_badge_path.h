#ifndef __RA_BADGE_PATH_H__
#define __RA_BADGE_PATH_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Badge image files, one folder per RetroAchievements game id:
//
//   <badges_dir>/<game_id>/<BadgeName>.png        unlocked (colored)
//   <badges_dir>/<game_id>/<BadgeName>_lock.png   locked (grey)
//
// <badges_dir> is RA_BADGE_CACHE_DIR (ra_paths.h) on device. <game_id> is the
// GameId of the hash's achievementsets response (rc_client game->id), the same
// for every disc/hash of a game. A badge two games share is stored in both
// folders: FAT32 caps a directory at ~32k long-named files, so one flat folder
// does not scale, and a game's folder can be deleted on its own. No SDL here
// (host-testable).

// "<badges_dir>/<game_id>". False (buf empty) for game id 0 or a missing dir.
bool RA_BadgePath_dir(const char* badges_dir, uint32_t game_id, char* buf, size_t n);

// The file for one badge variant. False (buf empty) for game id 0, an empty
// name or a name that is not a plain file name.
bool RA_BadgePath_file(const char* badges_dir, uint32_t game_id, const char* badge_name, bool locked,
					   char* buf, size_t n);

// Create <badges_dir> and its game folder (the parent of badges_dir must
// exist). True when the game folder exists afterwards.
bool RA_BadgePath_ensureDir(const char* badges_dir, uint32_t game_id);

#endif // __RA_BADGE_PATH_H__
