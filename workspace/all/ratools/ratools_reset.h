#ifndef __RATOOLS_RESET_H__
#define __RATOOLS_RESET_H__

// Clears account-specific RetroAchievements state: the cached login, per-game
// session unlock state, and the offline unlock journals (pending + confirmed
// + last-sync). Game achievement definitions and downloaded badges are KEPT,
// so switching accounts does not force a full re-download. Use before signing
// in as a different user.
void RATReset_clearAccountData(void);

// Full wipe: everything the above clears PLUS the cached game definitions,
// rom-path records, and badge cache - the whole <.ra> tree. A subsequent
// online play or prefetch rebuilds it from scratch.
void RATReset_clearAll(void);

#endif // __RATOOLS_RESET_H__
