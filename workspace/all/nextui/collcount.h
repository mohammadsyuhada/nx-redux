#ifndef COLLCOUNT_H
#define COLLCOUNT_H

#include <stdbool.h>

// Game counts for the Collections rows: resolved on a worker thread, kept in an app-private cache
// file (SHARED_USERDATA_PATH/.minui/collection_counts.txt) stamped with the collection file's
// mtime/size and the rom library fingerprint. The worker rewrites the file after each new count, since a
// launch leaves nextui with _exit and CollCount_quit only runs on a clean quit.
void CollCount_init(void);								// load the cache file (ignored if missing/corrupt), start the worker
void CollCount_quit(void);								// stop, join, write the cache file if dirty
int CollCount_get(const char* collection_path);			// count, or -1 while unknown/unreadable (queues a count)
void CollCount_invalidate(const char* collection_path); // after Add to Collection
bool CollCount_checkAsyncLoaded(void);					// main loop: redraw once after a new count

#endif
