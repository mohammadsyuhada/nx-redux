// Host-side tests for ra_badge_path.c (built by run_tests.sh)

#include "ra_badge_path.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ROOT "/tmp/nx_ra_badge_path_test"
#define BADGES ROOT "/.ra/cache/badges"

static bool is_dir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void test_file_paths(void) {
	char buf[256];
	assert(RA_BadgePath_file(BADGES, 1234, "00234", false, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/1234/00234.png") == 0);
	assert(RA_BadgePath_file(BADGES, 1234, "00234", true, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/1234/00234_lock.png") == 0);
	// the same badge in another game lives in that game's folder
	assert(RA_BadgePath_file(BADGES, 99, "00234", false, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/99/00234.png") == 0);
}

static void test_rejects(void) {
	char buf[256];
	strcpy(buf, "junk");
	assert(!RA_BadgePath_file(BADGES, 0, "00234", false, buf, sizeof(buf))); // game id 0: no badges
	assert(buf[0] == '\0');
	assert(!RA_BadgePath_file(BADGES, 1, "", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, 1, NULL, false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, 1, "../x", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(BADGES, 1, "..", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file(NULL, 1, "00234", false, buf, sizeof(buf)));
	assert(!RA_BadgePath_file("", 1, "00234", false, buf, sizeof(buf)));
	// truncated path is refused, not cut off
	char small[16];
	assert(!RA_BadgePath_file(BADGES, 1, "00234", false, small, sizeof(small)));
	assert(small[0] == '\0');
}

static void test_dir(void) {
	char buf[256];
	assert(RA_BadgePath_dir(BADGES, 4294967295u, buf, sizeof(buf)));
	assert(strcmp(buf, BADGES "/4294967295") == 0);
	assert(!RA_BadgePath_dir(BADGES, 0, buf, sizeof(buf)));
	assert(buf[0] == '\0');
}

static void test_ensure_dir(void) {
	assert(system("rm -rf " ROOT) == 0);
	assert(system("mkdir -p " ROOT "/.ra/cache") == 0);
	assert(!RA_BadgePath_ensureDir(BADGES, 0));
	assert(!is_dir(BADGES "/0"));
	assert(RA_BadgePath_ensureDir(BADGES, 1234)); // makes badges/ too
	assert(is_dir(BADGES "/1234"));
	assert(RA_BadgePath_ensureDir(BADGES, 1234)); // already there
	// a file in the folder's place is not a folder
	FILE* f = fopen(BADGES "/77", "wb");
	assert(f);
	fclose(f);
	assert(!RA_BadgePath_ensureDir(BADGES, 77));
	// missing parent: nothing created, false
	assert(!RA_BadgePath_ensureDir(ROOT "/nope/badges", 5));
	assert(system("rm -rf " ROOT) == 0);
}

int main(void) {
	test_file_paths();
	test_rejects();
	test_dir();
	test_ensure_dir();
	printf("test_ra_badge_path: all passed\n");
	return 0;
}
