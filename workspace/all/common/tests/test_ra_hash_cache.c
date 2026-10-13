// Host-side tests for ra_hash_cache.c (built by run_tests.sh)

#include "ra_hash_cache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#define ROOT "/tmp/nx_ra_hash_cache_test"
#define ROMS ROOT "/Roms"
#define GBA ROMS "/Game Boy Advance (GBA)"
#define CACHE GBA "/.retroachievements/hashes.json"

#define HASH_A "0123456789abcdef0123456789abcdef"
#define HASH_B "fedcba9876543210fedcba9876543210"

static void reset_root(void) {
	assert(system("rm -rf " ROOT) == 0);
	assert(system("mkdir -p '" GBA "/Sub Folder'") == 0);
	RA_HashCache_init(ROMS);
}

static void write_file(const char* path, const char* data) {
	FILE* f = fopen(path, "wb");
	assert(f);
	fputs(data, f);
	fclose(f);
}

static void set_mtime(const char* path, time_t t) {
	struct timeval tv[2] = {{t, 0}, {t, 0}};
	assert(utimes(path, tv) == 0);
}

static char* read_all(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* s = malloc((size_t)n + 1);
	assert(fread(s, 1, (size_t)n, f) == (size_t)n);
	s[n] = '\0';
	fclose(f);
	return s;
}

static void test_mtime_rule(void) {
	assert(RA_HashCache_mtimeMatches(1000, 1000));
	assert(RA_HashCache_mtimeMatches(1000, 1000 + 3600));
	assert(RA_HashCache_mtimeMatches(1000 + 3600, 1000));
	assert(RA_HashCache_mtimeMatches(1000, 1000 + 900));
	assert(RA_HashCache_mtimeMatches(1000, 1000 + 14 * 3600));
	assert(!RA_HashCache_mtimeMatches(1000, 1001));
	assert(!RA_HashCache_mtimeMatches(1000, 1000 + 1800 + 1));
	assert(!RA_HashCache_mtimeMatches(1000, 1000 + 14 * 3600 + 900));
	assert(!RA_HashCache_mtimeMatches(1000, 1000 + 15 * 3600));
	printf("ok test_mtime_rule\n");
}

static void test_miss_hit(void) {
	reset_root();
	const char* rom = GBA "/Game (USA).gba";
	write_file(rom, "romdata");
	char hash[33];
	uint32_t game = 99;
	// no file, no dir: miss
	assert(!RA_HashCache_lookup(rom, 5, hash, &game));

	struct stat st;
	assert(stat(GBA "/.retroachievements", &st) != 0);
	assert(RA_HashCache_store(rom, 5, HASH_A, 1234));
	assert(stat(CACHE, &st) == 0); // store creates the dir on demand

	assert(RA_HashCache_lookup(rom, 5, hash, &game));
	assert(!strcmp(hash, HASH_A) && game == 1234);
	// game id pointer is optional
	assert(RA_HashCache_lookup(rom, 5, hash, NULL));

	// console mismatch: miss
	assert(!RA_HashCache_lookup(rom, 6, hash, &game));

	// key is the path below the console folder
	char* s = read_all(CACHE);
	assert(strstr(s, "\"version\":1"));
	assert(strstr(s, "\"Game (USA).gba\""));
	assert(strstr(s, "\"console\":5"));
	assert(strstr(s, "\"size\":7"));
	free(s);
	printf("ok test_miss_hit\n");
}

static void test_size_and_mtime_change(void) {
	reset_root();
	const char* rom = GBA "/Game.gba";
	write_file(rom, "romdata");
	set_mtime(rom, 1700000000);
	assert(RA_HashCache_store(rom, 5, HASH_A, 1));
	char hash[33];
	uint32_t game;

	// mtime exact and shifted by a whole hour (FAT time zone)
	assert(RA_HashCache_lookup(rom, 5, hash, &game));
	set_mtime(rom, 1700000000 + 3600);
	assert(RA_HashCache_lookup(rom, 5, hash, &game));
	// +1 s: a real change
	set_mtime(rom, 1700000000 + 1);
	assert(!RA_HashCache_lookup(rom, 5, hash, &game));
	// +15 h: past the time zone window
	set_mtime(rom, 1700000000 + 15 * 3600);
	assert(!RA_HashCache_lookup(rom, 5, hash, &game));

	// size change with the original mtime: miss
	write_file(rom, "romdata-longer");
	set_mtime(rom, 1700000000);
	assert(!RA_HashCache_lookup(rom, 5, hash, &game));

	// restoring updates the stamp
	assert(RA_HashCache_store(rom, 5, HASH_B, 2));
	assert(RA_HashCache_lookup(rom, 5, hash, &game));
	assert(!strcmp(hash, HASH_B) && game == 2);
	printf("ok test_size_and_mtime_change\n");
}

static void test_keys(void) {
	reset_root();
	const char* sub = GBA "/Sub Folder/Disc (USA).m3u";
	const char* zip = GBA "/Game (USA).zip";
	write_file(sub, "a.cue\n");
	write_file(zip, "PK");
	char hash[33];
	uint32_t game;

	assert(RA_HashCache_store(sub, 12, HASH_A, 0));
	assert(RA_HashCache_store(zip, 5, HASH_B, 7));
	char* s = read_all(CACHE);
	assert(strstr(s, "\"Sub Folder/Disc (USA).m3u\""));
	assert(strstr(s, "\"Game (USA).zip\""));
	free(s);
	assert(RA_HashCache_lookup(sub, 12, hash, &game) && game == 0 && !strcmp(hash, HASH_A));
	assert(RA_HashCache_lookup(zip, 5, hash, &game) && game == 7);

	// outside the Roms root, directly in Roms/, or in a hidden folder: never cached
	assert(system("mkdir -p " ROOT "/Other " ROMS "/.hidden") == 0);
	write_file(ROOT "/Other/x.gba", "x");
	write_file(ROMS "/loose.gba", "x");
	write_file(ROMS "/.hidden/x.gba", "x");
	assert(!RA_HashCache_store(ROOT "/Other/x.gba", 5, HASH_A, 1));
	assert(!RA_HashCache_store(ROMS "/loose.gba", 5, HASH_A, 1));
	assert(!RA_HashCache_store(ROMS "/.hidden/x.gba", 5, HASH_A, 1));
	assert(!RA_HashCache_store(ROMS "Extra/x.gba", 5, HASH_A, 1)); // prefix, not a parent
	assert(!RA_HashCache_lookup(ROOT "/Other/x.gba", 5, hash, &game));
	struct stat st;
	assert(stat(ROOT "/Other/.retroachievements", &st) != 0);
	assert(stat(ROMS "/.retroachievements", &st) != 0);

	// a bad hash is refused
	assert(!RA_HashCache_store(zip, 5, "not-a-hash", 1));
	assert(!RA_HashCache_store(zip, 5, NULL, 1));
	printf("ok test_keys\n");
}

static void test_preserve_and_forget(void) {
	reset_root();
	const char* a = GBA "/A.gba";
	const char* b = GBA "/B.gba";
	write_file(a, "aaaa");
	write_file(b, "bbbb");
	assert(system("mkdir -p '" GBA "/.retroachievements'") == 0);
	// another writer's extra keys survive a rewrite
	write_file(CACHE, "{\"version\":1,\"writer\":\"android\",\"files\":{"
					  "\"Old.gba\":{\"size\":1,\"mtime\":2,\"console\":5,\"hash\":\"" HASH_B
					  "\",\"game\":3,\"extra\":true}}}");
	assert(RA_HashCache_store(a, 5, HASH_A, 10));
	assert(RA_HashCache_store(b, 5, HASH_B, 0));
	char* s = read_all(CACHE);
	assert(strstr(s, "\"writer\":\"android\""));
	assert(strstr(s, "\"Old.gba\""));
	assert(strstr(s, "\"extra\":true"));
	assert(strstr(s, "\"A.gba\"") && strstr(s, "\"B.gba\""));
	free(s);

	// identical store: no rewrite
	struct stat st1, st2;
	assert(stat(CACHE, &st1) == 0);
	set_mtime(CACHE, 1000);
	assert(RA_HashCache_store(a, 5, HASH_A, 10));
	assert(stat(CACHE, &st2) == 0 && st2.st_mtime == 1000);
	// ...nor when only a time zone step apart (the other OS's reading of the same FAT time)
	assert(stat(a, &st1) == 0);
	set_mtime(a, st1.st_mtime + 3600);
	assert(RA_HashCache_store(a, 5, HASH_A, 10));
	assert(stat(CACHE, &st2) == 0 && st2.st_mtime == 1000);

	// game 0 entry later learns its game id
	char hash[33];
	uint32_t game;
	assert(RA_HashCache_lookup(b, 5, hash, &game) && game == 0);
	assert(RA_HashCache_store(b, 5, HASH_B, 55));
	assert(RA_HashCache_lookup(b, 5, hash, &game) && game == 55);

	RA_HashCache_forget(a);
	assert(!RA_HashCache_lookup(a, 5, hash, &game));
	assert(RA_HashCache_lookup(b, 5, hash, &game));
	RA_HashCache_forget(a); // miss: no-op
	printf("ok test_preserve_and_forget\n");
}

static void test_corrupt(void) {
	reset_root();
	const char* a = GBA "/A.gba";
	write_file(a, "aaaa");
	assert(system("mkdir -p '" GBA "/.retroachievements'") == 0);
	char hash[33];
	uint32_t game;

	write_file(CACHE, "{\"version\":1,\"files\":{\"A.gba\":");
	assert(!RA_HashCache_lookup(a, 5, hash, &game));
	// replaced on the next write
	assert(RA_HashCache_store(a, 5, HASH_A, 1));
	assert(RA_HashCache_lookup(a, 5, hash, &game) && game == 1);

	// wrong types / bad values: miss, never a crash
	write_file(CACHE, "{\"files\":[1,2]}");
	assert(!RA_HashCache_lookup(a, 5, hash, &game));
	write_file(CACHE, "{\"files\":{\"A.gba\":{\"size\":\"4\",\"mtime\":0,\"console\":5,\"hash\":\"" HASH_A "\"}}}");
	assert(!RA_HashCache_lookup(a, 5, hash, &game));
	write_file(CACHE, "{\"files\":{\"A.gba\":{\"size\":4,\"mtime\":0,\"console\":5,\"hash\":\"xyz\",\"game\":1}}}");
	assert(!RA_HashCache_lookup(a, 5, hash, &game));
	write_file(CACHE, "[]");
	assert(!RA_HashCache_lookup(a, 5, hash, &game));
	assert(RA_HashCache_store(a, 5, HASH_A, 1));
	assert(RA_HashCache_lookup(a, 5, hash, &game));

	// uppercase hashes from another writer are accepted and lowercased
	struct stat st;
	assert(stat(a, &st) == 0);
	char body[512];
	snprintf(body, sizeof(body),
			 "{\"files\":{\"A.gba\":{\"size\":4,\"mtime\":%lld,\"console\":5,\"hash\":\"0123456789ABCDEF0123456789ABCDEF\",\"game\":9}}}",
			 (long long)st.st_mtime);
	write_file(CACHE, body);
	assert(RA_HashCache_lookup(a, 5, hash, &game) && !strcmp(hash, HASH_A) && game == 9);

	// missing rom: miss, and nothing to store
	assert(!RA_HashCache_lookup(GBA "/Missing.gba", 5, hash, &game));
	assert(!RA_HashCache_store(GBA "/Missing.gba", 5, HASH_A, 1));
	printf("ok test_corrupt\n");
}

static void test_can_store(void) {
	assert(RA_HashCache_canStore("/r/GBA/x.gba", 5));
	assert(RA_HashCache_canStore("/tmp/nextarch/GBA/x/x.gba", 5));
	assert(!RA_HashCache_canStore("/r/GBA/x.zip", 5));
	assert(!RA_HashCache_canStore("/r/GBA/x.7Z", 5));
	assert(RA_HashCache_canStore("/r/FBN/x.zip", 27)); // arcade: hashed by name
	assert(!RA_HashCache_canStore(NULL, 5));
	printf("ok test_can_store\n");
}

int main(void) {
	test_mtime_rule();
	test_can_store();
	test_miss_hit();
	test_size_and_mtime_change();
	test_keys();
	test_preserve_and_forget();
	test_corrupt();
	assert(system("rm -rf " ROOT) == 0);
	printf("ALL TESTS PASSED\n");
	return 0;
}
