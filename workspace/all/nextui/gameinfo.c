#include "gameinfo.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "api.h"
#include "content.h"
#include "defines.h"
#include "gametimedb.h"
#include "ra_offline.h"
#include "ratools_data.h"
#include "types.h"
#include "utils.h"

// Exported by libgametimedb (not in its header): the SD-relative form the rom table stores.
void __ensure_rel_path(char* rel_path, const char* rom_path);

///////////////////////////////////////
// Data (worker thread only)

static Hash* time_by_path = NULL; // rel path -> "<seconds> <epoch>"
static Hash* hash_by_rom = NULL;  // rom.txt path -> RA hash dir name
static bool time_loaded = false;
static int time_failures = 0; // failed loads; retried up to GAMEINFO_TIME_RETRIES
static bool ra_loaded = false;

// Open sessions (play_time NULL: the game that was running when nextui restarted) are counted
// the way play_activity_stop_all() will close them -- now - created_at, updated "now", and
// dropped when outside 0..24 h (its MAX_PLAUSIBLE_PLAY_TIME DELETE) -- since nextui's
// backgrounded `gametimectl stop_all` may not have run yet when this loads.
#define GAMEINFO_TIME_SQL_NOW "CAST(strftime('%s', 'now') AS INTEGER)"
#define GAMEINFO_TIME_SQL_OPEN_OK \
	"(" GAMEINFO_TIME_SQL_NOW " - pa.created_at BETWEEN 0 AND 86400)"

#define GAMEINFO_TIME_RETRIES 3

// Leaves time_loaded false on a DB error (e.g. SQLITE_BUSY past the timeout) so a later
// request retries instead of caching a partial table for the whole run.
static void loadTimes(void) {
	// Only read an existing DB: play_activity_db_open() creates one when missing.
	char db_path[MAX_PATH];
	snprintf(db_path, sizeof(db_path), "%s/game_logs.sqlite", SHARED_USERDATA_PATH);
	if (!exists(db_path)) {
		time_by_path = Hash_new();
		time_loaded = true;
		return;
	}

	sqlite3* db = play_activity_db_open();
	if (!db) {
		time_failures++;
		return;
	}
	sqlite3_busy_timeout(db, 500);
	const char* sql =
		"SELECT file_path, SUM(t), MAX(w) FROM ("
		" SELECT rom.file_path AS file_path,"
		"  CASE WHEN pa.play_time IS NOT NULL THEN pa.play_time"
		"   WHEN " GAMEINFO_TIME_SQL_OPEN_OK " THEN " GAMEINFO_TIME_SQL_NOW " - pa.created_at END AS t,"
		"  CASE WHEN pa.play_time IS NOT NULL THEN COALESCE(pa.updated_at, pa.created_at)"
		"   WHEN " GAMEINFO_TIME_SQL_OPEN_OK " THEN " GAMEINFO_TIME_SQL_NOW " END AS w"
		" FROM rom JOIN play_activity pa ON pa.rom_id = rom.id"
		" WHERE rom.file_path IS NOT NULL)"
		" GROUP BY file_path HAVING SUM(t) > 0;";
	Hash* table = Hash_new();
	sqlite3_stmt* stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			const char* path = (const char*)sqlite3_column_text(stmt, 0);
			if (!path)
				continue;
			char value[64];
			snprintf(value, sizeof(value), "%d %lld", sqlite3_column_int(stmt, 1),
					 (long long)sqlite3_column_int64(stmt, 2));
			Hash_set(table, path, value);
		}
	}
	sqlite3_finalize(stmt);
	play_activity_db_close(db);

	if (rc != SQLITE_DONE) {
		Hash_free(table);
		time_failures++;
		return;
	}
	time_by_path = table;
	time_loaded = true;
}

static void loadRaIndex(void) {
	ra_loaded = true;
	hash_by_rom = Hash_new();

	char ra_root[MAX_PATH];
	char games_dir[MAX_PATH];
	snprintf(ra_root, sizeof(ra_root), "%s/.ra", SHARED_USERDATA_PATH);
	snprintf(games_dir, sizeof(games_dir), "%s/cache/games", ra_root);
	DIR* d = opendir(games_dir);
	if (!d)
		return; // no RA cache: skip RA_Offline_init (it would create the dirs)

	if (!RA_Offline_rootDir()) // HomeStats_init sets it at startup, before any lookup reaches here
		RA_Offline_init(ra_root);
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		char rom[MAX_PATH];
		if (RA_Offline_getGameRomPath(ent->d_name, rom, sizeof(rom)))
			Hash_set(hash_by_rom, rom, ent->d_name);
	}
	closedir(d);
}

// False when the game time table failed to load and will be retried: the result is then not
// cached, so the next request for the path recomputes it.
static bool computeInfo(const char* path, GameInfo* info) {
	memset(info, 0, sizeof(*info));

	// Candidates: the path, its folder-game file, an .m3u's first disc (what openRom records).
	char cands[3][MAX_PATH];
	int n = 0;
	snprintf(cands[n++], MAX_PATH, "%s", path);
	const char* resolved = path;
	struct stat st;
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && dirGameFile(path, cands[n])) {
		resolved = cands[n];
		n++;
	}
	if (suffixMatch(".m3u", resolved)) {
		char disc[MAX_PATH];
		if (getFirstDisc((char*)resolved, disc)) {
			bool dup = false;
			for (int i = 0; i < n; i++)
				dup = dup || exactMatch(cands[i], disc);
			if (!dup)
				snprintf(cands[n++], MAX_PATH, "%s", disc);
		}
	}

	if (!time_loaded && time_failures < GAMEINFO_TIME_RETRIES)
		loadTimes();
	for (int i = 0; i < n && time_by_path && !info->has_time; i++) {
		char rel[MAX_PATH];
		__ensure_rel_path(rel, cands[i]);
		const char* value = Hash_get(time_by_path, rel);
		long long last = 0;
		int seconds = 0;
		if (value && sscanf(value, "%d %lld", &seconds, &last) == 2) {
			info->has_time = true;
			info->seconds = seconds;
			info->last_played = (time_t)last;
		}
	}

	if (!ra_loaded)
		loadRaIndex();
	for (int i = 0; i < n; i++) {
		const char* hash = Hash_get(hash_by_rom, cands[i]);
		if (!hash)
			continue;
		if (RAT_progressForHash(hash, &info->unlocked, &info->total, info->next,
								sizeof(info->next))) {
			info->has_ra = true;
			break;
		}
	}
	return time_loaded || time_failures >= GAMEINFO_TIME_RETRIES;
}

///////////////////////////////////////
// Worker + result cache

#define GAMEINFO_CACHE_SIZE 32

typedef struct {
	char path[MAX_PATH];
	GameInfo info;
	unsigned age; // 0 = empty slot
} GameInfoSlot;

static pthread_t worker;
static bool worker_running = false;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool quitting = false;
static char pending[MAX_PATH];	 // latest request; empty = none
static char computing[MAX_PATH]; // the request the worker is on; empty = idle
static GameInfoSlot cache[GAMEINFO_CACHE_SIZE];
static unsigned cache_clock = 0;
static SDL_atomic_t loaded;

static void cacheInsert(const char* path, const GameInfo* info) {
	GameInfoSlot* slot = &cache[0];
	for (int i = 0; i < GAMEINFO_CACHE_SIZE; i++) {
		if (cache[i].age < slot->age)
			slot = &cache[i];
	}
	snprintf(slot->path, sizeof(slot->path), "%s", path);
	slot->info = *info;
	slot->age = ++cache_clock;
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
		snprintf(path, sizeof(path), "%s", pending);
		snprintf(computing, sizeof(computing), "%s", pending);
		pending[0] = '\0';
		pthread_mutex_unlock(&lock);

		GameInfo info;
		bool complete = computeInfo(path, &info);

		pthread_mutex_lock(&lock);
		// an incomplete result is not cached; the redraw it triggers re-requests (a retry)
		if (complete)
			cacheInsert(path, &info);
		computing[0] = '\0';
		SDL_AtomicSet(&loaded, 1);
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

void GameInfo_init(void) {
	if (worker_running)
		return;
	quitting = false;
	pending[0] = '\0';
	computing[0] = '\0';
	memset(cache, 0, sizeof(cache));
	cache_clock = 0;
	SDL_AtomicSet(&loaded, 0);
	worker_running = pthread_create(&worker, NULL, workerMain, NULL) == 0;
}

void GameInfo_quit(void) {
	if (!worker_running)
		return;
	pthread_mutex_lock(&lock);
	quitting = true;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
	pthread_join(worker, NULL);
	worker_running = false;

	if (time_by_path)
		Hash_free(time_by_path);
	if (hash_by_rom)
		Hash_free(hash_by_rom);
	time_by_path = hash_by_rom = NULL;
	time_loaded = ra_loaded = false;
	time_failures = 0;
}

bool GameInfo_get(const char* path, GameInfo* out) {
	if (!worker_running || !path || !path[0] || strlen(path) >= MAX_PATH)
		return false;
	bool hit = false;
	pthread_mutex_lock(&lock);
	for (int i = 0; i < GAMEINFO_CACHE_SIZE; i++) {
		if (cache[i].age && exactMatch(cache[i].path, path)) {
			*out = cache[i].info;
			cache[i].age = ++cache_clock;
			hit = true;
			break;
		}
	}
	if (!hit) {
		if (exactMatch(computing, path)) {
			pending[0] = '\0'; // its result is on the way; drop a stale newer request
		} else {
			snprintf(pending, sizeof(pending), "%s", path);
			pthread_cond_signal(&wake);
		}
	}
	pthread_mutex_unlock(&lock);
	return hit;
}

bool GameInfo_checkAsyncLoaded(void) {
	return SDL_AtomicSet(&loaded, 0) != 0;
}
