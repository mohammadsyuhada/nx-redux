// Host test for the game-art variant path resolution in
// workspace/all/common/utils.c: ROM_mediaArtVariantPath splices the variant
// subfolder into the .media path, and ROM_displayArtPath either falls back to
// the root mix composite when the requested variant was never written (older
// libraries, or a game ScreenScraper had no such image for) or, with the
// fallback off (the background art style), leaves the missing variant path in
// place so the caller draws nothing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "utils.h"

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

static void write_stub(const char* path) {
	FILE* f = fopen(path, "w");
	if (f) {
		fputs("x", f);
		fclose(f);
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <tmpdir>\n", argv[0]);
		return 2;
	}
	const char* dir = argv[1];

	// <dir>/.media/G.png and <dir>/.media/screenshot/G.png exist; boxart does not.
	char media[512], shot_dir[512], mix[512], shot[512], rom[512], out[512];
	snprintf(media, sizeof(media), "%s/.media", dir);
	snprintf(shot_dir, sizeof(shot_dir), "%s/screenshot", media);
	mkdir(media, 0755);
	mkdir(shot_dir, 0755);
	snprintf(mix, sizeof(mix), "%s/G.png", media);
	snprintf(shot, sizeof(shot), "%s/G.png", shot_dir);
	write_stub(mix);
	write_stub(shot);
	snprintf(rom, sizeof(rom), "%s/G.gba", dir);

	ROM_mediaArtVariantPath(rom, "boxart", out, sizeof(out));
	char want[512];
	snprintf(want, sizeof(want), "%s/boxart/G.png", media);
	CHECK(strcmp(out, want) == 0, "variant path points into .media/boxart");

	ROM_mediaArtVariantPath(rom, NULL, out, sizeof(out));
	CHECK(strcmp(out, mix) == 0, "NULL variant is the root mix path");

	ROM_displayArtPath(rom, 1, true, out, sizeof(out)); // ART_TYPE_SCREENSHOT
	CHECK(strcmp(out, shot) == 0, "screenshot type resolves to the variant file");

	ROM_displayArtPath(rom, 2, true, out, sizeof(out)); // ART_TYPE_BOXART, missing
	CHECK(strcmp(out, mix) == 0, "missing box art falls back to the mix file");

	ROM_displayArtPath(rom, 0, true, out, sizeof(out)); // ART_TYPE_MIX
	CHECK(strcmp(out, mix) == 0, "mix type resolves to the root file");

	// Fallback off (background style): the variant path stands even when the
	// file is absent, so the list shows no art instead of the mix composite.
	ROM_displayArtPath(rom, 1, false, out, sizeof(out));
	CHECK(strcmp(out, shot) == 0, "no-fallback: present screenshot still resolves");

	char want_box[512];
	snprintf(want_box, sizeof(want_box), "%s/boxart/G.png", media);
	ROM_displayArtPath(rom, 2, false, out, sizeof(out));
	CHECK(strcmp(out, want_box) == 0, "no-fallback: missing variant stays missing");

	// A game with no art at all resolves to its (absent) variant path.
	char rom2[512], want_shot2[512];
	snprintf(rom2, sizeof(rom2), "%s/H.gba", dir);
	snprintf(want_shot2, sizeof(want_shot2), "%s/screenshot/H.png", media);
	ROM_displayArtPath(rom2, 1, false, out, sizeof(out));
	CHECK(strcmp(out, want_shot2) == 0, "no-fallback: unscraped game stays missing");
	ROM_displayArtPath(rom2, 1, true, out, sizeof(out));
	char want_mix2[512];
	snprintf(want_mix2, sizeof(want_mix2), "%s/H.png", media);
	CHECK(strcmp(out, want_mix2) == 0, "fallback: unscraped game falls back to mix path");

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
