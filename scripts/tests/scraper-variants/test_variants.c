// Host test for the Artwork Manager art variants: the variant-path helper
// (workspace/all/scraper/scraper_paths.c), the single-image compositor and
// its 256-colour save (workspace/all/scraper/scraper_compositor.c), and the
// "Optimize images" pass (workspace/all/scraper/scraper_optimize.c). Nothing
// is built for a device: the device TUs are compiled with the host SDL2 +
// SDL2_image and driven directly. The saved palette PNGs are read back with
// IMG_Load + SDL_ConvertSurfaceFormat, the way the menus load game art.
//
// SHARED_USERDATA_PATH resolves under the scratch card the test script bakes
// in with -DHOSTTEST_SDCARD (scripts/tests/hostplat/platform.h); the script
// passes that directory in.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "scraper_core.h"		// Scraper_variantPath
#include "scraper_compositor.h" // Compositor_createSingle, Compositor_savePNG
#include "scraper_optimize.h"
#include "png_palette.h"
#include "utils.h" // mkdir_p

// api.c (the logger) is not linked into this host test.
void LOG_note(int level, const char* fmt, ...) {
	(void)level;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

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

// A 32-bit PNG with a busy picture (far more than 256 colours) and, when
// `corners`, transparent corners: alpha 0 only (`soft` adds a translucent
// edge row too). Box art from ScreenScraper looks like this.
static bool make_art_png(const char* path, int w, int h, bool corners, bool soft) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return false;
	Uint32* px = s->pixels;
	unsigned seed = 7;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			seed = seed * 1103515245u + 12345u;
			Uint32 n = (seed >> 16) & 15;
			Uint32 a = 255;
			if (corners && (x < 8 || x >= w - 8) && (y < 8 || y >= h - 8))
				a = 0;
			if (soft && y == h - 1)
				a = 128;
			px[y * (s->pitch / 4) + x] = (a << 24) | (((x * 255 / w + n) & 255) << 16) |
										 (((y * 255 / h) & 255) << 8) | ((x ^ y) & 255);
		}
	int rc = IMG_SavePNG(s, path);
	SDL_FreeSurface(s);
	return rc == 0;
}

static long file_size(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// IMG_Load + ARGB8888 conversion (what nextui, Game Tracker and RA Tools do)
// and the alpha of pixel (x, y); -1 on a load failure.
static int loaded_alpha(const char* path, int x, int y, int* w, int* h) {
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return -1;
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!argb)
		return -1;
	*w = argb->w;
	*h = argb->h;
	Uint32 p = ((Uint32*)argb->pixels)[y * (argb->pitch / 4) + x];
	SDL_FreeSurface(argb);
	return (int)(p >> 24);
}

static bool in_list(const OptimizeList* l, const char* path) {
	for (int i = 0; i < l->count; i++)
		if (strcmp(l->paths[i], path) == 0)
			return true;
	return false;
}

static void write_byte(const char* path) {
	FILE* f = fopen(path, "wb");
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

	SDL_Init(0); // headless: no video, like run_headless_fetch

	// --- 1. Variant paths ---
	char vp[512];
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
	char land[512], port[512];
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

	// --- 3. Compositor_savePNG: a 256-colour PNG the menus load as before ---
	char art[512], saved[512];
	snprintf(art, sizeof(art), "%s/art.png", dir);
	snprintf(saved, sizeof(saved), "%s/out/boxart/art.png", dir);
	if (!make_art_png(art, 358, 480, true, false)) {
		fprintf(stderr, "could not generate the art PNG\n");
		return 2;
	}
	SDL_Surface* boxart = Compositor_createSingle(art);
	CHECK(boxart && Compositor_savePNG(boxart, saved), "savePNG writes the box art");
	if (boxart)
		SDL_FreeSurface(boxart);
	CHECK(PngPalette_isIndexedFile(saved), "savePNG output is an indexed (colour type 3) PNG");
	CHECK(file_size(saved) > 0 && file_size(saved) < file_size(art), "savePNG output is smaller than the 32-bit PNG");
	int lw = 0, lh = 0;
	CHECK(loaded_alpha(saved, 0, 0, &lw, &lh) == 0, "transparent corner loads with alpha 0 (tRNS single entry)");
	CHECK(lw == 358 && lh == 480, "saved art keeps its size");
	CHECK(loaded_alpha(saved, 100, 100, &lw, &lh) == 255, "picture pixels load opaque");
	char tmp_left[600];
	snprintf(tmp_left, sizeof(tmp_left), "%s.tmp", saved);
	CHECK(file_size(tmp_left) < 0, "no .tmp left behind");

	char soft[512], soft_saved[512];
	snprintf(soft, sizeof(soft), "%s/soft.png", dir);
	snprintf(soft_saved, sizeof(soft_saved), "%s/out/soft.png", dir);
	make_art_png(soft, 640, 480, true, true); // 640x480: createSingle keeps it 1:1
	SDL_Surface* soft_art = Compositor_createSingle(soft);
	CHECK(soft_art && Compositor_savePNG(soft_art, soft_saved), "savePNG writes art with translucent pixels");
	if (soft_art)
		SDL_FreeSurface(soft_art);
	int a_soft = loaded_alpha(soft_saved, 100, 479, &lw, &lh);
	CHECK(a_soft > 100 && a_soft < 160, "translucent pixels keep their alpha (tRNS expanded to RGBA)");
	CHECK(loaded_alpha(soft_saved, 0, 0, &lw, &lh) == 0, "and the transparent corner stays transparent");

	// --- 4. Optimize_collect: which files the pass touches ---
	char sys[600], p[900];
	snprintf(sys, sizeof(sys), "%s/Game Boy (GB)", ROMS_PATH);
	snprintf(p, sizeof(p), "%s/.media/screenshot", sys);
	mkdir_p(p);
	snprintf(p, sizeof(p), "%s/.media/boxart", sys);
	mkdir_p(p);
	snprintf(p, sizeof(p), "%s/Hacks/.media", sys);
	mkdir_p(p);
	const char* want[] = {"/.media/Game.png", "/.media/screenshot/Game.png", "/.media/boxart/Game.png",
						  "/Hacks/.media/Hack.png", "/.media/UPPER.PNG"};
	const char* skip[] = {"/.media/bg.png", "/.media/bglist.png", "/.media/bg-1024.png", "/.media/notes.txt",
						  "/.media/.hidden.png", "/.media/Game.png.tmp"};
	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", sys, want[i]);
		write_byte(p);
	}
	for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", sys, skip[i]);
		write_byte(p);
	}
	OptimizeList list;
	CHECK(Optimize_collect(ROMS_PATH, &list), "collect succeeds");
	bool all_wanted = true, none_skipped = true;
	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", sys, want[i]);
		all_wanted = all_wanted && in_list(&list, p);
	}
	for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", sys, skip[i]);
		none_skipped = none_skipped && !in_list(&list, p);
	}
	CHECK(all_wanted, "collect finds root, screenshot, boxart and nested .media art");
	CHECK(none_skipped, "collect leaves backgrounds, non-PNGs, hidden and tmp files out");
	CHECK(list.count == (int)(sizeof(want) / sizeof(want[0])), "collect lists nothing else");
	Optimize_freeList(&list);

	// --- 5. Optimize_file ---
	char game[900];
	snprintf(game, sizeof(game), "%s/.media/Game.png", sys);
	make_art_png(game, 640, 480, false, false);
	long before = file_size(game);
	long long saved_bytes = 0;
	CHECK(Optimize_file(game, &saved_bytes) == OPTIMIZE_DONE, "optimize converts a 32-bit PNG");
	long after = file_size(game);
	CHECK(after > 0 && after < before && saved_bytes == before - after, "optimize reports the bytes saved");
	CHECK(PngPalette_isIndexedFile(game), "optimized file is indexed");
	CHECK(loaded_alpha(game, 10, 10, &lw, &lh) == 255 && lw == 640 && lh == 480, "optimized file loads, same size");
	printf("     (640x480 busy art: %ld -> %ld bytes)\n", before, after);
	CHECK(Optimize_file(game, &saved_bytes) == OPTIMIZE_ALREADY, "a second pass skips it");

	snprintf(p, sizeof(p), "%s/.media/boxart/Game.png", sys); // the 1-byte placeholder: not a PNG
	CHECK(Optimize_file(p, &saved_bytes) == OPTIMIZE_FAILED, "an unreadable file fails");
	CHECK(file_size(p) == 1, "and is left as it was");

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
