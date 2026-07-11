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

#endif // __RA_OFFLINE_NET_H__
