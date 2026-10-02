#include "home_stats.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "gametimedb.h"
#include "ra_offline.h"
#include "ratools_data.h"
#include "utils.h"

///////////////////////////////////////
// Data (worker thread only)

// Title fallback when rom.name is NULL: the file name without its extension.
static void pathStem(const char* path, char* out, size_t size) {
	const char* base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(out, size, "%s", base);
	char* dot = strrchr(out, '.');
	if (dot && dot != out)
		*dot = '\0';
}

// Sessions that may overlap the window (a session is at most 24 h, so one starting a day early can
// still reach in). False on a DB error; a missing DB is not one (no sessions).
static bool loadSessions(long long window_start, HomeSession** out, int* count) {
	*out = NULL;
	*count = 0;
	// Only read an existing DB: play_activity_db_open() creates one when missing.
	char db_path[MAX_PATH];
	snprintf(db_path, sizeof(db_path), "%s/game_logs.sqlite", SHARED_USERDATA_PATH);
	if (!exists(db_path))
		return true;

	sqlite3* db = play_activity_db_open();
	if (!db)
		return false;
	sqlite3_busy_timeout(db, 500);
	const char* sql =
		"SELECT rom.id, rom.name, rom.file_path, pa.created_at, pa.play_time"
		" FROM play_activity pa JOIN rom ON rom.id = pa.rom_id"
		" WHERE pa.created_at >= ?1 - 86400 AND " GAMETIME_NOT_EXCLUDED_SQL ";";
	sqlite3_stmt* stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	HomeSession* rows = NULL;
	int n = 0, cap = 0;
	if (rc == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, window_start);
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			if (n == cap) {
				int grown = cap ? cap * 2 : 64;
				HomeSession* bigger = realloc(rows, grown * sizeof(HomeSession));
				if (!bigger) {
					rc = SQLITE_NOMEM;
					break;
				}
				rows = bigger;
				cap = grown;
			}
			HomeSession* s = &rows[n++];
			s->group = sqlite3_column_int(stmt, 0);
			const char* name = (const char*)sqlite3_column_text(stmt, 1);
			const char* path = (const char*)sqlite3_column_text(stmt, 2);
			if (name && name[0])
				snprintf(s->title, sizeof(s->title), "%s", name);
			else if (path)
				pathStem(path, s->title, sizeof(s->title));
			else
				s->title[0] = '\0';
			s->start = sqlite3_column_int64(stmt, 3);
			s->end = sqlite3_column_type(stmt, 4) == SQLITE_NULL
						 ? 0
						 : s->start + sqlite3_column_int64(stmt, 4);
		}
	}
	sqlite3_finalize(stmt);
	play_activity_db_close(db);

	if (rc != SQLITE_DONE) {
		free(rows);
		return false;
	}
	*out = rows;
	*count = n;
	return true;
}

///////////////////////////////////////
// Cache (worker thread only)
//
// The result persists across nextui restarts (one per game launch), keyed on what the worker reads: the
// play-time DB, the RA journal/confirmed files, the RA games and sessions cache directories, the RA sign-in
// state and the local date. A key match loads the file instead of re-reading the DB and re-parsing every
// cached sets.json. What the key can miss: a sets.json or session file rewritten in place inside an existing
// game directory without a rename in cache/sessions (the achievement list or server unlock times change only
// when RA refreshes them, which renames into cache/sessions); that stays stale until the next change to any key
// input, at most until the next local midnight (the date is in the key).

static void cachePath(char* out, size_t size) {
	snprintf(out, size, "%s/.minui/home_stats.txt", SHARED_USERDATA_PATH);
}

static HomeStatsSig fileSig(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0)
		return (HomeStatsSig){-1, -1};
	return (HomeStatsSig){(long long)st.st_mtime, (long long)st.st_size};
}

// A directory: (mtime, entry count). exFAT doesn't reliably bump a directory's mtime, the count catches
// added/removed games.
static HomeStatsSig dirSig(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0)
		return (HomeStatsSig){-1, -1};
	long long count = 0;
	DIR* d = opendir(path);
	if (d) {
		struct dirent* ent;
		while ((ent = readdir(d))) {
			if (ent->d_name[0] != '.')
				count++;
		}
		closedir(d);
	}
	return (HomeStatsSig){(long long)st.st_mtime, count};
}

static void readKey(HomeStatsKey* key, long long now, bool signed_in) {
	char path[MAX_PATH];
	memset(key, 0, sizeof(*key));
	key->day = HomeStats_localDay(now);
	key->signed_in = signed_in ? 1 : 0;
	snprintf(path, sizeof(path), "%s/game_logs.sqlite", SHARED_USERDATA_PATH);
	key->db = fileSig(path);
	snprintf(path, sizeof(path), "%s/.ra/pending/unlocks.jsonl", SHARED_USERDATA_PATH);
	key->journal = fileSig(path);
	snprintf(path, sizeof(path), "%s/.ra/pending/confirmed.jsonl", SHARED_USERDATA_PATH);
	key->confirmed = fileSig(path);
	snprintf(path, sizeof(path), "%s/.ra/cache/games", SHARED_USERDATA_PATH);
	key->games = dirSig(path);
	snprintf(path, sizeof(path), "%s/.ra/cache/sessions", SHARED_USERDATA_PATH);
	key->sessions = dirSig(path);
}

static bool loadCache(const HomeStatsKey* key, HomeStats* out) {
	char path[MAX_PATH];
	cachePath(path, sizeof(path));
	FILE* file = fopen(path, "r");
	if (!file)
		return false;
	char text[4096];
	size_t len = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[len] = '\0';
	return HomeStats_parse(text, key, out);
}

static void writeCache(const HomeStatsKey* key, const HomeStats* st) {
	char path[MAX_PATH];
	char text[4096];
	size_t len = HomeStats_format(key, st, text, sizeof(text));
	cachePath(path, sizeof(path));
	if (len > 0)
		writeFileAtomic(path, text, len); // tmp file + rename
}

///////////////////////////////////////
// Worker

static pthread_t worker;
static bool worker_running = false;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool quitting = false;
static bool pending = false;	  // a recompute is queued
static HomeStats result;		  // the latest published result
static HomeStatsKey result_key;	  // the inputs `result` was computed from (worker thread only)
static bool result_keyed = false; // result_key is valid: `result` is current for it (worker thread only)
static SDL_atomic_t loaded;
static SDL_atomic_t cancel_scan; // set by quit: the RA scan stops before the next cached hash

static bool scanCancelled(void) {
	return SDL_AtomicGet(&cancel_scan) != 0;
}

static void* workerMain(void* arg) {
	(void)arg;
	pthread_mutex_lock(&lock);
	for (;;) {
		while (!quitting && !pending)
			pthread_cond_wait(&wake, &lock);
		if (quitting)
			break;
		pending = false;
		bool have_result = result.ready;
		pthread_mutex_unlock(&lock);

		long long now = (long long)time(NULL);
		// plain int reads of settings the UI thread may change: a stale value only lasts until the next request
		bool signed_in = CFG_getRAEnable() && CFG_getRAAuthenticated();
		HomeStatsKey key;
		readKey(&key, now, signed_in);
		if (result_keyed && HomeStats_keyEqual(&key, &result_key)) {
			pthread_mutex_lock(&lock);
			continue; // nothing the result depends on changed
		}
		HomeStats cached;
		if (!have_result && loadCache(&key, &cached)) {
			LOG_info("stats: cache hit\n");
			result_key = key;
			result_keyed = true;
			pthread_mutex_lock(&lock);
			result = cached;
			SDL_AtomicSet(&loaded, 1);
			continue;
		}
		LOG_info("stats: cache miss, computing\n");

		HomeStats stats;
		HomeSession* sessions = NULL;
		int count = 0;
		bool ok = loadSessions(HomeStats_windowStart(now), &sessions, &count);
		HomeStats_compute(sessions, count, now, &stats);
		free(sessions);
		stats.unlocks = signed_in ? RAT_unlocksSinceCancellable((time_t)stats.window_start, scanCancelled) : -1;
		// -1 while signed in = the scan was cancelled (quitting) or ran out of memory: publish nothing
		bool scan_failed = signed_in && stats.unlocks < 0;

		// only a complete result is cached (and keyed): a DB error or a cancelled scan retries on the next request
		bool complete = ok && !scan_failed;
		if (complete) {
			writeCache(&key, &stats);
			result_key = key;
			result_keyed = true;
		}

		pthread_mutex_lock(&lock);
		// a DB error (e.g. SQLITE_BUSY past the timeout) keeps the previous result; the next request retries
		if (!scan_failed && (ok || !have_result)) {
			result = stats;
			SDL_AtomicSet(&loaded, 1);
		}
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

// RAT_unlocksSince reads the RA cache through ra_offline.c's process-wide root, which is empty until
// RA_Offline_init. Set it here, on the UI thread before the stats worker starts. GameInfo's worker is already
// running by then but sets the root lazily, on its first lookup (none has been asked for yet); left to it, the
// boot-time scan ran first, found no readable cache and counted 0.
// Same guard as GameInfo: no cache directory, no init (it would create the directories).
static void initRaRoot(void) {
	if (RA_Offline_rootDir())
		return;
	char ra_root[MAX_PATH];
	char games_dir[MAX_PATH];
	snprintf(ra_root, sizeof(ra_root), "%s/.ra", SHARED_USERDATA_PATH);
	snprintf(games_dir, sizeof(games_dir), "%s/cache/games", ra_root);
	if (exists(games_dir))
		RA_Offline_init(ra_root);
}

void HomeStats_init(void) {
	if (worker_running)
		return;
	initRaRoot();
	quitting = false;
	pending = false;
	SDL_AtomicSet(&cancel_scan, 0);
	memset(&result, 0, sizeof(result));
	result_keyed = false;
	SDL_AtomicSet(&loaded, 0);
	worker_running = pthread_create(&worker, NULL, workerMain, NULL) == 0;
}

void HomeStats_quit(void) {
	if (!worker_running)
		return;
	SDL_AtomicSet(&cancel_scan, 1); // don't wait out a full RA cache scan (nextui quits on every launch)
	pthread_mutex_lock(&lock);
	quitting = true;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
	pthread_join(worker, NULL);
	worker_running = false;
}

void HomeStats_request(void) {
	if (!worker_running)
		return;
	pthread_mutex_lock(&lock);
	pending = true; // one queued recompute however many requests arrive
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

bool HomeStats_get(HomeStats* out) {
	pthread_mutex_lock(&lock);
	*out = result;
	pthread_mutex_unlock(&lock);
	return out->ready;
}

bool HomeStats_checkAsyncLoaded(void) {
	return SDL_AtomicSet(&loaded, 0) != 0;
}
