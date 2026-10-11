// Host-side tests for the cloud refresh in ra_offline_net.c, with HTTP_post
// stubbed by canned responses. Needs the rcheevos source tree that the
// minarch build fetches (workspace/all/minarch/rcheevos/src); run_tests.sh
// skips this test when it is absent.

#include "http.h"
#include "ra_offline.h"
#include "ra_offline_net.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "/tmp/nx_ra_net_test_root"

// ---------------------------------------------------------------- HTTP stub

static int startsession_calls;
static char startsession_games[256];
static bool fail_console_3;

static HTTP_Response* stub_response(int status, const char* body) {
	HTTP_Response* r = (HTTP_Response*)calloc(1, sizeof(HTTP_Response));
	r->http_status = status;
	r->data = strdup(body);
	r->size = strlen(body);
	return r;
}

HTTP_Response* HTTP_post(const char* url, const char* post_data, const char* content_type) {
	(void)url;
	(void)content_type;
	char r[32] = {0}, c[16] = {0}, g[16] = {0};
	RA_Offline_getParam(post_data, "r", r, sizeof(r));
	if (strcmp(r, "login2") == 0)
		return stub_response(200, "{\"Success\":true,\"User\":\"bob\",\"Token\":\"tok\","
								  "\"Score\":0,\"SoftcoreScore\":3,\"Messages\":0}");
	if (strcmp(r, "allprogress") == 0) {
		RA_Offline_getParam(post_data, "c", c, sizeof(c));
		if (strcmp(c, "5") == 0) // the unlock is counted under the bonus set's own id
			return stub_response(200, "{\"Success\":true,\"Response\":{"
									  "\"100\":{\"Achievements\":10},"
									  "\"101\":{\"Achievements\":5,\"Unlocked\":1}}}");
		if (strcmp(c, "3") == 0 && !fail_console_3)
			return stub_response(200, "{\"Success\":true,\"Response\":{"
									  "\"200\":{\"Achievements\":8}}}");
		return stub_response(500, "Internal Server Error");
	}
	if (strcmp(r, "startsession") == 0) {
		RA_Offline_getParam(post_data, "g", g, sizeof(g));
		startsession_calls++;
		strncat(startsession_games, g, sizeof(startsession_games) - strlen(startsession_games) - 2);
		strcat(startsession_games, " ");
		return stub_response(200, "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}");
	}
	return stub_response(404, "{\"Success\":false}");
}

void HTTP_freeResponse(HTTP_Response* response) {
	if (!response)
		return;
	free(response->data);
	free(response->error);
	free(response);
}

// ---------------------------------------------------------------- fixtures

static void write_file(const char* rel, const char* body) {
	char path[512];
	snprintf(path, sizeof(path), ROOT "/%s", rel);
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	*slash = '\0';
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
	assert(system(cmd) == 0);
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(body, f);
	fclose(f);
}

#define SET(id, type)                                                                    \
	"{\"Title\":null,\"Type\":\"" type "\",\"AchievementSetId\":" #id ",\"GameId\":" #id \
	",\"ImageIconUrl\":\"\",\"Achievements\":[],\"Leaderboards\":[]}"

static void setup(void) {
	assert(system("rm -rf " ROOT) == 0);
	RA_Offline_init(ROOT);
	// game 100 (GBA): core set + bonus set 101; one unlock, earned in the bonus set
	write_file("cache/games/aaaa1111/sets.json",
			   "{\"Success\":true,\"GameId\":100,\"Title\":\"Subset Game\",\"ConsoleId\":5,"
			   "\"ImageIconUrl\":\"\",\"RichPresenceGameId\":100,\"RichPresencePatch\":\"\","
			   "\"Sets\":[" SET(100, "core") "," SET(101, "bonus") "]}");
	write_file("cache/sessions/100.json",
			   "{\"Success\":true,\"Unlocks\":[{\"ID\":5001,\"When\":1}],\"HardcoreUnlocks\":[]}");
	// game 200 (SNES): no unlocks
	write_file("cache/games/bbbb2222/sets.json",
			   "{\"Success\":true,\"GameId\":200,\"Title\":\"Plain Game\",\"ConsoleId\":3,"
			   "\"ImageIconUrl\":\"\",\"RichPresenceGameId\":200,\"RichPresencePatch\":\"\","
			   "\"Sets\":[" SET(200, "core") "]}");
	write_file("cache/sessions/200.json",
			   "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}");
	startsession_calls = 0;
	startsession_games[0] = '\0';
}

// ---------------------------------------------------------------- tests

// A game whose unlocks sit in a bonus/subset set is in sync: allprogress
// reports them under the subset's game id, the cached session lists them all.
static void test_subset_unlocks_in_sync(void) {
	setup();
	fail_console_3 = false;
	RA_RefreshStats stats;
	assert(RA_OfflineNet_refreshCloud("bob", "tok", NULL, NULL, NULL, &stats) == 0);
	if (startsession_calls != 0)
		fprintf(stderr, "re-fetched: %s\n", startsession_games);
	assert(startsession_calls == 0);
	assert(stats.games_checked == 2 && stats.games_updated == 0);
	assert(stats.consoles_failed == 0);
	printf("ok test_subset_unlocks_in_sync\n");
}

// A console whose allprogress request fails is skipped: its games keep their
// cached sessions instead of all being re-fetched.
static void test_failed_console_skipped(void) {
	setup();
	fail_console_3 = true;
	RA_RefreshStats stats;
	assert(RA_OfflineNet_refreshCloud("bob", "tok", NULL, NULL, NULL, &stats) == 0);
	if (startsession_calls != 0)
		fprintf(stderr, "re-fetched: %s\n", startsession_games);
	assert(startsession_calls == 0);
	assert(stats.consoles_failed == 1);
	assert(stats.games_checked == 1); // only the GBA game was compared
	printf("ok test_failed_console_skipped\n");
}

// A game with no cached session is still fetched, even on a failed console.
static void test_missing_session_fetched(void) {
	setup();
	fail_console_3 = true;
	assert(remove(ROOT "/cache/sessions/200.json") == 0);
	RA_RefreshStats stats;
	assert(RA_OfflineNet_refreshCloud("bob", "tok", NULL, NULL, NULL, &stats) == 0);
	assert(startsession_calls == 1 && strcmp(startsession_games, "200 ") == 0);
	assert(stats.games_updated == 1);
	printf("ok test_missing_session_fetched\n");
}

int main(void) {
	test_subset_unlocks_in_sync();
	test_failed_console_skipped();
	test_missing_session_fetched();
	assert(system("rm -rf " ROOT) == 0);
	printf("ALL TESTS PASSED\n");
	return 0;
}
