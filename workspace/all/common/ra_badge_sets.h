#ifndef __RA_BADGE_SETS_H__
#define __RA_BADGE_SETS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Per-game "all badges cached" markers.
//
// The badge cache (RA_BADGE_CACHE_DIR, ra_paths.h) is one flat directory that grows to tens
// of thousands of files once every game's badges are downloaded. On exFAT a
// lookup in it scans the directory, so checking a large set file by file costs
// seconds on a cold boot. A marker records the badge names already known to be
// on disk for one game, so later launches can skip the per-file checks.
//
// Markers live in <ra_dir>/badge_sets/<game_id>.txt, one badge name per line.
// <ra_dir> is the account directory (RA_USER_DIR on device). A marker that
// outlives a badge (the shared dir was cleared, or another app pruned it) is
// harmless: a badge that fails to load drops its game's marker.

// True when the game's marker lists every non-empty name in names.
bool RA_BadgeSets_covers(const char* ra_dir, uint32_t game_id,
						 const char** names, size_t count);

// Record that both variants of every name are cached. Written atomically
// (temp file + rename); failures are silent, the marker is only an optimisation.
void RA_BadgeSets_write(const char* ra_dir, uint32_t game_id,
						const char** names, size_t count);

// Drop the game's marker (a listed badge turned out to be missing).
void RA_BadgeSets_invalidate(const char* ra_dir, uint32_t game_id);

#endif // __RA_BADGE_SETS_H__
