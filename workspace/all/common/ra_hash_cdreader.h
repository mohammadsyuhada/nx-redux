#ifndef __RA_HASH_CDREADER_H__
#define __RA_HASH_CDREADER_H__

// CHD-aware CD reader callbacks for rcheevos hashing.
// Tries the CHD reader first, falls back to rcheevos' default (CUE/BIN/ISO).
// Shared by minarch (rc_client hash callbacks) and the RetroAchievements
// pak's library prefetch (rc_hash_init_custom_cdreader).

#include <rcheevos/rc_hash.h>

void RA_HashCdreader_get(rc_hash_cdreader_t* out);

#endif // __RA_HASH_CDREADER_H__
