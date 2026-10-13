#ifndef __RA_BADGE_PATH_H__
#define __RA_BADGE_PATH_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Badge image files, one folder per RetroAchievements game id, kept in the
// console folder of the game's rom next to its hashes.json (ra_hash_cache.h):
//
//   Roms/<Folder>/.retroachievements/badges/<game_id>/<BadgeName>.png        unlocked (colored)
//   Roms/<Folder>/.retroachievements/badges/<game_id>/<BadgeName>_lock.png   locked (grey)
//
// <Folder> is the first path component under the Roms root of the rom as the
// user sees it (the .zip, the .m3u, not an unpacked /tmp copy;
// RA_HashCache_folderDir). A rom that is unknown or not under Roms/<Folder>/
// uses <fallback_dir>/<game_id>/ instead (RA_BADGE_CACHE_DIR, ra_paths.h, on
// device). <game_id> is the GameId of the hash's achievementsets response
// (rc_client game->id), the same for every disc/hash of a game. The same game
// in two console folders, or a badge two games share, is stored once per
// folder: FAT32 caps a directory at ~32k long-named files, and a game's
// folder can be deleted on its own. No SDL here (host-testable).

#define RA_BADGE_FOLDER "badges"

// The game's badge folder for rom_path (NULL/empty: unknown):
// "<console folder>/.retroachievements/badges/<game_id>" or
// "<fallback_dir>/<game_id>". False (buf empty) for game id 0, a missing
// fallback dir (when needed) or a path that does not fit.
bool RA_BadgePath_dir(const char* fallback_dir, const char* rom_path, uint32_t game_id, char* buf, size_t n);

// The file for one badge variant in a game folder from RA_BadgePath_dir().
// False (buf empty) for an empty game dir, an empty name, a name that is not
// a plain file name, or a path that does not fit.
bool RA_BadgePath_inDir(const char* game_dir, const char* badge_name, bool locked, char* buf, size_t n);

// RA_BadgePath_dir() + RA_BadgePath_inDir().
bool RA_BadgePath_file(const char* fallback_dir, const char* rom_path, uint32_t game_id, const char* badge_name,
					   bool locked, char* buf, size_t n);

// Create a game folder from RA_BadgePath_dir() and its two parents if missing
// (badges/ and .retroachievements/ or cache/); the level above those (the
// console folder, .ra) must exist. True when the game folder exists afterwards.
bool RA_BadgePath_ensureDir(const char* game_dir);

#endif // __RA_BADGE_PATH_H__
