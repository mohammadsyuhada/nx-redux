// Host test for the Layouts > "List art" (gamelistart=) and "Backdrop art"
// (backdropart=) settings round-trip through workspace/all/common/config.c: a
// settings file WITHOUT the keys must load the defaults (Screenshot and 3D box
// art, so existing installs are unchanged), every value must parse,
// out-of-range values must fall back to the default like the other game list
// keys, and the setters must persist them.
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

	// 1. Existing install: file predates the keys -> defaults.
	write_settings(dir, "menuanim=1\nshowhome=1\n");
	CFG_init(NULL, NULL);
	CHECK(GAME_LIST_ART_SCREENSHOT == 0 && GAME_LIST_ART_MIX == 1 && GAME_LIST_ART_BOXART2D == 2 &&
			  GAME_LIST_ART_WHEEL == 3 && GAME_LIST_ART_BOXART3D == 4 && GAME_LIST_ART_COUNT == 5,
		  "List art: Screenshot 0, Mix 1, 2D box art 2, Wheel 3, 3D box art 4, count 5");
	CHECK(BACKDROP_ART_BOXART3D == 0 && BACKDROP_ART_BOXART2D == 1 && BACKDROP_ART_WHEEL == 2 &&
			  BACKDROP_ART_COUNT == 3,
		  "Backdrop art: 3D box art 0, 2D box art 1, Wheel 2, count 3");
	CHECK(CFG_DEFAULT_GAMELISTART == GAME_LIST_ART_SCREENSHOT, "List art default is Screenshot");
	CHECK(CFG_DEFAULT_BACKDROPART == BACKDROP_ART_BOXART3D, "Backdrop art default is 3D box art");
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_SCREENSHOT, "missing gamelistart loads as Screenshot");
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART3D, "missing backdropart loads as 3D box art");
	CHECK(settings_contain(dir, "gamelistart=0\n"), "first sync writes gamelistart=0");
	CHECK(settings_contain(dir, "backdropart=0\n"), "first sync writes backdropart=0");

	// 2. Every in-range value parses, and the two keys are independent.
	char body[128];
	char msg[128];
	for (int v = 0; v < GAME_LIST_ART_COUNT; v++) {
		snprintf(body, sizeof(body), "gamelistart=%i\nbackdropart=%i\n", v, (v + 1) % BACKDROP_ART_COUNT);
		write_settings(dir, body);
		CFG_init(NULL, NULL);
		snprintf(msg, sizeof(msg), "gamelistart=%i loads as %i", v, v);
		CHECK(CFG_getGameListArt() == v, msg);
		snprintf(msg, sizeof(msg), "backdropart=%i loads as %i", (v + 1) % BACKDROP_ART_COUNT,
				 (v + 1) % BACKDROP_ART_COUNT);
		CHECK(CFG_getBackdropArt() == (v + 1) % BACKDROP_ART_COUNT, msg);
	}

	// 3. Out-of-range values fall back to the defaults.
	write_settings(dir, "gamelistart=5\nbackdropart=3\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_SCREENSHOT, "gamelistart=5 loads as Screenshot");
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART3D, "backdropart=3 loads as 3D box art");
	write_settings(dir, "gamelistart=-1\nbackdropart=-1\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_SCREENSHOT, "gamelistart=-1 loads as Screenshot");
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART3D, "backdropart=-1 loads as 3D box art");

	// 4. Setters persist, and the key lookup reports the live values.
	char value[64] = {0};
	CFG_setGameListArt(GAME_LIST_ART_WHEEL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_WHEEL, "setter sets Wheel in memory");
	CHECK(settings_contain(dir, "gamelistart=3\n"), "setter writes gamelistart=3");
	CFG_get("gamelistart", value);
	CHECK(strcmp(value, "3") == 0, "CFG_get(\"gamelistart\") reports 3");
	CFG_setBackdropArt(BACKDROP_ART_WHEEL);
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_WHEEL, "setter sets Backdrop Wheel in memory");
	CHECK(settings_contain(dir, "backdropart=2\n"), "setter writes backdropart=2");
	CFG_get("backdropart", value);
	CHECK(strcmp(value, "2") == 0, "CFG_get(\"backdropart\") reports 2");

	// 5. Reload from what was written.
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_WHEEL, "round-trip reload is Wheel");
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_WHEEL, "round-trip reload is Backdrop Wheel");
	CFG_setGameListArt(GAME_LIST_ART_BOXART2D);
	CFG_setBackdropArt(BACKDROP_ART_BOXART2D);
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_BOXART2D, "round-trip reload is 2D box art");
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART2D, "round-trip reload is Backdrop 2D box art");
	CFG_setGameListArt(GAME_LIST_ART_BOXART3D);
	CHECK(settings_contain(dir, "gamelistart=4\n"), "setter writes gamelistart=4");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_BOXART3D, "round-trip reload is 3D box art");

	// 6. The setters guard their input too, and that persists.
	CFG_setGameListArt(5);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_SCREENSHOT, "setter stores 5 as Screenshot");
	CHECK(settings_contain(dir, "gamelistart=0\n"), "setter writes gamelistart=0");
	CFG_setBackdropArt(3);
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART3D, "setter stores 3 as 3D box art");
	CHECK(settings_contain(dir, "backdropart=0\n"), "setter writes backdropart=0");
	CFG_setBackdropArt(-1);
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART3D, "setter stores -1 as 3D box art");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameListArt() == GAME_LIST_ART_SCREENSHOT, "round-trip reload is Screenshot");
	CHECK(CFG_getBackdropArt() == BACKDROP_ART_BOXART3D, "round-trip reload is 3D box art");

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
