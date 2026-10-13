#ifndef __RA_HASH_CACHE_H__
#define __RA_HASH_CACHE_H__

// RetroAchievements ROM fingerprint cache.
//
// Hashing a ROM (or a disc image) for RetroAchievements reads the whole file,
// so minarch and the RetroAchievements pak's prefetch look the hash up here
// first. One file per console folder, shared with the Android app on the same
// card:
//
//   <roms>/<Folder>/.retroachievements/hashes.json
//   {"version":1,"files":{"<path below <Folder>>":
//     {"size":<bytes>,"mtime":<unix seconds>,"console":<rc console id>,
//      "hash":"<32 lowercase hex>","game":<RA game id, 0 = unknown to RA>}}}
//
// An entry is valid while the file's size matches and its mtime matches,
// exactly or by a whole number of 15 minute steps up to 14 hours (FAT stores
// local time; two OSes may read it in different time zones). Anything else is
// a miss and the caller rehashes and stores again.
//
// HOST-TESTABLE: libc + cJSON only. The Roms root is injected via
// RA_HashCache_init().

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_HASH_CACHE_DIR ".retroachievements"
#define RA_HASH_CACHE_FILE "hashes.json"

/** Set the Roms root (e.g. ROMS_PATH). Paths outside it are never cached. */
void RA_HashCache_init(const char* roms_root);

/**
 * The data dir of rom_path's console folder, "<roms>/<Folder>/.retroachievements"
 * (<Folder> = the first path component under the Roms root; roms in subfolders
 * and m3u's share it). The one place that splits a rom path into its console
 * folder: hashes.json and the badge folders (ra_badge_path.h) live in there.
 * False (buf empty) for paths outside a visible console folder, before
 * RA_HashCache_init(), or when it does not fit.
 */
bool RA_HashCache_folderDir(const char* rom_path, char* buf, size_t n);

/** True when a cached mtime still counts for the file's current one. */
bool RA_HashCache_mtimeMatches(long long cached, long long actual);

/**
 * Look up rom_path's hash for console_id. On a valid hit copies the hash
 * (32 lowercase hex + NUL) to hash_out, sets *game_id (0 = unknown to RA;
 * may be NULL) and returns true.
 */
bool RA_HashCache_lookup(const char* rom_path, uint32_t console_id, char hash_out[33],
						 uint32_t* game_id);

/**
 * Record rom_path's hash, console and game id, stamped with the file's
 * current size and mtime. No write when the stored entry already matches
 * (an mtime off by an accepted time zone step counts as a match). Returns
 * false when nothing could be stored (path outside the Roms root, bad hash,
 * unreadable rom, write failure).
 */
bool RA_HashCache_store(const char* rom_path, uint32_t console_id, const char* hash,
						uint32_t game_id);

/**
 * Whether a hash made from hashed_path may be stored. A .zip/.7z hashed as
 * is (rc_hash reads its raw bytes) is not: minarch unpacks an archive for any
 * core that can't open it and hashes the rom inside, so the raw hash would
 * shadow that correct one. Arcade is the exception (rc_hash hashes the file
 * name there).
 */
bool RA_HashCache_canStore(const char* hashed_path, uint32_t console_id);

/** Drop rom_path's entry (no-op on a miss). */
void RA_HashCache_forget(const char* rom_path);

#endif // __RA_HASH_CACHE_H__
