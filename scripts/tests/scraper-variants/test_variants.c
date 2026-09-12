// Host test for the Artwork Manager art variants: the variant-path helper
// (workspace/all/scraper/scraper_paths.c) and the single-image compositor
// (workspace/all/scraper/scraper_compositor.c). Nothing is built for a device:
// the two device TUs are compiled with the host SDL2 + SDL2_image and driven
// directly.
//
// SHARED_USERDATA_PATH is the PATHS_SHARED_USERDATA runtime array under
// HAS_RUNTIME_PATHS (paths.c, linked in); main() points it at the scratch
// directory this test is handed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "scraper_core.h"		// Scraper_variantPath
#include "scraper_compositor.h" // Compositor_create, Compositor_createSingle
#include "paths.h"				// PATHS_SHARED_USERDATA

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

// Write a solid-colour PNG so the compositor has real image dimensions to work
// with; no fixtures needed on disk.
static bool make_png(const char* path, int w, int h) {
	SDL_Surface* s = SDL_CreateRGBSurface(0, w, h, 32,
										  0x00FF0000, 0x0000FF00,
										  0x000000FF, 0xFF000000);
	if (!s)
		return false;
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 40, 120, 200, 255));
	int rc = IMG_SavePNG(s, path);
	SDL_FreeSurface(s);
	return rc == 0;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <tmpdir>\n", argv[0]);
		return 2;
	}
	const char* dir = argv[1];
	snprintf(PATHS_SHARED_USERDATA, sizeof(PATHS_SHARED_USERDATA), "%s", dir);

	SDL_Init(0); // headless: no video, like run_headless_fetch

	// --- 1. Variant paths ---
	char vp[PATHS_MAX];
	Scraper_variantPath("/a/b/.media/Game Name.png", "screenshot", vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/screenshot/Game Name.png") == 0,
		  "variantPath screenshot splices the subfolder");
	Scraper_variantPath("/a/b/.media/Game Name.png", "boxart", vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/boxart/Game Name.png") == 0,
		  "variantPath boxart splices the subfolder");
	Scraper_variantPath("/a/b/.media/Game.png", NULL, vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/Game.png") == 0, "variantPath NULL -> mix path");
	Scraper_variantPath("/a/b/.media/Game.png", "", vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/Game.png") == 0, "variantPath \"\" -> mix path");
	Scraper_variantPath("Game.png", "screenshot", vp, sizeof(vp));
	CHECK(strcmp(vp, "screenshot/Game.png") == 0, "variantPath without a slash");

	// --- 2. Compositor_createSingle ---
	char land[PATHS_MAX], port[PATHS_MAX];
	snprintf(land, sizeof(land), "%s/land.png", dir);
	snprintf(port, sizeof(port), "%s/port.png", dir);
	if (!make_png(land, 800, 600) || !make_png(port, 300, 600)) {
		fprintf(stderr, "could not generate input PNGs (SDL2_image?)\n");
		return 2;
	}

	SDL_Surface* single_land = Compositor_createSingle(land);
	CHECK(single_land && single_land->w == 640 && single_land->h == 480,
		  "createSingle 800x600 -> 640x480 (fit, no padding)");
	if (single_land)
		SDL_FreeSurface(single_land);

	SDL_Surface* single_port = Compositor_createSingle(port);
	CHECK(single_port && single_port->w == 240 && single_port->h == 480,
		  "createSingle 300x600 -> 240x480 (aspect preserved)");
	if (single_port)
		SDL_FreeSurface(single_port);

	SDL_Surface* single_missing = Compositor_createSingle("/no/such/file.png");
	CHECK(single_missing == NULL, "createSingle nonexistent -> NULL");
	if (single_missing)
		SDL_FreeSurface(single_missing);

	// --- 3. Mix compositor unchanged (always the padded 640x480 canvas) ---
	SDL_Surface* mix = Compositor_create(land, NULL, NULL);
	CHECK(mix && mix->w == 640 && mix->h == 480, "Compositor_create -> 640x480");
	if (mix)
		SDL_FreeSurface(mix);

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
