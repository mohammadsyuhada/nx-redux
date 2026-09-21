#ifndef __RA_OFFLINE_NET_H__
#define __RA_OFFLINE_NET_H__

// Network half of the offline layer: replays the pending-unlock journal
// against the RetroAchievements server. Device-only (links rcheevos + http);
// the journal/cache logic lives in host-testable ra_offline.c.

#include "ra_offline.h"

/**
 * Synchronously submit all journaled unlocks for `username`.
 * Server "already unlocked" responses count as success. Blocking —
 * call from a background thread (minarch) or a modal screen (pak).
 * @return number of entries synced, -1 if nothing could be attempted.
 */
int RA_OfflineNet_syncAll(const char* username, const char* token,
						  RA_SyncProgressFn progress, void* progress_userdata);

/** Cancel poll shared by the sync and refresh entry points: return non-zero
 *  to stop before the next request. Polled between entries only. */
typedef int (*RA_CancelFn)(void* userdata);

/**
 * RA_OfflineNet_syncAll with a cancel poll. Once `cancel` fires, every entry
 * not yet submitted is left in the journal for the next sync (the entry in
 * flight always completes; a curl call cannot be interrupted).
 */
int RA_OfflineNet_syncAllEx(const char* username, const char* token,
							RA_SyncProgressFn progress, RA_CancelFn cancel,
							void* userdata);

/**
 * Synchronously log in with `token` (rc_client's "login2" request) and
 * mirror the server response into the offline cache (cache/login.json),
 * the file minarch needs before it will start a session offline. The pak
 * is the only place a user can log in without ever playing online, so it
 * calls this after authenticating and before a library prefetch. Blocking.
 * @return 0 when the cache was written, -1 on network/auth failure.
 */
int RA_OfflineNet_cacheLogin(const char* username, const char* token);

/** Progress callback for RA_OfflineNet_refreshCloud: current game index
 *  (1-based), total games to check, and the current game's title. */
typedef void (*RA_RefreshProgressFn)(int done, int total, const char* label,
									 void* userdata);

/** Result counters filled by RA_OfflineNet_refreshCloud. */
typedef struct {
	int games_checked;	  // games whose cloud state was examined
	int games_updated;	  // games whose session cache was rewritten
	bool login_refreshed; // login2 (points) re-fetched successfully
} RA_RefreshStats;

/**
 * Pull fresh cloud state so the pak reflects unlocks earned in standalone
 * emulators (e.g. the DC.pak flycast build) that never touched the offline
 * layer. Blocking — call from a modal screen. In order it:
 *   1. re-fetches login2 (refreshes the cached points total);
 *   2. enumerates cached games (cache/games/<hash>/sets.json);
 *   3. for each distinct console, POSTs allprogress once to learn the
 *      server's per-game unlock counts;
 *   4. re-fetches (startsession, write-through) every game whose cached
 *      session is missing or whose unlock count no longer matches the server
 *      (or whose console's allprogress request failed).
 * @param progress optional per-game callback (may be NULL).
 * @param cancel   optional cancel poll (may be NULL).
 * @param out      optional stats (may be NULL).
 * @return 0 on success, -1 when even the login refresh failed.
 */
int RA_OfflineNet_refreshCloud(const char* username, const char* token,
							   RA_RefreshProgressFn progress, RA_CancelFn cancel,
							   void* userdata, RA_RefreshStats* out);

#endif // __RA_OFFLINE_NET_H__
