#include "ra_offline_net.h"
#include "http.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <rcheevos/rc_api_runtime.h>
#include <rcheevos/rc_api_user.h>
#include <rcheevos/rc_consoles.h>

typedef struct {
	const char* token;
	RA_SyncProgressFn user_progress;
	RA_CancelFn user_cancel;
	void* user_userdata;
	uint32_t last_score;		  // freshest totals seen in award responses
	uint32_t last_softcore_score; // (0/0 = server never reported them)
	bool cancelled;
} RA_NetSyncCtx;

static int ra_net_submit(const RA_PendingUnlock* e, uint32_t seconds_since_unlock,
						 void* userdata) {
	RA_NetSyncCtx* ctx = (RA_NetSyncCtx*)userdata;

	// cancelled: keep this and every later entry for the next sync (a
	// negative return leaves the journal line in place)
	if (ctx->cancelled || (ctx->user_cancel && ctx->user_cancel(ctx->user_userdata))) {
		ctx->cancelled = true;
		return -1;
	}

	rc_api_award_achievement_request_t api_params;
	memset(&api_params, 0, sizeof(api_params));
	api_params.username = e->username;
	api_params.api_token = ctx->token;
	api_params.achievement_id = e->achievement_id;
	api_params.hardcore = 0; // offline sessions are always softcore
	api_params.game_hash = e->game_hash;
	api_params.seconds_since_unlock = seconds_since_unlock;

	rc_api_request_t request;
	if (rc_api_init_award_achievement_request(&request, &api_params) != RC_OK)
		return -1;

	HTTP_Response* resp = HTTP_post(request.url, request.post_data, request.content_type);
	rc_api_destroy_request(&request);
	if (!resp)
		return -1;

	int result = -1;
	if (resp->data && !resp->error && resp->http_status == 200) {
		rc_api_server_response_t server_response;
		memset(&server_response, 0, sizeof(server_response));
		server_response.body = resp->data;
		server_response.body_length = resp->size;
		server_response.http_status_code = resp->http_status;

		rc_api_award_achievement_response_t award;
		// rcheevos maps "User already has ... awarded" to succeeded=1 itself
		if (rc_api_process_award_achievement_server_response(&award, &server_response) == RC_OK &&
			award.response.succeeded) {
			result = 0;
			// "already unlocked" responses omit the totals (parsed as 0) -
			// only keep genuinely reported scores
			if (award.new_player_score || award.new_player_score_softcore) {
				ctx->last_score = award.new_player_score;
				ctx->last_softcore_score = award.new_player_score_softcore;
			}
		}
		rc_api_destroy_award_achievement_response(&award);
	}
	HTTP_freeResponse(resp);
	return result;
}

static void ra_net_progress(int done, int total, void* userdata) {
	RA_NetSyncCtx* ctx = (RA_NetSyncCtx*)userdata;
	if (ctx->user_progress)
		ctx->user_progress(done, total, ctx->user_userdata);
}

int RA_OfflineNet_syncAll(const char* username, const char* token,
						  RA_SyncProgressFn progress, void* progress_userdata) {
	return RA_OfflineNet_syncAllEx(username, token, progress, NULL, progress_userdata);
}

int RA_OfflineNet_syncAllEx(const char* username, const char* token,
							RA_SyncProgressFn progress, RA_CancelFn cancel,
							void* userdata) {
	if (!username || !*username || !token || !*token)
		return -1;
	RA_NetSyncCtx ctx = {token, progress, cancel, userdata, 0, 0, false};
	int synced = RA_Offline_sync(username, time(NULL), ra_net_submit, ra_net_progress, &ctx);
	// refresh the cached login's point totals so the pak's header doesn't
	// show stale scores until the next online login rewrites the cache
	if (synced > 0 && (ctx.last_score || ctx.last_softcore_score))
		RA_Offline_updateCachedScores(ctx.last_score, ctx.last_softcore_score);
	return synced;
}

int RA_OfflineNet_cacheLogin(const char* username, const char* token) {
	if (!username || !*username || !token || !*token)
		return -1;

	rc_api_login_request_t api_params;
	memset(&api_params, 0, sizeof(api_params));
	api_params.username = username;
	api_params.api_token = token;

	rc_api_request_t request;
	if (rc_api_init_login_request(&request, &api_params) != RC_OK)
		return -1;

	HTTP_Response* resp = HTTP_post(request.url, request.post_data, request.content_type);
	int result = -1;
	if (resp && resp->data && !resp->error && resp->http_status == 200) {
		rc_api_server_response_t server_response;
		memset(&server_response, 0, sizeof(server_response));
		server_response.body = resp->data;
		server_response.body_length = resp->size;
		server_response.http_status_code = resp->http_status;

		rc_api_login_response_t login;
		if (rc_api_process_login_server_response(&login, &server_response) == RC_OK &&
			login.response.succeeded) {
			// same write-through path minarch uses for its own online login
			RA_Offline_cacheResponse(request.post_data, resp->data, resp->size);
			result = RA_Offline_hasLoginCache() ? 0 : -1;
		}
		rc_api_destroy_login_response(&login);
	}
	if (resp)
		HTTP_freeResponse(resp);
	rc_api_destroy_request(&request);
	return result;
}

/*****************************************************************************
 * Cloud refresh (pull the server's fresh unlock state back into the cache)
 *****************************************************************************/

typedef struct {
	uint32_t id;			  // sets "id" (allprogress keys on this)
	uint32_t session_game_id; // used for startsession + cache/sessions/<id>.json
	uint32_t console_id;
	char title[128];
	char hash[64];
} RA_CachedGameInfo;

// Enumerate cache/games/<hash>/sets.json into a malloc'd array (caller frees).
static int ra_net_enum_cached_games(RA_CachedGameInfo** out) {
	*out = NULL;
	const char* root = RA_Offline_rootDir();
	if (!root)
		return 0;

	char games_dir[512];
	snprintf(games_dir, sizeof(games_dir), "%s/cache/games", root);
	DIR* d = opendir(games_dir);
	if (!d)
		return 0;

	int cap = 64, count = 0;
	RA_CachedGameInfo* games = (RA_CachedGameInfo*)malloc(cap * sizeof(RA_CachedGameInfo));
	if (!games) {
		closedir(d);
		return 0;
	}

	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;

		char rel[192];
		snprintf(rel, sizeof(rel), "cache/games/%s/sets.json", ent->d_name);
		char* body = NULL;
		size_t len = 0;
		if (!RA_Offline_readCacheFile(rel, &body, &len))
			continue;

		rc_api_server_response_t sr;
		memset(&sr, 0, sizeof(sr));
		sr.body = body;
		sr.body_length = len;
		sr.http_status_code = 200;

		rc_api_fetch_game_sets_response_t sets;
		if (rc_api_process_fetch_game_sets_server_response(&sets, &sr) == RC_OK &&
			sets.response.succeeded) {
			if (count == cap) {
				int bigger_cap = cap * 2;
				RA_CachedGameInfo* bigger = (RA_CachedGameInfo*)realloc(
					games, bigger_cap * sizeof(RA_CachedGameInfo));
				if (!bigger) {
					rc_api_destroy_fetch_game_sets_response(&sets);
					free(body);
					break;
				}
				games = bigger;
				cap = bigger_cap;
			}
			RA_CachedGameInfo* g = &games[count++];
			memset(g, 0, sizeof(*g));
			g->id = sets.id;
			g->session_game_id = sets.session_game_id;
			g->console_id = sets.console_id;
			snprintf(g->title, sizeof(g->title), "%s", sets.title ? sets.title : "");
			snprintf(g->hash, sizeof(g->hash), "%s", ent->d_name);
		}
		rc_api_destroy_fetch_game_sets_response(&sets);
		free(body);
	}
	closedir(d);

	if (count == 0) {
		free(games);
		return 0;
	}
	*out = games;
	return count;
}

// Count the unlocks in cache/sessions/<session_game_id>.json, excluding the
// synthetic "Unknown Emulator" warning the server reports in every softcore
// session (allprogress does not count it, so leaving it in would make every
// game look permanently out of sync). Returns false when the file is absent.
static bool ra_net_cached_session_count(uint32_t session_game_id, int* out_count) {
	char rel[128];
	snprintf(rel, sizeof(rel), "cache/sessions/%u.json", session_game_id);
	char* body = NULL;
	size_t len = 0;
	if (!RA_Offline_readCacheFile(rel, &body, &len))
		return false;

	rc_api_server_response_t sr;
	memset(&sr, 0, sizeof(sr));
	sr.body = body;
	sr.body_length = len;
	sr.http_status_code = 200;

	rc_api_start_session_response_t resp;
	int count = 0;
	if (rc_api_process_start_session_server_response(&resp, &sr) == RC_OK &&
		resp.response.succeeded) {
		for (uint32_t i = 0; i < resp.num_unlocks; i++)
			if (resp.unlocks[i].achievement_id != RA_UNKNOWN_EMULATOR_ACHIEVEMENT_ID)
				count++;
		for (uint32_t i = 0; i < resp.num_hardcore_unlocks; i++)
			if (resp.hardcore_unlocks[i].achievement_id != RA_UNKNOWN_EMULATOR_ACHIEVEMENT_ID)
				count++;
	}
	rc_api_destroy_start_session_response(&resp);
	free(body);
	if (out_count)
		*out_count = count;
	return true;
}

// Look up a game's server unlock count in an allprogress response. allprogress
// keys on the main game id; fall back to the session id for the rare subset
// game where the two differ.
static bool ra_net_prog_lookup(const rc_api_fetch_all_user_progress_response_t* prog,
							   uint32_t id, uint32_t session_id, int* out_count) {
	if (!prog)
		return false;
	for (uint32_t i = 0; i < prog->num_entries; i++) {
		if (prog->entries[i].game_id == id || prog->entries[i].game_id == session_id) {
			if (out_count)
				*out_count = (int)(prog->entries[i].num_unlocked_achievements +
								   prog->entries[i].num_unlocked_achievements_hardcore);
			return true;
		}
	}
	return false;
}

// Decide whether a cached game's session cache is stale versus the server.
static bool ra_net_game_needs_refresh(const RA_CachedGameInfo* g,
									  const rc_api_fetch_all_user_progress_response_t* prog) {
	int cached_count = 0;
	if (!ra_net_cached_session_count(g->session_game_id, &cached_count))
		return true; // no session cache yet — fetch it
	if (!prog)
		return true; // allprogress unavailable for this console — refresh anyway

	int server_count = 0;
	if (!ra_net_prog_lookup(prog, g->id, g->session_game_id, &server_count))
		return cached_count > 0; // absent from allprogress: stale only if we hold unlocks
	return server_count != cached_count;
}

// POST startsession and mirror the response into cache/sessions/<id>.json.
static bool ra_net_refresh_session(const char* username, const char* token,
								   uint32_t session_game_id, const char* game_hash) {
	rc_api_start_session_request_t ssreq;
	memset(&ssreq, 0, sizeof(ssreq));
	ssreq.username = username;
	ssreq.api_token = token;
	ssreq.game_id = session_game_id;
	ssreq.game_hash = (game_hash && *game_hash) ? game_hash : NULL;

	rc_api_request_t request;
	if (rc_api_init_start_session_request(&request, &ssreq) != RC_OK) {
		rc_api_destroy_request(&request);
		return false;
	}

	HTTP_Response* resp = HTTP_post(request.url, request.post_data, request.content_type);
	bool ok = false;
	if (resp && resp->data && !resp->error && resp->http_status == 200) {
		// request.post_data is still alive here (destroyed below), matching
		// prefetch's rat_post_and_cache ordering
		RA_Offline_cacheResponse(request.post_data, resp->data, resp->size);
		ok = true;
	}
	rc_api_destroy_request(&request);
	if (resp)
		HTTP_freeResponse(resp);
	return ok;
}

// Fetch allprogress for one console (caller destroys *out on true).
static bool ra_net_fetch_allprogress(const char* username, const char* token,
									 uint32_t console_id,
									 rc_api_fetch_all_user_progress_response_t* out) {
	if (console_id == RC_CONSOLE_UNKNOWN)
		return false;

	rc_api_fetch_all_user_progress_request_t preq;
	memset(&preq, 0, sizeof(preq));
	preq.username = username;
	preq.api_token = token;
	preq.console_id = console_id;

	rc_api_request_t request;
	if (rc_api_init_fetch_all_user_progress_request(&request, &preq) != RC_OK) {
		rc_api_destroy_request(&request);
		return false;
	}

	HTTP_Response* resp = HTTP_post(request.url, request.post_data, request.content_type);
	rc_api_destroy_request(&request);
	bool ok = false;
	if (resp && resp->data && !resp->error && resp->http_status == 200) {
		rc_api_server_response_t sr;
		memset(&sr, 0, sizeof(sr));
		sr.body = resp->data;
		sr.body_length = resp->size;
		sr.http_status_code = 200;
		if (rc_api_process_fetch_all_user_progress_server_response(out, &sr) == RC_OK &&
			out->response.succeeded)
			ok = true;
		else
			rc_api_destroy_fetch_all_user_progress_response(out);
	}
	if (resp)
		HTTP_freeResponse(resp);
	return ok;
}

int RA_OfflineNet_refreshCloud(const char* username, const char* token,
							   RA_RefreshProgressFn progress, RA_CancelFn cancel,
							   void* userdata, RA_RefreshStats* out) {
	RA_RefreshStats stats = {0, 0, false};
	if (!username || !*username || !token || !*token) {
		if (out)
			*out = stats;
		return -1;
	}

	// 1) refresh the cached points total
	stats.login_refreshed = (RA_OfflineNet_cacheLogin(username, token) == 0);

	// 2) enumerate cached games
	RA_CachedGameInfo* games = NULL;
	int ngames = ra_net_enum_cached_games(&games);

	// 3+4) walk games grouped by console: one allprogress per console, then
	// re-fetch each stale session. processed[] guards the grouping so every
	// game is handled exactly once regardless of directory order.
	bool* processed = (ngames > 0) ? (bool*)calloc((size_t)ngames, sizeof(bool)) : NULL;
	int done = 0;
	bool cancelled = false;

	for (int i = 0; i < ngames && !cancelled; i++) {
		if (processed && processed[i])
			continue;
		uint32_t console = games[i].console_id;

		rc_api_fetch_all_user_progress_response_t prog;
		bool have_prog = ra_net_fetch_allprogress(username, token, console, &prog);

		for (int j = i; j < ngames && !cancelled; j++) {
			if (games[j].console_id != console)
				continue;
			if (processed)
				processed[j] = true;

			if (cancel && cancel(userdata)) {
				cancelled = true;
				break;
			}

			done++;
			stats.games_checked++;
			if (progress)
				progress(done, ngames, games[j].title[0] ? games[j].title : "game", userdata);

			if (ra_net_game_needs_refresh(&games[j], have_prog ? &prog : NULL)) {
				if (ra_net_refresh_session(username, token, games[j].session_game_id,
										   games[j].hash))
					stats.games_updated++;
			}
		}

		if (have_prog)
			rc_api_destroy_fetch_all_user_progress_response(&prog);
	}

	free(processed);
	free(games);
	if (out)
		*out = stats;
	return stats.login_refreshed ? 0 : -1;
}
