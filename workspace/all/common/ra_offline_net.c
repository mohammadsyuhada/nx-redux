#include "ra_offline_net.h"
#include "http.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <rcheevos/rc_api_runtime.h>

typedef struct {
	const char* token;
	RA_SyncProgressFn user_progress;
	void* user_userdata;
	uint32_t last_score;          // freshest totals seen in award responses
	uint32_t last_softcore_score; // (0/0 = server never reported them)
} RA_NetSyncCtx;

static int ra_net_submit(const RA_PendingUnlock* e, uint32_t seconds_since_unlock,
						 void* userdata) {
	RA_NetSyncCtx* ctx = (RA_NetSyncCtx*)userdata;

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
	if (!username || !*username || !token || !*token)
		return -1;
	RA_NetSyncCtx ctx = {token, progress, progress_userdata, 0, 0};
	int synced = RA_Offline_sync(username, time(NULL), ra_net_submit, ra_net_progress, &ctx);
	// refresh the cached login's point totals so the pak's header doesn't
	// show stale scores until the next online login rewrites the cache
	if (synced > 0 && (ctx.last_score || ctx.last_softcore_score))
		RA_Offline_updateCachedScores(ctx.last_score, ctx.last_softcore_score);
	return synced;
}
