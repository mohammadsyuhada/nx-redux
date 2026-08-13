#ifndef __NET_CACHE_H__
#define __NET_CACHE_H__

#include <stdbool.h>

// Ensure the cache directory exists (0 on success or already-present, -1 else).
int NetCache_ensureDir(const char* dir);

// Read-through cache. If cache_path already holds a non-empty file and !force,
// returns 0 without touching the network. Otherwise downloads url to
// <cache_path>.tmp via wget and atomically renames it into cache_path.
// should_stop / progress_pct are forwarded to the downloader (may be NULL).
// Returns 0 on success, -1 on failure (cache_path left unchanged on failure).
int NetCache_ensure(const char* url, const char* cache_path, bool force,
					volatile bool* should_stop, volatile int* progress_pct);

#endif // __NET_CACHE_H__
