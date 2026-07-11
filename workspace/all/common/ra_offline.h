#ifndef __RA_OFFLINE_H__
#define __RA_OFFLINE_H__

// Offline support layer for RetroAchievements.
//
// Sits between rc_client and the HTTP layer. Online, successful server
// responses are mirrored to disk (write-through cache). Offline, cached
// responses are served back to rc_client and achievement unlocks are
// journaled for later submission.
//
// HOST-TESTABLE: this module may only depend on libc + pthread. No SDL,
// no rcheevos, no defines.h — the storage root is injected via
// RA_Offline_init().

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define RA_OFFLINE_MAX_PENDING 1024

typedef enum {
	RA_NET_ONLINE = 0,
	RA_NET_OFFLINE,
} RA_NetMode;

/**
 * Initialize the module. Creates <root>/cache, <root>/cache/games,
 * <root>/cache/sessions and <root>/pending. Resets mode to RA_NET_ONLINE.
 * @param root_dir e.g. SHARED_USERDATA_PATH "/.ra"
 */
void RA_Offline_init(const char* root_dir);

void RA_Offline_setMode(RA_NetMode mode);
RA_NetMode RA_Offline_getMode(void);

/** true if cache/login.json exists (a login succeeded online at least once) */
bool RA_Offline_hasLoginCache(void);

/**
 * Record the rom path used to load a game, keyed by its hash, so the
 * achievements browser can later locate the rom's box art (<dir>/.media/
 * <basename-without-ext>.png). Validates game_hash with the same rule as
 * the cache path components (alnum/_/- only); no-op otherwise.
 */
void RA_Offline_setGameRomPath(const char* game_hash, const char* rom_path);

/**
 * Read back the rom path recorded by RA_Offline_setGameRomPath(). Returns
 * false on miss (never recorded, or game_hash invalid).
 */
bool RA_Offline_getGameRomPath(const char* game_hash, char* out, size_t out_size);

/**
 * Extract a URL-encoded form parameter ("key=value&...") from a POST body.
 * Returns false if key absent. Value is copied verbatim (not URL-decoded;
 * RA usernames/hashes/ids are alphanumeric so this is sufficient).
 */
bool RA_Offline_getParam(const char* post_data, const char* key,
						 char* out, size_t out_size);

/**
 * Online path: mirror a successful server response to the disk cache.
 * Classifies the request by its "r" param; non-cacheable requests and
 * bodies without "Success":true are ignored. Safe to call from any thread.
 */
void RA_Offline_cacheResponse(const char* post_data, const char* body,
							  size_t body_len);

/**
 * Read a cache file by path relative to the root (e.g.
 * "cache/games/<hash>/sets.json"). On success *out_body is malloc'd
 * (NUL-terminated; caller frees) and *out_len set. Returns false on miss.
 */
bool RA_Offline_readCacheFile(const char* relpath, char** out_body,
							  size_t* out_len);

/**
 * Offline path: serve or synthesize a response for an rc_client request.
 * Returns true if handled (always, when mode is RA_NET_OFFLINE and the
 * request has an "r" param); *out_body is malloc'd (caller frees),
 * *out_status is 200. Returns false when mode is RA_NET_ONLINE or the
 * request is unclassifiable.
 * awardachievement requests are appended to the journal as a side effect.
 */
bool RA_Offline_handleRequest(const char* post_data, char** out_body,
							  size_t* out_len, int* out_status);

/** Hash of the game most recently seen in a gameid/achievementsets request. */
const char* RA_Offline_currentGameHash(void);

// -------------------- journal / sync --------------------

typedef struct {
	char username[64];
	uint32_t achievement_id;
	char game_hash[64];
	time_t when;
} RA_PendingUnlock;

/** Number of parseable journal entries (all users). */
int RA_Offline_pendingCount(void);

/** Read journal entries into out (up to max). Returns count. */
int RA_Offline_readJournal(RA_PendingUnlock* out, int max);

/** Read confirmed (synced offline) unlocks into out (up to max). Returns
 *  count. These are already on the server; they are kept so offline replays
 *  and the tools pak keep showing them as unlocked until a fresh online
 *  session cache supersedes them. */
int RA_Offline_readConfirmed(RA_PendingUnlock* out, int max);

/**
 * Submit callback for RA_Offline_sync. Return 0 on success (entry is
 * removed from the journal — treat server "already unlocked" as success),
 * negative to keep the entry for a later sync.
 */
typedef int (*RA_SubmitFn)(const RA_PendingUnlock* entry,
						   uint32_t seconds_since_unlock, void* userdata);

typedef void (*RA_SyncProgressFn)(int done, int total, void* userdata);

/**
 * Replay journal entries belonging to `username` through `submit`.
 * Entries for other users are preserved. seconds_since_unlock is
 * max(1, now - entry.when). Journal is rewritten atomically; unparseable
 * lines are preserved verbatim. Writes lastsync on any success (or when
 * there was nothing to sync). Returns number of entries synced, -1 on error.
 */
int RA_Offline_sync(const char* username, time_t now, RA_SubmitFn submit,
					RA_SyncProgressFn progress, void* userdata);

/** Unix time of last successful sync, or 0 if never. */
time_t RA_Offline_lastSyncTime(void);

/**
 * Patch the cached login response's "Score" and "SoftcoreScore" fields
 * (award responses carry the fresh totals; the login cache is otherwise
 * only refreshed by the next online login). No-op if there is no cached
 * login or a field is absent.
 */
void RA_Offline_updateCachedScores(uint32_t score, uint32_t softcore_score);

#endif // __RA_OFFLINE_H__
