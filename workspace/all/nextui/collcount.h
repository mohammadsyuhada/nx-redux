#ifndef COLLCOUNT_H
#define COLLCOUNT_H

#include <stdbool.h>

// Game counts for the Collections rows: resolved on a worker thread, kept in an app-private cache
// file (SHARED_USERDATA_PATH/.minui/collection_counts.txt) stamped with the collection file's
// mtime/size and the rom library fingerprint. The worker rewrites the file once its queue has been idle for
// 500 ms (at most every 5 s while busy); since a launch leaves nextui with _exit and CollCount_quit only runs
// on a clean quit, the launch path calls CollCount_flush.
void CollCount_init(void);								// load the cache file (ignored if missing/corrupt), start the worker
void CollCount_quit(void);								// stop, join, write the cache file if dirty
int CollCount_get(const char* collection_path);			// count, or -1 while unknown/unreadable (queues a count)
void CollCount_invalidate(const char* collection_path); // after any in-app edit of a collection file (re-stats it)
void CollCount_flush(void);								// write the cache file now if dirty (before a launch's _exit)
bool CollCount_checkAsyncLoaded(void);					// main loop: redraw once after a new count

#endif
