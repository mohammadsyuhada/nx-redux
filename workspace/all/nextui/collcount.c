#include "collcount.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "api.h"
#include "collcount_model.h"
#include "content.h"
#include "defines.h"
#include "menutabs.h"
#include "utils.h"

// SHARED_USERDATA_PATH is a runtime value on desktop, so the path is built, not concatenated.
static void cachePath(char* out, size_t size) {
	snprintf(out, size, "%s/.minui/collection_counts.txt", SHARED_USERDATA_PATH);
}

// In-memory counts, one per collection file name. count -1 marks a file that could not be read at
// those stamps: it is not retried (no redraw loop) and never written out. Guarded by `lock`.
static CollCountLine* lines = NULL;
static int lines_len = 0;
static int lines_cap = 0;
static bool dirty = false;

static pthread_t worker;
static bool worker_running = false;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool quitting = false;
static char pending[MAX_PATH];	 // latest request; empty = none
static char pending_fp[64];		 // the library fingerprint the request is stamped with
static char computing[MAX_PATH]; // the request the worker is on; empty = idle
static SDL_atomic_t loaded;
static SDL_atomic_t reports; // bumped on every count the worker stores; part of the stamp generation

// The cache file is written once the queue has been empty this long, and at most this often while counts keep
// coming (a first Collections visit queues one per visible tile): every write is an fsync'd SD rewrite that
// would otherwise land once per count, competing with the art loaders. A launch flushes synchronously
// (CollCount_flush from launcher.c queueNext) since it leaves nextui with _exit.
#define FLUSH_IDLE_MS 500
#define FLUSH_BUSY_MS 5000

// Held across one snapshot + its write, always taken before `lock` (never while holding it): two writers never
// interleave, an older snapshot can't land after a newer one, and CollCount_flush waits out a worker write
// already in flight. `lock` itself is never held across the write, so CollCount_get never waits on the card.
static pthread_mutex_t write_lock = PTHREAD_MUTEX_INITIALIZER;

// UI thread only (CollCount_get / CollCount_invalidate): each collection file's last stat, so the tiles that ask
// for a count every frame (gridview, rowview, the List info band) stat a file at most once per generation.
// The generation is stamp_gen + reports + MenuTabs_generation() (each only grows), so a file is re-stat()ed:
//  - after CollCount_invalidate (stamp_gen): every in-app edit of a collection file calls it — Add to
//    Collection (existing and new), Rename and Delete of a collection, and the ROM delete that prunes the
//    game's line from every collection (gamelist.c doAddToCollection / doRenameCollection /
//    doDeleteCollection / removeCollectionLines);
//  - after the worker stores a count (reports): the worker stamps the file itself, and a stale UI stamp
//    would otherwise mismatch it forever and requeue the count every frame;
//  - on a tab switch or MenuTabs_reload (MenuTabs_generation), which also covers edits made outside nextui
//    between visits.
// The library fingerprint is still checked on every call (a cheap memo read), so "a count is only trusted
// against the library it was resolved in" holds without the stamp cache knowing about it.
static CollCountStamps stamps;
static unsigned stamp_gen = 0;

// The cache key: the collection's file name, when it fits a cache line.
static bool keyFor(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	const char* name = slash ? slash + 1 : path;
	if (!name[0] || strlen(name) >= size || strpbrk(name, "\t\r\n"))
		return false;
	snprintf(out, size, "%s", name);
	return true;
}

static CollCountLine* findLine(const char* name) {
	for (int i = 0; i < lines_len; i++) {
		if (strcmp(lines[i].name, name) == 0)
			return &lines[i];
	}
	return NULL;
}

static void storeLine(const CollCountLine* line) {
	CollCountLine* slot = findLine(line->name);
	if (!slot) {
		if (lines_len == lines_cap) {
			int cap = lines_cap ? lines_cap * 2 : 16;
			CollCountLine* grown = realloc(lines, cap * sizeof(CollCountLine));
			if (!grown)
				return;
			lines = grown;
			lines_cap = cap;
		}
		slot = &lines[lines_len++];
	}
	*slot = *line;
}

static void removeLine(const char* name) {
	CollCountLine* slot = findLine(name);
	if (!slot)
		return;
	*slot = lines[--lines_len];
}

static void loadCache(void) {
	char path[MAX_PATH];
	cachePath(path, sizeof(path));
	FILE* file = fopen(path, "r");
	if (!file)
		return;
	char text[512];
	CollCountLine line;
	while (fgets(text, sizeof(text), file)) {
		if (CollCount_parseLine(text, &line))
			storeLine(&line); // a corrupt line is skipped; its collection is just recounted
	}
	fclose(file);
}

// The cache file's text for the current lines (the caller holds `lock`, or the worker is stopped); NULL when
// out of memory. *len 0 = nothing worth keeping.
static char* snapshotCache(size_t* len) {
	size_t cap = (size_t)(lines_len + 1) * 400;
	char* buf = malloc(cap);
	*len = 0;
	if (!buf)
		return NULL;
	buf[0] = '\0';
	for (int i = 0; i < lines_len; i++) {
		if (lines[i].count < 0)
			continue;
		CollCount_formatLine(&lines[i], buf + *len, cap - *len);
		*len += strlen(buf + *len);
	}
	return buf;
}

// The caller holds write_lock.
static void writeSnapshot(const char* buf, size_t len) {
	char path[MAX_PATH];
	cachePath(path, sizeof(path));
	if (len > 0)
		writeFileAtomic(path, buf, len); // tmp file + rename
	else
		unlink(path);
}

// Writes the cache file when a count changed since the last write. Called without `lock` (see write_lock).
static void persist(void) {
	pthread_mutex_lock(&write_lock);
	pthread_mutex_lock(&lock);
	size_t len = 0;
	char* snap = dirty ? snapshotCache(&len) : NULL;
	if (snap)
		dirty = false; // out of memory leaves it dirty for the next try
	pthread_mutex_unlock(&lock);
	if (snap) {
		writeSnapshot(snap, len); // tmp file + fsync + rename
		free(snap);
	}
	pthread_mutex_unlock(&write_lock);
}

// CLOCK_REALTIME, as pthread_cond_timedwait expects by default (macOS has no condattr clock); a clock jump
// only moves one write.
static void deadlineIn(struct timespec* ts, long ms) {
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static void* workerMain(void* arg) {
	(void)arg;
	Uint32 last_write = SDL_GetTicks();
	pthread_mutex_lock(&lock);
	for (;;) {
		while (!quitting && !pending[0]) {
			if (!dirty) {
				pthread_cond_wait(&wake, &lock);
				continue;
			}
			// unsaved counts: write them once the queue has stayed empty for FLUSH_IDLE_MS
			struct timespec until;
			deadlineIn(&until, FLUSH_IDLE_MS);
			if (pthread_cond_timedwait(&wake, &lock, &until) == ETIMEDOUT && !quitting && !pending[0] && dirty) {
				pthread_mutex_unlock(&lock);
				persist();
				last_write = SDL_GetTicks();
				pthread_mutex_lock(&lock);
			}
		}
		if (quitting)
			break;
		char path[MAX_PATH];
		CollCountLine line = {0};
		snprintf(path, sizeof(path), "%s", pending);
		snprintf(line.libfp, sizeof(line.libfp), "%s", pending_fp);
		snprintf(computing, sizeof(computing), "%s", pending);
		pending[0] = '\0';
		pthread_mutex_unlock(&lock);

		// stamp before reading: an edit during the count then mismatches and is recounted
		struct stat st;
		bool stamped = stat(path, &st) == 0 && keyFor(path, line.name, sizeof(line.name));
		if (stamped) {
			line.mtime = (long long)st.st_mtime;
			line.size = (long long)st.st_size;
			line.count = Content_forEachCollectionGame(path, NULL, NULL); // -1: unreadable
		}

		pthread_mutex_lock(&lock);
		if (stamped) {
			storeLine(&line);
			if (line.count >= 0)
				dirty = true;
		}
		computing[0] = '\0';
		if (stamped)
			SDL_AtomicAdd(&reports, 1);
		SDL_AtomicSet(&loaded, 1);

		// a long run of counts still reaches the card now and then, not only when it ends
		if (dirty && SDL_GetTicks() - last_write >= FLUSH_BUSY_MS) {
			pthread_mutex_unlock(&lock);
			persist();
			last_write = SDL_GetTicks();
			pthread_mutex_lock(&lock);
		}
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

void CollCount_init(void) {
	if (worker_running)
		return;
	quitting = false;
	pending[0] = computing[0] = '\0';
	dirty = false;
	lines_len = 0;
	SDL_AtomicSet(&loaded, 0);
	stamp_gen++; // stamps from a previous run are not reused
	loadCache();
	worker_running = pthread_create(&worker, NULL, workerMain, NULL) == 0;
}

void CollCount_quit(void) {
	if (!worker_running)
		return;
	pthread_mutex_lock(&lock);
	quitting = true;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
	pthread_join(worker, NULL);
	worker_running = false;

	persist();
	CollCount_stampsFree(&stamps);
	free(lines);
	lines = NULL;
	lines_len = lines_cap = 0;
	dirty = false;
}

int CollCount_get(const char* collection_path) {
	if (!worker_running || !collection_path || strlen(collection_path) >= MAX_PATH)
		return -1;
	char name[sizeof(lines[0].name)];
	if (!keyFor(collection_path, name, sizeof(name)))
		return -1;
	// a count is only trusted against the library it was resolved in
	const char* libfp = Content_libraryFingerprint();
	if (!libfp[0] || strlen(libfp) >= sizeof(pending_fp))
		return -1;
	// the file's stamps: stat()ed once per generation (see `stamps`), not once per tile per frame
	unsigned gen = stamp_gen + (unsigned)SDL_AtomicGet(&reports) + MenuTabs_generation();
	const CollCountStamp* stamp = CollCount_stampFind(&stamps, name, gen);
	bool ok;
	long long mtime = 0, size = 0;
	if (stamp) {
		ok = stamp->ok;
		mtime = stamp->mtime;
		size = stamp->size;
	} else {
		struct stat st;
		ok = stat(collection_path, &st) == 0;
		if (ok) {
			mtime = (long long)st.st_mtime;
			size = (long long)st.st_size;
		}
		CollCount_stampPut(&stamps, name, ok, mtime, size, gen); // false: stat again next call
	}
	if (!ok)
		return -1;

	int count = -1;
	pthread_mutex_lock(&lock);
	CollCountLine* line = findLine(name);
	if (line && CollCount_matches(line, mtime, size, libfp)) {
		count = line->count; // -1 when it was unreadable at these stamps: not retried
	} else if (exactMatch(computing, collection_path)) {
		pending[0] = '\0'; // its result is on the way; drop a stale newer request
	} else {
		snprintf(pending, sizeof(pending), "%s", collection_path);
		snprintf(pending_fp, sizeof(pending_fp), "%s", libfp);
		pthread_cond_signal(&wake);
	}
	pthread_mutex_unlock(&lock);
	return count;
}

void CollCount_invalidate(const char* collection_path) {
	stamp_gen++; // the file changed: every tile re-stats it on its next call
	char name[sizeof(lines[0].name)];
	if (!worker_running || !collection_path || !keyFor(collection_path, name, sizeof(name)))
		return;
	pthread_mutex_lock(&lock);
	if (findLine(name)) {
		removeLine(name);
		dirty = true;
	}
	pthread_mutex_unlock(&lock);
}

void CollCount_flush(void) {
	if (worker_running)
		persist(); // waits out a worker write in flight, then writes anything newer
}

bool CollCount_checkAsyncLoaded(void) {
	return SDL_AtomicSet(&loaded, 0) != 0;
}
