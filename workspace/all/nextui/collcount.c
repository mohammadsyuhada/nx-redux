#include "collcount.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "api.h"
#include "collcount_model.h"
#include "content.h"
#include "defines.h"
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

// No lock needed: only the worker writes while it runs, and CollCount_quit writes after joining it.
static void writeSnapshot(const char* buf, size_t len) {
	char path[MAX_PATH];
	cachePath(path, sizeof(path));
	if (len > 0)
		writeFileAtomic(path, buf, len); // tmp file + rename
	else
		unlink(path);
}

static void* workerMain(void* arg) {
	(void)arg;
	pthread_mutex_lock(&lock);
	for (;;) {
		while (!quitting && !pending[0])
			pthread_cond_wait(&wake, &lock);
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
		SDL_AtomicSet(&loaded, 1);

		// Persist now: a game or pak launch leaves nextui with _exit, so CollCount_quit never runs in
		// normal use. Snapshot under the lock, write (atomically) outside it.
		if (dirty) {
			size_t len;
			char* snap = snapshotCache(&len);
			if (snap) {
				dirty = false;
				pthread_mutex_unlock(&lock);
				writeSnapshot(snap, len);
				free(snap);
				pthread_mutex_lock(&lock);
			}
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

	if (dirty) {
		size_t len;
		char* snap = snapshotCache(&len);
		if (snap) {
			writeSnapshot(snap, len);
			free(snap);
		}
	}
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
	struct stat st;
	if (stat(collection_path, &st) != 0)
		return -1;

	int count = -1;
	pthread_mutex_lock(&lock);
	CollCountLine* line = findLine(name);
	if (line && CollCount_matches(line, (long long)st.st_mtime, (long long)st.st_size, libfp)) {
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

bool CollCount_checkAsyncLoaded(void) {
	return SDL_AtomicSet(&loaded, 0) != 0;
}
