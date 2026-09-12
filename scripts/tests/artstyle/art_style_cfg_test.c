// Host test for the "Game art style" setting round-trip through
// workspace/all/common/config.c: a settings file WITHOUT the key must load the
// default (Thumbnail = 0, so existing installs are unchanged), the key must
// parse, the setter must clamp and persist, and CFG_get must report it.
// Mirrors scripts/tests/searchhint/search_hint_cfg_test.c.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "paths.h"

// config.c reaches the settings file through SHARED_USERDATA_PATH (a runtime
// path variable in paths.c, linked in); main() points it at a scratch dir.

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
	char path[PATHS_MAX];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);
	FILE* f = fopen(path, "w");
	if (!f) {
		perror(path);
		exit(2);
	}
	fputs(body, f);
	fclose(f);
}

static int settings_contain(const char* dir, const char* needle) {
	char path[PATHS_MAX];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
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
	snprintf(PATHS_SHARED_USERDATA, sizeof(PATHS_SHARED_USERDATA), "%s", dir);
	setenv("SHARED_USERDATA_PATH", dir, 1);

	// 1. Existing install: file predates the key -> default (Thumbnail).
	write_settings(dir, "artWidth=45\nbatteryperc=0\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_DEFAULT_GAMEARTSTYLE == ART_STYLE_THUMBNAIL, "default is Thumbnail");
	CHECK(CFG_getGameArtStyle() == ART_STYLE_THUMBNAIL, "missing key loads as Thumbnail");

	// 2. Explicit parse of both values.
	write_settings(dir, "artStyle=1\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameArtStyle() == ART_STYLE_BACKGROUND, "artStyle=1 loads as Background");
	write_settings(dir, "artStyle=0\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameArtStyle() == ART_STYLE_THUMBNAIL, "artStyle=0 loads as Thumbnail");

	// 3. Setter persists + clamps, and the key lookup reports the live value.
	CFG_setGameArtStyle(ART_STYLE_BACKGROUND);
	CHECK(CFG_getGameArtStyle() == ART_STYLE_BACKGROUND, "setter flips in memory");
	CHECK(settings_contain(dir, "artStyle=1\n"), "setter writes artStyle=1");
	char value[64] = {0};
	CFG_get("artStyle", value);
	CHECK(strcmp(value, "1") == 0, "CFG_get(\"artStyle\") reports 1");
	CFG_setGameArtStyle(99); // out of range -> clamps to Background (1)
	CHECK(CFG_getGameArtStyle() == ART_STYLE_BACKGROUND, "setter clamps high to 1");
	CFG_setGameArtStyle(-5); // out of range -> clamps to Thumbnail (0)
	CHECK(CFG_getGameArtStyle() == ART_STYLE_THUMBNAIL, "setter clamps low to 0");
	CHECK(settings_contain(dir, "artStyle=0\n"), "setter writes artStyle=0");

	// 4. Reload from what was written.
	CFG_setGameArtStyle(ART_STYLE_BACKGROUND);
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameArtStyle() == ART_STYLE_BACKGROUND, "round-trip reload is Background");

	// 5. Game art type: default Mix, parse, clamp, persist.
	write_settings(dir, "artWidth=45\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_DEFAULT_GAMEARTTYPE == ART_TYPE_MIX, "default type is Mix");
	CHECK(CFG_getGameArtType() == ART_TYPE_MIX, "missing artType loads as Mix");
	write_settings(dir, "artType=2\n");
	CFG_init(NULL, NULL);
	CHECK(CFG_getGameArtType() == ART_TYPE_BOXART, "artType=2 loads as Box art");
	CFG_setGameArtType(ART_TYPE_SCREENSHOT);
	CHECK(settings_contain(dir, "artType=1\n"), "setter writes artType=1");
	CFG_setGameArtType(99);
	CHECK(CFG_getGameArtType() == ART_TYPE_BOXART, "type setter clamps high to 2");
	CFG_setGameArtType(-1);
	CHECK(CFG_getGameArtType() == ART_TYPE_MIX, "type setter clamps low to 0");

	// 6. The background style always shows the screenshot, whatever the type.
	CFG_setGameArtStyle(ART_STYLE_THUMBNAIL);
	CFG_setGameArtType(ART_TYPE_BOXART);
	CHECK(CFG_getEffectiveArtType() == ART_TYPE_BOXART,
		  "thumbnail style uses the chosen type");
	CFG_setGameArtStyle(ART_STYLE_BACKGROUND);
	CHECK(CFG_getEffectiveArtType() == ART_TYPE_SCREENSHOT,
		  "background style forces Screenshot");
	CFG_setGameArtType(ART_TYPE_MIX);
	CHECK(CFG_getEffectiveArtType() == ART_TYPE_SCREENSHOT,
		  "background style forces Screenshot over Mix too");

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
