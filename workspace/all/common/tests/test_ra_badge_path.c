// Host-side tests for ra_badge_path.c (built by run_tests.sh)

#include "ra_badge_path.h"
#include "ra_hash_cache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ROOT "/tmp/nx_ra_badge_path_test"
#define ROMS ROOT "/Roms"
#define BADGES ROOT "/.ra/cache/badges"
#define GBA ROMS "/Game Boy Advance (GBA)"
#define GBA_BADGES GBA "/.retroachievements/badges"

static bool is_dir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void test_console_folder(void) {
	char buf[512];
	// a rom straight in its console folder
	assert(RA_BadgePath_file(BADGES, GBA "/Pokemon.gba", 1234, "00234", false, buf, sizeof(buf)));
	assert(strcmp(buf, GBA_BADGES "/1234/00234.png") == 0);
	assert(RA_BadgePath_file(BADGES, GBA "/Pokemon.gba", 1234, "00234", true, buf, sizeof(buf)));
	assert(strcmp(buf, GBA_BADGES "/1234/00234_lock.png") == 0);
	// a rom in a subfolder (m3u, cue/bin) uses the top-level console folder
	assert(RA_BadgePath_file(BADGES, ROMS "/Sony PlayStation (PS)/FF7 (USA)/FF7 (USA).m3u", 99, "5", false, buf,
							 sizeof(buf)));
	assert(strcmp(buf, ROMS "/Sony PlayStation (PS)/.retroachievements/badges/99/5.png") == 0);
	// an archive is a rom like any other
	assert(RA_BadgePath_file(BADGES, ROMS "/Nintendo (FC)/Mega Man.zip", 7, "1", false, buf, sizeof(buf)));
	assert(strcmp(buf, ROMS "/Nintendo (FC)/.retroachievements/badges/7/1.png") == 0);
	// the same badge in another game lives in that game's folder
	assert(RA_BadgePath_file(BADGES, GBA "/Other.gba", 99, "00234", false, buf, sizeof(buf)));
	assert(strcmp(buf, GBA_BADGES "/99/00234.png") == 0);
	// dir + inDir build the same file
	char dir[512];
	assert(RA_BadgePath_dir(BADGES, GBA "/Pokemon.gba", 1234, dir, sizeof(dir)));
	assert(strcmp(dir, GBA_BADGES "/1234") == 0);
	assert(RA_BadgePath_inDir(dir, "00234", true, buf, sizeof(buf)));
	assert(strcmp(buf, GBA_BADGES "/1234/00234_lock.png") == 0);
}

static void test_fallback(void) {
	char buf[512];
	// unknown rom
	assert(RA_BadgePath_file(BADGES, NULL, 1234, "00234", false, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/1234/00234.png") == 0);
	assert(RA_BadgePath_file(BADGES, "", 1234, "00234", true, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/1234/00234_lock.png") == 0);
	// outside the Roms root, straight in Roms/, a hidden folder, ..
	assert(RA_BadgePath_dir(BADGES, "/tmp/nextarch/x/Mega Man.nes", 5, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/5") == 0);
	assert(RA_BadgePath_dir(BADGES, ROMS "/loose.gba", 5, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/5") == 0);
	assert(RA_BadgePath_dir(BADGES, ROMS "/.hidden/a.gba", 5, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/5") == 0);
	assert(RA_BadgePath_dir(BADGES, GBA "/../x.gba", 5, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/5") == 0);
	assert(RA_BadgePath_dir(BADGES, ROMS "x/GBA/a.gba", 5, buf, sizeof(buf))); // a sibling of Roms
	assert(strcmp(buf, BADGES "/5") == 0);
	// no fallback dir and no console folder: nothing
	strcpy(buf, "junk");
	assert(!RA_BadgePath_dir(NULL, NULL, 5, buf, sizeof(buf)));
	assert(buf[0] == '\0');
	assert(!RA_BadgePath_file("", "/elsewhere/a.gba", 5, "1", false, buf, sizeof(buf)));
	// a console folder needs no fallback dir
	assert(RA_BadgePath_dir(NULL, GBA "/a.gba", 5, buf, sizeof(buf)));
	assert(strcmp(buf, GBA_BADGES "/5") == 0);
}

static void test_rejects(void) {
	char buf[512];
	const char* rom = GBA "/Pokemon.gba";
	strcpy(buf, "junk");
	assert(!RA_BadgePath_file(BADGES, rom, 0, "00234", false, buf, sizeof(buf))); // game id 0: no badges
	assert(buf[0] == '\0');
	assert(!RA_BadgePath_dir(BADGES, rom, 0, buf, sizeof(buf)));
	assert(!RA_BadgePath_dir(BADGES, NULL, 0, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, rom, 1, "", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, rom, 1, NULL, false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, rom, 1, "../x", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, NULL, 1, "..", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, NULL, 1, ".", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_inDir(NULL, "1", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_inDir("", "1", false, buf, sizeof(buf)));
	// truncated paths are refused, not cut off
	char small[16];
	strcpy(small, "junk");
	assert(!RA_BadgePath_file(BADGES, rom, 1, "00234", false, small, sizeof(small)));
	assert(small[0] == '\0');
	strcpy(small, "junk");
	assert(!RA_BadgePath_dir(BADGES, NULL, 1, small, sizeof(small)));
	assert(small[0] == '\0');
	strcpy(small, "junk");
	assert(!RA_BadgePath_inDir(BADGES, "1", false, small, sizeof(small)));
	assert(small[0] == '\0');
	// a console folder too long for the dir: refused, not cut off
	char long_rom[1400];
	memset(long_rom, 'a', sizeof(long_rom));
	memcpy(long_rom, ROMS "/", strlen(ROMS "/"));
	memcpy(long_rom + 1300, "/x.gba", 7);
	assert(!RA_BadgePath_dir(NULL, long_rom, 1, buf, sizeof(buf)));
	assert(buf[0] == '\0');
}

static void test_dir_max_id(void) {
	char buf[256];
	assert(RA_BadgePath_dir(BADGES, NULL, 4294967295u, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/4294967295") == 0);
}

static void test_ensure_dir(void) {
	char dir[512];
	assert(system("rm -rf " ROOT) == 0);
	assert(system("mkdir -p " ROOT "/.ra/cache '" GBA "'") == 0);
	assert(!RA_BadgePath_ensureDir(NULL));
	assert(!RA_BadgePath_ensureDir(""));
	// fallback: makes badges/ too
	assert(RA_BadgePath_dir(BADGES, NULL, 1234, dir, sizeof(dir)));
	assert(RA_BadgePath_ensureDir(dir));
	assert(is_dir(BADGES "/1234"));
	assert(RA_BadgePath_ensureDir(dir)); // already there
	// console folder: makes .retroachievements/ and badges/ too
	assert(RA_BadgePath_dir(BADGES, GBA "/Pokemon.gba", 1234, dir, sizeof(dir)));
	assert(RA_BadgePath_ensureDir(dir));
	assert(is_dir(GBA_BADGES "/1234"));
	// a file in the folder's place is not a folder
	FILE* f = fopen(GBA_BADGES "/77", "wb");
	assert(f);
	fclose(f);
	assert(RA_BadgePath_dir(BADGES, GBA "/a.gba", 77, dir, sizeof(dir)));
	assert(!RA_BadgePath_ensureDir(dir));
	// missing console folder: nothing created, false
	assert(RA_BadgePath_dir(BADGES, ROMS "/Missing/a.gba", 5, dir, sizeof(dir)));
	assert(!RA_BadgePath_ensureDir(dir));
	assert(!is_dir(ROMS "/Missing"));
	assert(system("rm -rf " ROOT) == 0);
}

int main(void) {
	RA_HashCache_init(ROMS "/"); // a trailing slash is fine
	test_console_folder();
	test_fallback();
	test_rejects();
	test_dir_max_id();
	test_ensure_dir();
	// before the Roms root is known everything uses the fallback
	RA_HashCache_init(NULL);
	char buf[256];
	assert(RA_BadgePath_dir(BADGES, GBA "/Pokemon.gba", 3, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/3") == 0);
	printf("test_ra_badge_path: all passed\n");
	return 0;
}
