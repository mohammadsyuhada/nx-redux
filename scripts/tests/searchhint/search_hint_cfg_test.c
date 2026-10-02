// Host test for the "Show search hint" setting round-trip through
// workspace/all/common/config.c: a settings file WITHOUT the key must load
// the default (hint visible, so existing installs are unchanged), the key
// must parse both ways, and the setter must persist it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "config.h"

// config.c reaches the settings file through SHARED_USERDATA_PATH; the test
// script compiles with -DHOSTTEST_SDCARD (scripts/tests/hostplat/platform.h)
// so that resolves under its scratch card, and passes that directory in.

static int failures = 0;
#define CHECK(cond, msg)                \
	do {                                \
		if (cond)                       \
			printf("ok   - %s\n", msg); \
		else {                          \
			printf("FAIL - %s\n", msg); \
			failures++;                 \
		}                               \
	} while (0)

static void write_settings(const char* dir, const char* body) {
	char path[512];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);
	FILE* f = fopen(path, "w");
	if (!f) {
		perror(path);
		exit(2);
	}
	fputs(body, f);
	fclose(f);
}

static bool settings_contain(const char* dir, const char* needle) {
	char path[512];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	static char buf[65536];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	return strstr(buf, needle) != NULL;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <tmpdir>\n", argv[0]);
		return 2;
	}
	const char* dir = argv[1];
	setenv("SHARED_USERDATA_PATH", dir, 1);

	// 1. Existing install: file predates the key -> default (visible).
	write_settings(dir, "menuanim=1\nbatteryperc=0\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_DEFAULT_SHOWSEARCHHINT == true, "default is visible");
	CHECK(CFG_getShowSearchHint() == true, "missing key loads as visible");

	// 2. Explicit off / on parse.
	write_settings(dir, "searchhint=0\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getShowSearchHint() == false, "searchhint=0 loads as hidden");
	write_settings(dir, "searchhint=1\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getShowSearchHint() == true, "searchhint=1 loads as visible");

	// 3. Setter persists, and the key lookup reports the live value.
	CFG_setShowSearchHint(false);
	CHECK(CFG_getShowSearchHint() == false, "setter flips in memory");
	CHECK(settings_contain(dir, "searchhint=0\n"), "setter writes searchhint=0");
	char value[64] = {0};
	CFG_get("searchhint", value);
	CHECK(strcmp(value, "0") == 0, "CFG_get(\"searchhint\") reports 0");
	CFG_setShowSearchHint(true);
	CHECK(settings_contain(dir, "searchhint=1\n"), "setter writes searchhint=1");

	// 4. Reload from what was written.
	CFG_init(NULL, NULL);
	CHECK(CFG_getShowSearchHint() == true, "round-trip reload is visible");

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
