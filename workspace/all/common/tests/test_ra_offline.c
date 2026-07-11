// Host-side tests for ra_offline.c
// Build+run:
//   cd workspace/all/common/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -o build/test_ra_offline test_ra_offline.c ../ra_offline.c -I.. && \
//   ./build/test_ra_offline

#include "ra_offline.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ROOT "build/ra_test_root"

static void reset_root(void) {
	system("rm -rf " ROOT);
	RA_Offline_init(ROOT);
}

static bool file_exists(const char* rel) {
	char p[512];
	snprintf(p, sizeof(p), ROOT "/%s", rel);
	struct stat st;
	return stat(p, &st) == 0;
}

static void test_get_param(void) {
	char v[64];
	const char* pd = "r=login2&u=bob&t=tok123&x=1";
	assert(RA_Offline_getParam(pd, "r", v, sizeof(v)) && !strcmp(v, "login2"));
	assert(RA_Offline_getParam(pd, "u", v, sizeof(v)) && !strcmp(v, "bob"));
	assert(RA_Offline_getParam(pd, "x", v, sizeof(v)) && !strcmp(v, "1"));
	assert(!RA_Offline_getParam(pd, "t2", v, sizeof(v)));
	assert(!RA_Offline_getParam(pd, "og", v, sizeof(v))); // must not match inside "login2"... key match is anchored
	assert(!RA_Offline_getParam(NULL, "r", v, sizeof(v)));
	printf("ok test_get_param\n");
}

static void test_cache_classification(void) {
	reset_root();
	const char* login_body = "{\"Success\":true,\"User\":\"bob\",\"Token\":\"tok\",\"Score\":100,\"SoftcoreScore\":5}";
	RA_Offline_cacheResponse("r=login2&u=bob&p=xx", login_body, strlen(login_body));
	assert(file_exists("cache/login.json"));
	assert(RA_Offline_hasLoginCache());

	const char* gid = "{\"Success\":true,\"GameID\":1446}";
	RA_Offline_cacheResponse("r=gameid&m=abcdef0123456789abcdef0123456789", gid, strlen(gid));
	assert(file_exists("cache/games/abcdef0123456789abcdef0123456789/gameid.json"));
	assert(!strcmp(RA_Offline_currentGameHash(), "abcdef0123456789abcdef0123456789"));

	const char* sets = "{\"Success\":true,\"GameId\":1446,\"Title\":\"Test Game\",\"Sets\":[]}";
	RA_Offline_cacheResponse("r=achievementsets&u=bob&t=tok&m=abcdef0123456789abcdef0123456789",
							 sets, strlen(sets));
	assert(file_exists("cache/games/abcdef0123456789abcdef0123456789/sets.json"));

	const char* sess = "{\"Success\":true,\"Unlocks\":[{\"ID\":1,\"When\":1000}],\"HardcoreUnlocks\":[]}";
	RA_Offline_cacheResponse("r=startsession&u=bob&t=tok&g=1446", sess, strlen(sess));
	assert(file_exists("cache/sessions/1446.json"));

	// non-cacheable / failed responses must not be written
	RA_Offline_cacheResponse("r=ping&u=bob&t=tok&g=1446", "{\"Success\":true}", 16);
	assert(!file_exists("cache/sessions/ping.json"));
	const char* fail = "{\"Success\":false,\"Error\":\"nope\"}";
	RA_Offline_cacheResponse("r=startsession&u=bob&t=tok&g=999", fail, strlen(fail));
	assert(!file_exists("cache/sessions/999.json"));
	// path traversal must be rejected
	RA_Offline_cacheResponse("r=gameid&m=../evil", gid, strlen(gid));
	assert(!file_exists("cache/games/../evil/gameid.json"));

	// read back
	char* body = NULL;
	size_t len = 0;
	assert(RA_Offline_readCacheFile("cache/login.json", &body, &len));
	assert(len == strlen(login_body) && !strcmp(body, login_body));
	free(body);
	assert(!RA_Offline_readCacheFile("cache/nope.json", &body, &len));
	printf("ok test_cache_classification\n");
}

static int stub_submit_ok(const RA_PendingUnlock* e, uint32_t secs, void* ud) {
	(void)e; (void)ud;
	assert(secs >= 1);
	return 0;
}
static int stub_submit_fail_777(const RA_PendingUnlock* e, uint32_t secs, void* ud) {
	(void)secs; (void)ud;
	return (e->achievement_id == 777) ? -1 : 0;
}
static int stub_submit_fail_all(const RA_PendingUnlock* e, uint32_t secs, void* ud) {
	(void)e; (void)secs; (void)ud;
	return -1;
}

static void test_offline_handling(void) {
	reset_root();
	// prime caches (same bodies as test_cache_classification)
	const char* login_body = "{\"Success\":true,\"User\":\"bob\",\"Token\":\"tok\",\"Score\":100,\"SoftcoreScore\":5}";
	RA_Offline_cacheResponse("r=login2&u=bob&p=xx", login_body, strlen(login_body));
	const char* gid = "{\"Success\":true,\"GameID\":1446}";
	RA_Offline_cacheResponse("r=gameid&m=aaaa1111", gid, strlen(gid));
	const char* sets = "{\"Success\":true,\"GameId\":1446,\"Title\":\"Test Game\"}";
	RA_Offline_cacheResponse("r=achievementsets&u=bob&t=tok&m=aaaa1111", sets, strlen(sets));
	const char* sess = "{\"Success\":true,\"Unlocks\":[{\"ID\":1,\"When\":1000}],\"HardcoreUnlocks\":[]}";
	RA_Offline_cacheResponse("r=startsession&u=bob&t=tok&g=1446", sess, strlen(sess));

	char* body; size_t len; int status;

	// online mode: never handles
	assert(!RA_Offline_handleRequest("r=ping&u=bob", &body, &len, &status));

	RA_Offline_setMode(RA_NET_OFFLINE);

	// login served from cache when username matches
	assert(RA_Offline_handleRequest("r=login2&u=bob&t=tok", &body, &len, &status));
	assert(status == 200 && strstr(body, "\"User\":\"bob\""));
	free(body);
	// different user -> synthesized failure
	assert(RA_Offline_handleRequest("r=login2&u=alice&t=tok", &body, &len, &status));
	assert(strstr(body, "\"Success\":false"));
	free(body);

	// gameid + sets served from cache
	assert(RA_Offline_handleRequest("r=gameid&m=aaaa1111", &body, &len, &status));
	assert(strstr(body, "\"GameID\":1446"));
	free(body);
	assert(RA_Offline_handleRequest("r=achievementsets&u=bob&t=tok&m=aaaa1111", &body, &len, &status));
	assert(strstr(body, "Test Game"));
	free(body);
	// unknown hash -> synthesized "GameID":0 (rc_client: game not recognized)
	assert(RA_Offline_handleRequest("r=gameid&m=ffff9999", &body, &len, &status));
	assert(strstr(body, "\"GameID\":0"));
	free(body);
	// reset hash back to aaaa1111 for subsequent tests (with Fix 1, hash is updated on cache miss)
	assert(RA_Offline_handleRequest("r=gameid&m=aaaa1111", &body, &len, &status));
	free(body);

	// ping synthesized
	assert(RA_Offline_handleRequest("r=ping&u=bob&t=tok&g=1446", &body, &len, &status));
	assert(strstr(body, "\"Success\":true"));
	free(body);

	// award -> journaled + success synthesized
	assert(RA_Offline_handleRequest("r=awardachievement&u=bob&t=tok&a=777&h=0&m=aaaa1111&v=x",
									&body, &len, &status));
	assert(strstr(body, "\"Success\":true") && strstr(body, "\"AchievementID\":777"));
	free(body);
	assert(RA_Offline_pendingCount() == 1);
	// duplicate award is deduped
	assert(RA_Offline_handleRequest("r=awardachievement&u=bob&t=tok&a=777&h=0&m=aaaa1111&v=x",
									&body, &len, &status));
	free(body);
	assert(RA_Offline_pendingCount() == 1);

	RA_PendingUnlock e[4];
	assert(RA_Offline_readJournal(e, 4) == 1);
	assert(!strcmp(e[0].username, "bob") && e[0].achievement_id == 777 &&
		   !strcmp(e[0].game_hash, "aaaa1111") && e[0].when > 0);

	// startsession: cached body merged with journaled unlock
	assert(RA_Offline_handleRequest("r=startsession&u=bob&t=tok&g=1446", &body, &len, &status));
	assert(strstr(body, "\"ID\":777"));        // journaled entry injected
	assert(strstr(body, "\"ID\":1"));          // original server unlock kept
	free(body);
	// startsession with no cache at all -> valid synthesized body w/ journal merge
	// (different game: no journal entries for it either)
	assert(RA_Offline_handleRequest("r=achievementsets&u=bob&t=tok&m=bbbb2222", &body, &len, &status));
	free(body); // sets current hash to bbbb2222 (cache miss -> failure body, still tracks hash)
	assert(RA_Offline_handleRequest("r=startsession&u=bob&t=tok&g=555", &body, &len, &status));
	assert(strstr(body, "\"Success\":true"));
	assert(!strstr(body, "\"ID\":777"));   // game 555's session must not inherit game aaaa1111's journal entry
	free(body);

	// unhandled request kinds -> synthesized failure
	assert(RA_Offline_handleRequest("r=submitlbentry&u=bob&t=tok&i=1&s=100", &body, &len, &status));
	assert(strstr(body, "\"Success\":false"));
	free(body);

	RA_Offline_setMode(RA_NET_ONLINE);
	printf("ok test_offline_handling\n");
}

static void test_sync(void) {
	reset_root();
	RA_Offline_setMode(RA_NET_OFFLINE);
	char* body; size_t len; int status;
	// three pending unlocks: two for bob, one for alice
	RA_Offline_handleRequest("r=achievementsets&u=bob&t=tok&m=cccc3333", &body, &len, &status); free(body);
	RA_Offline_handleRequest("r=awardachievement&u=bob&t=tok&a=777&h=0&m=cccc3333", &body, &len, &status); free(body);
	RA_Offline_handleRequest("r=awardachievement&u=bob&t=tok&a=888&h=0&m=cccc3333", &body, &len, &status); free(body);
	RA_Offline_handleRequest("r=awardachievement&u=alice&t=tok&a=999&h=0&m=cccc3333", &body, &len, &status); free(body);
	RA_Offline_setMode(RA_NET_ONLINE);
	assert(RA_Offline_pendingCount() == 3);
	assert(RA_Offline_lastSyncTime() == 0);

	// partial failure: 777 kept, 888 synced, alice's untouched
	time_t now = time(NULL);
	assert(RA_Offline_sync("bob", now, stub_submit_fail_777, NULL, NULL) == 1);
	assert(RA_Offline_pendingCount() == 2);
	assert(RA_Offline_lastSyncTime() == now);

	// full success for bob; alice's entry survives
	assert(RA_Offline_sync("bob", now + 10, stub_submit_ok, NULL, NULL) == 1);
	assert(RA_Offline_pendingCount() == 1);
	RA_PendingUnlock e[4];
	assert(RA_Offline_readJournal(e, 4) == 1 && !strcmp(e[0].username, "alice"));

	// nothing to sync still updates lastsync and returns 0
	assert(RA_Offline_sync("bob", now + 20, stub_submit_ok, NULL, NULL) == 0);
	assert(RA_Offline_lastSyncTime() == now + 20);

	// all submissions fail: journal + lastsync untouched, returns 0
	RA_Offline_setMode(RA_NET_OFFLINE);
	RA_Offline_handleRequest("r=awardachievement&u=bob&t=tok&a=555&h=0&m=cccc3333", &body, &len, &status);
	free(body);
	RA_Offline_setMode(RA_NET_ONLINE);
	assert(RA_Offline_pendingCount() == 2); // alice's 999 + bob's new 555
	assert(RA_Offline_sync("bob", now + 30, stub_submit_fail_all, NULL, NULL) == 0);
	assert(RA_Offline_pendingCount() == 2);          // nothing dropped
	assert(RA_Offline_lastSyncTime() == now + 20);   // lastsync NOT advanced

	// unparseable journal lines survive a rewrite verbatim
	{
		FILE* f = fopen(ROOT "/pending/unlocks.jsonl", "a");
		assert(f);
		fputs("this is not json\n", f);
		fclose(f);
	}
	assert(RA_Offline_sync("alice", now + 40, stub_submit_ok, NULL, NULL) == 1); // alice's 999 syncs, triggers rewrite
	{
		char* jbody = NULL;
		size_t jlen = 0;
		assert(RA_Offline_readCacheFile("pending/unlocks.jsonl", &jbody, &jlen));
		assert(strstr(jbody, "this is not json"));
		free(jbody);
	}

	printf("ok test_sync\n");
}

// Fix B: a corrupt/truncated cache file must be treated as a cache-miss,
// never served verbatim.
static void test_corrupt_cache(void) {
	reset_root();

	// write garbage directly to each cache slot, bypassing the write-through
	// cache (RA_Offline_cacheResponse would refuse a non-"Success":true body)
	{
		char path[512];
		snprintf(path, sizeof(path), ROOT "/cache/login.json");
		FILE* f = fopen(path, "w");
		assert(f);
		fputs("corrupt", f);
		fclose(f);
	}
	{
		system("mkdir -p " ROOT "/cache/games/eeee5555");
		char path[512];
		snprintf(path, sizeof(path), ROOT "/cache/games/eeee5555/sets.json");
		FILE* f = fopen(path, "w");
		assert(f);
		fputs("corrupt", f);
		fclose(f);
	}
	{
		char path[512];
		snprintf(path, sizeof(path), ROOT "/cache/sessions/7777.json");
		FILE* f = fopen(path, "w");
		assert(f);
		fputs("corrupt", f);
		fclose(f);
	}

	RA_Offline_setMode(RA_NET_OFFLINE);
	char* body; size_t len; int status;

	// corrupt login.json -> "no cached login" failure, not the garbage
	assert(RA_Offline_handleRequest("r=login2&u=bob&t=tok", &body, &len, &status));
	assert(strstr(body, "\"Success\":false") && strstr(body, "no cached login"));
	assert(!strstr(body, "corrupt"));
	free(body);

	// corrupt sets.json -> "no cached achievement data" failure, not the garbage
	assert(RA_Offline_handleRequest("r=achievementsets&u=bob&t=tok&m=eeee5555", &body, &len, &status));
	assert(strstr(body, "\"Success\":false") && strstr(body, "no cached achievement data"));
	assert(!strstr(body, "corrupt"));
	free(body);

	// corrupt session cache -> merge falls back to the synthesized empty base
	assert(RA_Offline_handleRequest("r=startsession&u=bob&t=tok&g=7777", &body, &len, &status));
	assert(strstr(body, "\"Success\":true"));
	assert(!strstr(body, "corrupt"));
	free(body);

	RA_Offline_setMode(RA_NET_ONLINE);
	printf("ok test_corrupt_cache\n");
}

// Fix C: a synced (confirmed) unlock must keep suppressing re-triggers
// offline even though the cached session body predates the sync.
static void test_confirmed(void) {
	reset_root();

	RA_Offline_setMode(RA_NET_OFFLINE);
	char* body; size_t len; int status;
	// sets ra_current_hash = dddd4444 (cache miss -> failure body, side effect only)
	RA_Offline_handleRequest("r=achievementsets&u=carol&t=tok&m=dddd4444", &body, &len, &status);
	free(body);
	RA_Offline_handleRequest("r=awardachievement&u=carol&t=tok&a=4242&h=0&m=dddd4444",
							 &body, &len, &status);
	free(body);
	RA_Offline_setMode(RA_NET_ONLINE);
	assert(RA_Offline_pendingCount() == 1);

	time_t now = time(NULL);
	assert(RA_Offline_sync("carol", now, stub_submit_ok, NULL, NULL) == 1);
	assert(RA_Offline_pendingCount() == 0); // confirmed entries are NOT pending

	// offline again: startsession for the same game must still carry the
	// synced achievement (sourced from confirmed.jsonl; no cached session
	// exists here so the merge starts from the empty base)
	RA_Offline_setMode(RA_NET_OFFLINE);
	assert(RA_Offline_handleRequest("r=startsession&u=carol&t=tok&g=9001", &body, &len, &status));
	assert(strstr(body, "\"ID\":4242"));
	free(body);
	assert(RA_Offline_pendingCount() == 0);
	RA_Offline_setMode(RA_NET_ONLINE);

	// duplicate suppression: a cached session whose body already lists the
	// confirmed ID must not get it injected a second time
	const char* sess_dup = "{\"Success\":true,\"Unlocks\":[{\"ID\":4242,\"When\":123}],\"HardcoreUnlocks\":[]}";
	RA_Offline_cacheResponse("r=startsession&u=carol&t=tok&g=7001", sess_dup, strlen(sess_dup));
	RA_Offline_setMode(RA_NET_OFFLINE);
	assert(RA_Offline_handleRequest("r=startsession&u=carol&t=tok&g=7001", &body, &len, &status));
	char* first = strstr(body, "\"ID\":4242");
	assert(first);
	char* second = strstr(first + 1, "\"ID\":4242");
	assert(!second);
	free(body);

	RA_Offline_setMode(RA_NET_ONLINE);
	printf("ok test_confirmed\n");
}

static void test_game_rom_path(void) {
	reset_root();

	// miss returns false, out buffer untouched semantics aside
	char out[256];
	assert(!RA_Offline_getGameRomPath("abcdef0123456789abcdef0123456789", out, sizeof(out)));

	// set -> get roundtrip
	RA_Offline_setGameRomPath("abcdef0123456789abcdef0123456789",
							  "/mnt/SDCARD/Roms/Game Boy (GB)/Some Game (USA).gb");
	assert(file_exists("cache/games/abcdef0123456789abcdef0123456789/rom.txt"));
	assert(RA_Offline_getGameRomPath("abcdef0123456789abcdef0123456789", out, sizeof(out)));
	assert(!strcmp(out, "/mnt/SDCARD/Roms/Game Boy (GB)/Some Game (USA).gb"));

	// overwrite replaces the previous value
	RA_Offline_setGameRomPath("abcdef0123456789abcdef0123456789", "/mnt/SDCARD/Roms/Other.gb");
	assert(RA_Offline_getGameRomPath("abcdef0123456789abcdef0123456789", out, sizeof(out)));
	assert(!strcmp(out, "/mnt/SDCARD/Roms/Other.gb"));

	// unsafe hash (path traversal attempt) is rejected on both set and get
	RA_Offline_setGameRomPath("../evil", "/mnt/SDCARD/Roms/x.gb");
	assert(!file_exists("cache/games/../evil/rom.txt"));
	assert(!RA_Offline_getGameRomPath("../evil", out, sizeof(out)));

	// miss for a different (valid) hash
	assert(!RA_Offline_getGameRomPath("ffff9999", out, sizeof(out)));

	printf("ok test_game_rom_path\n");
}


static void test_update_cached_scores(void) {
	reset_root();
	const char* login_body = "{\"Success\":true,\"User\":\"bob\",\"Token\":\"tok\",\"Score\":0,\"SoftcoreScore\":0}";
	RA_Offline_cacheResponse("r=login2&u=bob&p=xx", login_body, strlen(login_body));

	RA_Offline_updateCachedScores(150, 275);

	char* body = NULL;
	size_t len = 0;
	assert(RA_Offline_readCacheFile("cache/login.json", &body, &len));
	assert(strstr(body, "\"Score\":150"));
	assert(strstr(body, "\"SoftcoreScore\":275"));
	assert(strstr(body, "\"User\":\"bob\"")); // rest of the body intact
	assert(strstr(body, "\"Token\":\"tok\""));
	free(body);

	// no cached login: must be a clean no-op
	reset_root();
	RA_Offline_updateCachedScores(1, 2);
	assert(!RA_Offline_readCacheFile("cache/login.json", &body, &len));
	printf("ok test_update_cached_scores\n");
}

int main(void) {
	test_get_param();
	test_cache_classification();
	test_offline_handling();
	test_sync();
	test_corrupt_cache();
	test_confirmed();
	test_game_rom_path();
	test_update_cached_scores();
	printf("ALL TESTS PASSED\n");
	return 0;
}
