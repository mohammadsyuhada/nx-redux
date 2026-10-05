#include "ratools_data.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <rcheevos/rc_api_runtime.h>
#include <rcheevos/rc_api_user.h>

#include "config.h"
#include "defines.h"
#include "ra_offline.h"

// nx-redux always runs softcore, where RA suppresses the "Warning: Unknown
// Emulator" achievement in-game - hide it in the browser and counts too.
static bool rat_is_hidden_achievement(uint32_t id) {
	return id == RA_UNKNOWN_EMULATOR_ACHIEVEMENT_ID;
}

// Match what the in-game achievements menu shows
// (RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE): core achievements only, minus the
// Unknown Emulator warning. Unofficial (Flags=5) achievements are excluded.
static bool rat_is_visible_achievement(const rc_api_achievement_definition_t* def) {
	return def->category == RC_ACHIEVEMENT_CATEGORY_CORE &&
		   !rat_is_hidden_achievement(def->id);
}

static bool rat_parse_sets(const char* hash, rc_api_fetch_game_sets_response_t* out) {
	char rel[192];
	snprintf(rel, sizeof(rel), "cache/games/%s/sets.json", hash);
	char* body = NULL;
	size_t len = 0;
	if (!RA_Offline_readCacheFile(rel, &body, &len))
		return false;

	rc_api_server_response_t sr;
	memset(&sr, 0, sizeof(sr));
	sr.body = body;
	sr.body_length = len;
	sr.http_status_code = 200;

	int rc = rc_api_process_fetch_game_sets_server_response(out, &sr);
	free(body);
	if (rc != RC_OK || !out->response.succeeded) {
		rc_api_destroy_fetch_game_sets_response(out);
		return false;
	}
	return true;
}

typedef struct {
	uint32_t id;
	time_t when;
} RAT_ServerUnlock;

// server unlocks for a game (softcore+hardcore merged, with unlock times);
// returns malloc'd array via *out, count as return value
static int rat_server_unlocks(uint32_t session_game_id, RAT_ServerUnlock** out) {
	*out = NULL;
	char rel[128];
	snprintf(rel, sizeof(rel), "cache/sessions/%u.json", session_game_id);
	char* body = NULL;
	size_t len = 0;
	if (!RA_Offline_readCacheFile(rel, &body, &len))
		return 0;

	rc_api_server_response_t sr;
	memset(&sr, 0, sizeof(sr));
	sr.body = body;
	sr.body_length = len;
	sr.http_status_code = 200;

	rc_api_start_session_response_t resp;
	int count = 0;
	if (rc_api_process_start_session_server_response(&resp, &sr) == RC_OK &&
		resp.response.succeeded) {
		uint32_t total = resp.num_unlocks + resp.num_hardcore_unlocks;
		if (total > 0) {
			RAT_ServerUnlock* ids = (RAT_ServerUnlock*)malloc(total * sizeof(RAT_ServerUnlock));
			if (ids) {
				// the server reports the "Unknown Emulator" warning
				// (101000001) as unlocked in every softcore session on an
				// unapproved client - filter hidden ids here so they never
				// inflate the unlocked counts
				for (uint32_t i = 0; i < resp.num_unlocks; i++) {
					if (rat_is_hidden_achievement(resp.unlocks[i].achievement_id))
						continue;
					ids[count].id = resp.unlocks[i].achievement_id;
					ids[count].when = resp.unlocks[i].when;
					count++;
				}
				for (uint32_t i = 0; i < resp.num_hardcore_unlocks; i++) {
					if (rat_is_hidden_achievement(resp.hardcore_unlocks[i].achievement_id))
						continue;
					ids[count].id = resp.hardcore_unlocks[i].achievement_id;
					ids[count].when = resp.hardcore_unlocks[i].when;
					count++;
				}
				*out = ids;
			}
		}
	}
	rc_api_destroy_start_session_response(&resp);
	free(body);
	return count;
}

static bool rat_server_find(const RAT_ServerUnlock* ids, int count, uint32_t id,
							time_t* out_when) {
	for (int i = 0; i < count; i++) {
		if (ids[i].id == id) {
			if (out_when)
				*out_when = ids[i].when;
			return true;
		}
	}
	return false;
}

// The unlock state of achievement `id`: a server unlock, else an offline one awaiting sync (pending),
// else a synced offline one the stale session cache predates (confirmed, unlocked). `hash` filters the
// pending/confirmed entries by game; NULL when the arrays are already filtered. *when = the unlock
// time (untouched while locked).
static RAT_AchState rat_resolve_unlock(uint32_t id, const char* hash, const RAT_ServerUnlock* srv,
									   int nsrv, const RA_PendingUnlock* pend, int npend,
									   const RA_PendingUnlock* conf, int nconf, time_t* when) {
	if (rat_server_find(srv, nsrv, id, when))
		return RAT_ACH_UNLOCKED;
	for (int i = 0; i < npend; i++) {
		if (pend[i].achievement_id == id && (!hash || strcmp(pend[i].game_hash, hash) == 0)) {
			*when = pend[i].when;
			return RAT_ACH_PENDING;
		}
	}
	for (int i = 0; i < nconf; i++) {
		if (conf[i].achievement_id == id && (!hash || strcmp(conf[i].game_hash, hash) == 0)) {
			*when = conf[i].when;
			return RAT_ACH_UNLOCKED;
		}
	}
	return RAT_ACH_LOCKED;
}

typedef int (*RAT_EntriesReadFn)(RA_PendingUnlock* out, int max);

static int rat_entries_for_hash(RAT_EntriesReadFn read_fn, const char* hash,
								RA_PendingUnlock** out) {
	RA_PendingUnlock* all = (RA_PendingUnlock*)malloc(
		sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	*out = NULL;
	if (!all)
		return 0;
	int total = read_fn(all, RA_OFFLINE_MAX_PENDING);
	int kept = 0;
	for (int i = 0; i < total; i++) {
		if (strcmp(all[i].game_hash, hash) == 0)
			all[kept++] = all[i];
	}
	if (kept == 0) {
		free(all);
		return 0;
	}
	*out = all;
	return kept;
}

// journaled offline unlocks awaiting sync
static int rat_pending_for_hash(const char* hash, RA_PendingUnlock** out) {
	return rat_entries_for_hash(RA_Offline_readJournal, hash, out);
}

// synced offline unlocks the stale session cache does not know about yet.
// Filtered while reading: the confirmed file outgrows the per-read cap.
static int rat_confirmed_for_hash(const char* hash, RA_PendingUnlock** out) {
	*out = NULL;
	RA_PendingUnlock* all = (RA_PendingUnlock*)malloc(
		sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	if (!all)
		return 0;
	int kept = RA_Offline_readConfirmedFor(NULL, hash, all, RA_OFFLINE_MAX_PENDING);
	if (kept == 0) {
		free(all);
		return 0;
	}
	*out = all;
	return kept;
}

static int rat_game_cmp(const void* a, const void* b) {
	return strcasecmp(((const RAT_Game*)a)->title, ((const RAT_Game*)b)->title);
}

int RAT_listGames(RAT_Game** out_games) {
	*out_games = NULL;
	char games_dir[512];
	snprintf(games_dir, sizeof(games_dir), "%s/.ra/cache/games", SHARED_USERDATA_PATH);
	DIR* d = opendir(games_dir);
	if (!d)
		return 0;

	int cap = 64, count = 0;
	RAT_Game* games = (RAT_Game*)malloc(cap * sizeof(RAT_Game));
	if (!games) {
		closedir(d);
		return 0;
	}

	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;

		rc_api_fetch_game_sets_response_t sets;
		if (!rat_parse_sets(ent->d_name, &sets))
			continue;

		if (count == cap) {
			cap *= 2;
			RAT_Game* bigger = (RAT_Game*)realloc(games, cap * sizeof(RAT_Game));
			if (!bigger) {
				rc_api_destroy_fetch_game_sets_response(&sets);
				break;
			}
			games = bigger;
		}

		RAT_Game* g = &games[count];
		memset(g, 0, sizeof(*g));
		snprintf(g->hash, sizeof(g->hash), "%s", ent->d_name);
		g->session_game_id = sets.session_game_id;
		snprintf(g->title, sizeof(g->title), "%s", sets.title ? sets.title : "(unknown)");

		RAT_ServerUnlock* server_ids = NULL;
		int server_count = rat_server_unlocks(g->session_game_id, &server_ids);
		RA_PendingUnlock* pend = NULL;
		int pend_count = rat_pending_for_hash(g->hash, &pend);
		RA_PendingUnlock* conf = NULL;
		int conf_count = rat_confirmed_for_hash(g->hash, &conf);

		// counts are derived from the same visible-definition set the
		// achievement list shows, so they can never disagree with it
		for (uint32_t s = 0; s < sets.num_sets; s++) {
			for (uint32_t a = 0; a < sets.sets[s].num_achievements; a++) {
				const rc_api_achievement_definition_t* def = &sets.sets[s].achievements[a];
				if (!rat_is_visible_achievement(def))
					continue;
				g->total++;
				time_t when = 0;
				RAT_AchState state = rat_resolve_unlock(def->id, NULL, server_ids, server_count, pend,
														pend_count, conf, conf_count, &when);
				if (state != RAT_ACH_LOCKED)
					g->unlocked++;
				if (state == RAT_ACH_PENDING)
					g->pending++;
			}
		}

		free(server_ids);
		free(pend);
		free(conf);
		rc_api_destroy_fetch_game_sets_response(&sets);
		count++;
	}
	closedir(d);

	// Multi-disc games: every disc (and the .m3u, which hashes as disc 1)
	// resolves to its own hash but the same RA game, and all of them are
	// cached so minarch can identify any disc offline. Show the game once:
	// keep the entry whose rom is the .m3u when there is one (its art is the
	// folder art), otherwise the first seen, carrying over the highest unlock
	// count (server unlocks are shared through the session cache; only
	// offline-pending unlocks are hash-specific, and those follow the disc
	// that was actually played).
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count;) {
			if (games[j].session_game_id != games[i].session_game_id) {
				j++;
				continue;
			}
			char rom[512];
			bool j_is_m3u = RA_Offline_getGameRomPath(games[j].hash, rom, sizeof(rom)) &&
							strlen(rom) > 4 && strcasecmp(rom + strlen(rom) - 4, ".m3u") == 0;
			RAT_Game* keep = &games[i];
			RAT_Game* drop = &games[j];
			if (j_is_m3u) {
				RAT_Game tmp = games[i];
				games[i] = games[j];
				games[j] = tmp;
			}
			if (drop->unlocked > keep->unlocked) {
				keep->unlocked = drop->unlocked;
				keep->pending = drop->pending;
			}
			memmove(&games[j], &games[j + 1], (size_t)(count - j - 1) * sizeof(RAT_Game));
			count--;
		}
	}

	if (count == 0) {
		free(games);
		return 0;
	}
	qsort(games, count, sizeof(RAT_Game), rat_game_cmp);
	*out_games = games;
	return count;
}

int RAT_loadAchievements(const RAT_Game* game, RAT_Achievement** out) {
	*out = NULL;
	rc_api_fetch_game_sets_response_t sets;
	if (!rat_parse_sets(game->hash, &sets))
		return 0;

	int total = 0;
	for (uint32_t s = 0; s < sets.num_sets; s++)
		total += (int)sets.sets[s].num_achievements;
	if (total == 0) {
		rc_api_destroy_fetch_game_sets_response(&sets);
		return 0;
	}

	RAT_Achievement* achs = (RAT_Achievement*)calloc(total, sizeof(RAT_Achievement));
	if (!achs) {
		rc_api_destroy_fetch_game_sets_response(&sets);
		return 0;
	}

	RAT_ServerUnlock* server_ids = NULL;
	int server_count = rat_server_unlocks(game->session_game_id, &server_ids);
	RA_PendingUnlock* pend = NULL;
	int pend_count = rat_pending_for_hash(game->hash, &pend);
	RA_PendingUnlock* conf = NULL;
	int conf_count = rat_confirmed_for_hash(game->hash, &conf);

	int n = 0;
	for (uint32_t s = 0; s < sets.num_sets; s++) {
		for (uint32_t a = 0; a < sets.sets[s].num_achievements; a++) {
			const rc_api_achievement_definition_t* def = &sets.sets[s].achievements[a];
			if (!rat_is_visible_achievement(def))
				continue;
			RAT_Achievement* dst = &achs[n++];
			dst->id = def->id;
			dst->points = def->points;
			snprintf(dst->title, sizeof(dst->title), "%s", def->title ? def->title : "");
			snprintf(dst->description, sizeof(dst->description), "%s",
					 def->description ? def->description : "");
			snprintf(dst->badge_name, sizeof(dst->badge_name), "%s",
					 def->badge_name ? def->badge_name : "");
			dst->type = def->type;
			dst->rarity = def->rarity;
			time_t when = 0;
			dst->state = rat_resolve_unlock(def->id, NULL, server_ids, server_count, pend, pend_count,
											conf, conf_count, &when);
			dst->unlock_time = dst->state == RAT_ACH_LOCKED ? 0 : when;
		}
	}

	free(server_ids);
	free(pend);
	free(conf);
	rc_api_destroy_fetch_game_sets_response(&sets);
	*out = achs;
	return n;
}

// A cut at byte `len` can split a UTF-8 sequence: back off to the last sequence boundary so the
// string never ends in a partial character.
static void rat_utf8_trim(char* s, size_t len) {
	size_t i = len;
	size_t cont = 0;
	while (i > 0 && cont < 3 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
		i--;
		cont++;
	}
	if (i == 0) {
		s[len - cont] = '\0'; // only continuation bytes: nothing to anchor them
		return;
	}
	unsigned char lead = (unsigned char)s[i - 1];
	size_t need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2
								 : lead >= 0xC0	  ? 1
												  : 0;
	if (lead >= 0xC0 && cont < need)
		s[i - 1] = '\0'; // incomplete: drop the lead byte and its continuation bytes
	else if (lead < 0xC0 && cont > 0)
		s[i] = '\0'; // stray continuation bytes after ASCII
}

bool RAT_progressForHash(const char* hash, int* unlocked, int* total, char* next, size_t next_size) {
	rc_api_fetch_game_sets_response_t sets;
	if (!rat_parse_sets(hash, &sets))
		return false;
	RAT_Game game;
	memset(&game, 0, sizeof(game));
	snprintf(game.hash, sizeof(game.hash), "%s", hash);
	game.session_game_id = sets.session_game_id;
	rc_api_destroy_fetch_game_sets_response(&sets);

	// RAT_loadAchievements already drops non-core and hidden achievements
	// (rat_is_visible_achievement), the same set RAT_listGames counts
	RAT_Achievement* achs = NULL;
	int n = RAT_loadAchievements(&game, &achs);
	if (n <= 0) {
		free(achs);
		return false;
	}
	const RAT_Achievement* first_locked = NULL;
	*unlocked = 0;
	*total = n;
	for (int i = 0; i < n; i++) {
		if (achs[i].state != RAT_ACH_LOCKED)
			(*unlocked)++;
		else if (!first_locked || achs[i].id < first_locked->id)
			first_locked = &achs[i];
	}
	if (next_size) {
		const char* title = first_locked ? first_locked->title : "";
		snprintf(next, next_size, "%s", title);
		size_t len = strlen(next);
		if (strlen(title) > len && len > 0) // truncated: don't end mid-character
			rat_utf8_trim(next, len);
	}
	free(achs);
	return true;
}


/*****************************************************************************
 * Sorting - mirrors the in-game menu's comparators (ma_menu.c
 * ach_sort_achievements): pending-sync counts as unlocked, achievement ID is
 * the display-order proxy, "won by" is the rarity percentage.
 *****************************************************************************/

static int rats_unlocked(const RAT_Achievement* a) {
	return a->state != RAT_ACH_LOCKED;
}

static int rats_cmp_unlocked_first(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	if (rats_unlocked(A) != rats_unlocked(B))
		return rats_unlocked(B) - rats_unlocked(A);
	return 0;
}
static int rats_cmp_display_first(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	return (int)A->id - (int)B->id;
}
static int rats_cmp_display_last(const void* a, const void* b) {
	return -rats_cmp_display_first(a, b);
}
static int rats_cmp_won_by_most(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	if (A->rarity != B->rarity)
		return (B->rarity - A->rarity) > 0 ? 1 : -1;
	return 0;
}
static int rats_cmp_won_by_least(const void* a, const void* b) {
	return -rats_cmp_won_by_most(a, b);
}
static int rats_cmp_points_most(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	return (int)B->points - (int)A->points;
}
static int rats_cmp_points_least(const void* a, const void* b) {
	return -rats_cmp_points_most(a, b);
}
static int rats_cmp_title_az(const void* a, const void* b) {
	return strcmp(((const RAT_Achievement*)a)->title, ((const RAT_Achievement*)b)->title);
}
static int rats_cmp_title_za(const void* a, const void* b) {
	return -rats_cmp_title_az(a, b);
}
static int rats_cmp_type_asc(const void* a, const void* b) {
	return (int)((const RAT_Achievement*)a)->type - (int)((const RAT_Achievement*)b)->type;
}
static int rats_cmp_type_desc(const void* a, const void* b) {
	return -rats_cmp_type_asc(a, b);
}

void RAT_sortAchievements(RAT_Achievement* achs, int count) {
	if (!achs || count <= 1)
		return;

	int (*compare)(const void*, const void*) = NULL;
	switch (CFG_getRAAchievementSortOrder()) {
	case RA_SORT_UNLOCKED_FIRST:
		compare = rats_cmp_unlocked_first;
		break;
	case RA_SORT_DISPLAY_ORDER_FIRST:
		compare = rats_cmp_display_first;
		break;
	case RA_SORT_DISPLAY_ORDER_LAST:
		compare = rats_cmp_display_last;
		break;
	case RA_SORT_WON_BY_MOST:
		compare = rats_cmp_won_by_most;
		break;
	case RA_SORT_WON_BY_LEAST:
		compare = rats_cmp_won_by_least;
		break;
	case RA_SORT_POINTS_MOST:
		compare = rats_cmp_points_most;
		break;
	case RA_SORT_POINTS_LEAST:
		compare = rats_cmp_points_least;
		break;
	case RA_SORT_TITLE_AZ:
		compare = rats_cmp_title_az;
		break;
	case RA_SORT_TITLE_ZA:
		compare = rats_cmp_title_za;
		break;
	case RA_SORT_TYPE_ASC:
		compare = rats_cmp_type_asc;
		break;
	case RA_SORT_TYPE_DESC:
		compare = rats_cmp_type_desc;
		break;
	default:
		return;
	}
	qsort(achs, count, sizeof(RAT_Achievement), compare);
}

bool RAT_getCachedScore(uint32_t* score, uint32_t* softcore_score) {
	char* body = NULL;
	size_t len = 0;
	if (!RA_Offline_readCacheFile("cache/login.json", &body, &len))
		return false;

	rc_api_server_response_t sr;
	memset(&sr, 0, sizeof(sr));
	sr.body = body;
	sr.body_length = len;
	sr.http_status_code = 200;

	rc_api_login_response_t resp;
	bool ok = false;
	if (rc_api_process_login_server_response(&resp, &sr) == RC_OK && resp.response.succeeded) {
		if (score)
			*score = resp.score;
		if (softcore_score)
			*softcore_score = resp.score_softcore;
		ok = true;
	}
	rc_api_destroy_login_response(&resp);
	free(body);
	return ok;
}

typedef struct {
	uint32_t game; // session_game_id: every disc of a multi-disc game shares it (and its unlocks)
	uint32_t id;
} RAT_UnlockKey;

static int rat_unlock_key_cmp(const void* a, const void* b) {
	const RAT_UnlockKey* x = a;
	const RAT_UnlockKey* y = b;
	if (x->game != y->game)
		return x->game < y->game ? -1 : 1;
	return x->id < y->id ? -1 : x->id > y->id;
}

// One pass over cache/games/: each sets.json is parsed once (RAT_listGames + RAT_loadAchievements would
// parse it twice), the journal and confirmed files are read once, and the unlocks are kept as
// (session game, achievement) keys, so the discs of one game (RAT_listGames merges them) count once.
// Reads and parses every cached game: call it from a worker thread only.
int RAT_unlocksSinceCancellable(time_t since, bool (*cancelled)(void)) {
	char games_dir[512];
	snprintf(games_dir, sizeof(games_dir), "%s/.ra/cache/games", SHARED_USERDATA_PATH);
	DIR* d = opendir(games_dir);
	if (!d)
		return 0;

	RA_PendingUnlock* pend = malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	RA_PendingUnlock* conf = malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	if (!pend || !conf) {
		free(pend);
		free(conf);
		closedir(d);
		return -1;
	}
	int pend_count = RA_Offline_readJournal(pend, RA_OFFLINE_MAX_PENDING);
	int conf_count = RA_Offline_readConfirmed(conf, RA_OFFLINE_MAX_PENDING);

	RAT_UnlockKey* keys = NULL;
	int nkeys = 0, cap = 0;
	bool failed = false; // cancelled or out of memory
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (cancelled && cancelled()) {
			failed = true;
			break;
		}
		if (ent->d_name[0] == '.')
			continue;
		const char* hash = ent->d_name;
		rc_api_fetch_game_sets_response_t sets;
		if (!rat_parse_sets(hash, &sets))
			continue;
		RAT_ServerUnlock* server_ids = NULL;
		int server_count = rat_server_unlocks(sets.session_game_id, &server_ids);

		// the journal and confirmed files hold every game's entries: filtered by hash here
		for (uint32_t s = 0; s < sets.num_sets && !failed; s++) {
			for (uint32_t a = 0; a < sets.sets[s].num_achievements; a++) {
				const rc_api_achievement_definition_t* def = &sets.sets[s].achievements[a];
				if (!rat_is_visible_achievement(def))
					continue;
				time_t when = 0;
				if (rat_resolve_unlock(def->id, hash, server_ids, server_count, pend, pend_count, conf,
									   conf_count, &when) == RAT_ACH_LOCKED ||
					when < since)
					continue;
				if (nkeys == cap) {
					int grown = cap ? cap * 2 : 64;
					RAT_UnlockKey* bigger = realloc(keys, grown * sizeof(RAT_UnlockKey));
					if (!bigger) {
						failed = true; // no silent undercount
						break;
					}
					keys = bigger;
					cap = grown;
				}
				keys[nkeys].game = sets.session_game_id;
				keys[nkeys].id = def->id;
				nkeys++;
			}
		}
		free(server_ids);
		rc_api_destroy_fetch_game_sets_response(&sets);
		if (failed)
			break;
	}
	closedir(d);
	free(pend);
	free(conf);

	int count = -1;
	if (!failed) {
		if (nkeys > 1)
			qsort(keys, nkeys, sizeof(RAT_UnlockKey), rat_unlock_key_cmp);
		count = 0;
		for (int i = 0; i < nkeys; i++) {
			if (i == 0 || rat_unlock_key_cmp(&keys[i - 1], &keys[i]) != 0)
				count++;
		}
	}
	free(keys);
	return count;
}
