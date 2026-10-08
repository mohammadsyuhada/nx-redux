// Host test for the Artwork Manager art variants: the variant-path helper
// (workspace/all/scraper/scraper_paths.c), the single-image compositor, the
// mix compositor, their 256-colour save and the full-colour (RGBA) save the
// mix, 2D box art and wheel use
// (workspace/all/scraper/scraper_compositor.c), and the
// "Optimize images" pass (workspace/all/scraper/scraper_optimize.c) and the
// per-.media "Reset artwork" delete (scraper_paths.c). Nothing
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

#include "scraper_core.h"		// Scraper_variantPath, Scraper_deleteMediaArtwork, Scraper_deleteAllArtwork
#include "scraper_compositor.h" // Compositor_create(Single/UpTo), Compositor_savePNG(RGBA), Compositor_saveMix
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

// A w x h PNG filled with one opaque colour, except that when `half_clear`
// the right half is fully transparent (a wheel logo on a clear background).
static bool make_solid_png(const char* path, int w, int h, Uint8 r, Uint8 g, Uint8 b, bool half_clear) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return false;
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, r, g, b, 255));
	if (half_clear) {
		SDL_Rect right = {w / 2, 0, w - w / 2, h};
		SDL_FillRect(s, &right, SDL_MapRGBA(s->format, 0, 0, 0, 0));
	}
	int rc = IMG_SavePNG(s, path);
	SDL_FreeSurface(s);
	return rc == 0;
}

// ARGB8888 pixel (x, y) of a surface already in that format.
static Uint32 pixel_at(SDL_Surface* s, int x, int y) {
	return ((Uint32*)s->pixels)[y * (s->pitch / 4) + x];
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
	Scraper_variantPath("/a/b/.media/Game.png", "mix", vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/mix/Game.png") == 0, "variantPath mix splices the subfolder");
	Scraper_variantPath("/a/b/.media/Game.png", NULL, vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/Game.png") == 0, "variantPath NULL -> the base path");
	Scraper_variantPath("/a/b/.media/Game.png", "", vp, sizeof(vp));
	CHECK(strcmp(vp, "/a/b/.media/Game.png") == 0, "variantPath \"\" -> the base path");
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

	// --- 3b. Compositor_create: the mix ---
	// Inputs: an opaque 320x240 screenshot (blue), a 200x280 box (red) and a
	// 300x100 wheel (green, right half transparent). On the 384x288 canvas (the
	// List's fitted box width, 4:3) the screenshot fills the 338x254 inset at
	// x 23, the box fits in 192x144 (102x143..144 at 7 px from the left and
	// bottom) and the wheel in 192x86 (192x64 at 185,217).
	char mss[512], mbox[512], mwheel[512];
	snprintf(mss, sizeof(mss), "%s/mix_ss.png", dir);
	snprintf(mbox, sizeof(mbox), "%s/mix_box.png", dir);
	snprintf(mwheel, sizeof(mwheel), "%s/mix_wheel.png", dir);
	if (!make_solid_png(mss, 320, 240, 0, 0, 255, false) || !make_solid_png(mbox, 200, 280, 255, 0, 0, false) ||
		!make_solid_png(mwheel, 300, 100, 0, 255, 0, true)) {
		fprintf(stderr, "could not generate the mix input PNGs\n");
		return 2;
	}
	const int pad = 7, box_w = 102, box_h = 143, wheel_x = 185, wheel_y = 217, wheel_w = 192, wheel_h = 64;
	const int box_cx = pad + box_w / 2, box_cy = 288 - pad - box_h / 2;
	const int wheel_cx = wheel_x + wheel_w / 4, wheel_cy = wheel_y + wheel_h / 2;
	SDL_Surface* mixs = Compositor_create(mss, mbox, mwheel);
	CHECK(mixs && mixs->w == 384 && mixs->h == 288, "create(ss, box, wheel) -> 384x288");
	if (mixs) {
		CHECK((pixel_at(mixs, 5, 5) >> 24) == 0, "mix background outside the screenshot inset is transparent");
		CHECK((pixel_at(mixs, 22, 40) >> 24) == 0 && pixel_at(mixs, 23, 40) == 0xFF0000FF &&
				  pixel_at(mixs, 360, 40) == 0xFF0000FF && (pixel_at(mixs, 361, 40) >> 24) == 0,
			  "screenshot inset is 6% in from each side (x 23..360)");
		CHECK(pixel_at(mixs, 150, 253) == 0xFF0000FF && (pixel_at(mixs, 150, 254) >> 24) == 0,
			  "screenshot inset leaves a 12% bottom gap (ends at y 254)");
		CHECK(pixel_at(mixs, box_cx, box_cy) == 0xFFFF0000, "box art sits bottom-left, opaque");
		CHECK(pixel_at(mixs, pad, 288 - pad - 1) == 0xFFFF0000 && (pixel_at(mixs, pad - 1, 280) >> 24) == 0 &&
				  (pixel_at(mixs, pad + 10, 288 - pad + 2) >> 24) == 0,
			  "box art is 7 px in from the left and bottom");
		// the drop shadows (2 px under the box, 1 px under the wheel) still show at this size
		Uint32 box_sh = pixel_at(mixs, pad + box_w + 1, 288 - pad - 10);
		CHECK((box_sh >> 24) > 0 && (box_sh & 0xFFFFFF) == 0, "box art casts a visible 2 px shadow");
		Uint32 wheel_sh = pixel_at(mixs, wheel_x + 10, wheel_y + wheel_h);
		CHECK((wheel_sh >> 24) > 0 && (wheel_sh & 0xFFFFFF) == 0, "wheel casts a visible 1 px shadow");
		CHECK(pixel_at(mixs, wheel_cx, wheel_cy) == 0xFF00FF00, "wheel sits bottom-right, opaque where the logo is");
		CHECK(pixel_at(mixs, 192, 40) == 0xFF0000FF, "screenshot fills the inset");
		SDL_FreeSurface(mixs);
	}

	SDL_Surface* no_ss = Compositor_create(NULL, mbox, mwheel);
	CHECK(no_ss && pixel_at(no_ss, 192, 40) >> 24 == 0 && pixel_at(no_ss, box_cx, box_cy) == 0xFFFF0000,
		  "create without a screenshot: clear top, box still drawn");
	if (no_ss)
		SDL_FreeSurface(no_ss);
	SDL_Surface* no_box = Compositor_create(mss, NULL, mwheel);
	CHECK(no_box && pixel_at(no_box, box_cx, box_cy) == 0xFF0000FF && pixel_at(no_box, wheel_cx, wheel_cy) == 0xFF00FF00,
		  "create without box art: screenshot shows where the box was, wheel drawn");
	if (no_box)
		SDL_FreeSurface(no_box);
	SDL_Surface* no_wheel = Compositor_create(mss, mbox, NULL);
	CHECK(no_wheel && pixel_at(no_wheel, wheel_cx, wheel_cy) == 0xFF0000FF,
		  "create without a wheel: screenshot shows where the wheel was");
	if (no_wheel)
		SDL_FreeSurface(no_wheel);
	SDL_Surface* none = Compositor_create(NULL, NULL, NULL);
	CHECK(none == NULL, "create with no layers -> NULL");
	if (none)
		SDL_FreeSurface(none);

	// A 160x144 Game Boy screenshot grows ~2.1x into the inset: 2x nearest,
	// then a short area step, so every source pixel stays a hard-edged block (a
	// 2-colour checkerboard; the output pixel at each source pixel's centre is
	// exactly its colour, never a blend).
	char gb[512];
	snprintf(gb, sizeof(gb), "%s/gb.png", dir);
	{
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 160, 144, 32, SDL_PIXELFORMAT_ARGB8888);
		for (int y = 0; y < 144; y++)
			for (int x = 0; x < 160; x++)
				((Uint32*)s->pixels)[y * (s->pitch / 4) + x] = ((x + y) & 1) ? 0xFFF0E000 : 0xFF102030;
		IMG_SavePNG(s, gb);
		SDL_FreeSurface(s);
	}
	SDL_Surface* gbmix = Compositor_create(gb, NULL, NULL);
	bool crisp = gbmix != NULL;
	int checked = 0;
	// fill: 160x144 -> 338x304 (x2.1125), centre-cropped to 254 rows (25 off the top)
	const double gsx = 338.0 / 160, gsy = 304.0 / 144;
	for (int j = 0; crisp && j < 144; j++) {
		int cy = (int)((j + 0.5) * gsy) - 25;
		if (cy < 0 || cy > 253)
			continue;
		for (int i = 0; i < 160; i++) {
			int cx = 23 + (int)((i + 0.5) * gsx);
			Uint32 want = ((i + j) & 1) ? 0xFFF0E000 : 0xFF102030;
			if (pixel_at(gbmix, cx, cy) != want)
				crisp = false;
			checked++;
		}
	}
	CHECK(crisp && checked > 100 * 160, "a 160x144 screenshot keeps hard pixel edges in the mix");
	if (gbmix)
		SDL_FreeSurface(gbmix);

	// A big screenshot shrinks with an area filter: a 1-px checkerboard at 1/2
	// averages to one flat colour instead of nearest-neighbour picking one.
	char big[512];
	snprintf(big, sizeof(big), "%s/big.png", dir);
	{
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 1804, 1352, 32, SDL_PIXELFORMAT_ARGB8888);
		for (int y = 0; y < 1352; y++)
			for (int x = 0; x < 1804; x++)
				((Uint32*)s->pixels)[y * (s->pitch / 4) + x] = ((x + y) & 1) ? 0xFFFFFFFF : 0xFF000000;
		IMG_SavePNG(s, big);
		SDL_FreeSurface(s);
	}
	SDL_Surface* bigmix = Compositor_create(big, NULL, NULL);
	Uint32 grey = bigmix ? pixel_at(bigmix, 192, 120) : 0;
	int gr = (grey >> 16) & 0xFF;
	CHECK(bigmix && (grey >> 24) == 0xFF && gr > 110 && gr < 145, "a large screenshot shrinks with an area filter");
	if (bigmix)
		SDL_FreeSurface(bigmix);

	// --- 3c. Compositor_saveMix: full-colour RGBA under .media/mix/ ---
	char media[512], out_png[600], mix_at[700];
	snprintf(media, sizeof(media), "%s/media/.media", dir);
	snprintf(out_png, sizeof(out_png), "%s/Game.png", media);
	snprintf(mix_at, sizeof(mix_at), "%s/mix/Game.png", media);
	CHECK(Compositor_saveMix(mss, mbox, mwheel, out_png), "saveMix builds and saves the mix");
	CHECK(file_size(mix_at) > 0, "the mix is written to .media/mix/");
	CHECK(file_size(out_png) < 0, "and not to the root .media/<name>.png");
	CHECK(!PngPalette_isIndexedFile(mix_at), "the saved mix is not a 256-colour PNG");
	{
		SDL_Surface* raw = IMG_Load(mix_at);
		CHECK(raw && raw->format->BitsPerPixel == 32 && raw->format->Amask != 0 && !raw->format->palette &&
				  raw->w == 384 && raw->h == 288,
			  "the saved mix reloads as 384x288 32-bit with alpha");
		if (raw)
			SDL_FreeSurface(raw);
	}
	CHECK(loaded_alpha(mix_at, 5, 5, &lw, &lh) == 0, "saved mix: corner keeps alpha 0");
	CHECK(loaded_alpha(mix_at, box_cx, box_cy, &lw, &lh) == 255, "saved mix: box art opaque");
	CHECK(loaded_alpha(mix_at, wheel_x + wheel_w * 3 / 4, 280, &lw, &lh) == 0,
		  "saved mix: the wheel's clear half keeps alpha 0");
	snprintf(tmp_left, sizeof(tmp_left), "%s.tmp", mix_at);
	CHECK(file_size(tmp_left) < 0, "saveMix leaves no .tmp behind");
	CHECK(!Compositor_saveMix(NULL, NULL, NULL, out_png), "saveMix with no layers fails");

	// --- 3d. Compositor_createUpTo: 2D box art and wheel, full colour, capped at 480x576 (Backdrop slot + List box) ---
	CHECK(COMPOSITOR_LIST_ART_MAX_W == 480 && COMPOSITOR_LIST_ART_MAX_H == 576,
		  "2D box art and wheel are stored within 480x576 (Backdrop slot + List box)");
	const int lw_max = COMPOSITOR_LIST_ART_MAX_W, lh_max = COMPOSITOR_LIST_ART_MAX_H;
	char huge[512], tall[512];
	snprintf(huge, sizeof(huge), "%s/huge.png", dir);
	make_png(huge, 2048, 1200);
	SDL_Surface* upto_big = Compositor_createUpTo(huge, lw_max, lh_max);
	CHECK(upto_big && upto_big->w == 480 && upto_big->h == 281, "createUpTo 2048x1200 -> 480x281 (width-bound)");
	if (upto_big)
		SDL_FreeSurface(upto_big);
	snprintf(tall, sizeof(tall), "%s/tall.png", dir);
	make_png(tall, 600, 1200);
	SDL_Surface* upto_tall = Compositor_createUpTo(tall, lw_max, lh_max);
	CHECK(upto_tall && upto_tall->w == 288 && upto_tall->h == 576, "createUpTo 600x1200 -> 288x576 (height-bound)");
	if (upto_tall)
		SDL_FreeSurface(upto_tall);
	SDL_Surface* upto_land = Compositor_createUpTo(land, lw_max, lh_max);
	CHECK(upto_land && upto_land->w == 480 && upto_land->h == 360, "createUpTo 800x600 -> 480x360");
	if (upto_land)
		SDL_FreeSurface(upto_land);
	CHECK(Compositor_createUpTo("/no/such/file.png", lw_max, lh_max) == NULL, "createUpTo nonexistent -> NULL");

	char wheel_saved[512];
	snprintf(wheel_saved, sizeof(wheel_saved), "%s/out/wheel/w.png", dir);
	SDL_Surface* wheel_var = Compositor_createUpTo(mwheel, lw_max, lh_max); // 300x100 stays as is (no upscale)
	CHECK(wheel_var && Compositor_savePNGRGBA(wheel_var, wheel_saved), "savePNGRGBA writes the wheel variant");
	if (wheel_var)
		SDL_FreeSurface(wheel_var);
	CHECK(!PngPalette_isIndexedFile(wheel_saved), "saved wheel variant is full colour");
	CHECK(loaded_alpha(wheel_saved, 225, 50, &lw, &lh) == 0 && lw == 300 && lh == 100,
		  "saved wheel variant: clear half keeps alpha 0");
	CHECK(loaded_alpha(wheel_saved, 75, 50, &lw, &lh) == 255, "saved wheel variant: logo opaque");

	// --- 4. Optimize_collect: which files the pass touches ---
	char sys[600], p[900];
	snprintf(sys, sizeof(sys), "%s/Game Boy (GB)", ROMS_PATH);
	snprintf(p, sizeof(p), "%s/.media/screenshot", sys);
	mkdir_p(p);
	snprintf(p, sizeof(p), "%s/.media/boxart", sys);
	mkdir_p(p);
	snprintf(p, sizeof(p), "%s/Hacks/.media/screenshot", sys);
	mkdir_p(p);
	const char* kept_full[] = {"boxart2d", "wheel", "mix"}; // folders made here; only boxart2d is optimized
	for (size_t i = 0; i < sizeof(kept_full) / sizeof(kept_full[0]); i++) {
		snprintf(p, sizeof(p), "%s/.media/%s", sys, kept_full[i]);
		mkdir_p(p);
	}
	// The root .media/<name>.png is the user's own picture (Ports, hand-made art): never optimized.
	const char* want[] = {"/.media/screenshot/Game.png", "/.media/boxart/Game.png",
						  "/Hacks/.media/screenshot/Hack.png", "/.media/boxart/UPPER.PNG", "/.media/boxart2d/Game.png"};
	const char* skip[] = {"/.media/Game.png", "/Hacks/.media/Hack.png", "/.media/UPPER.PNG",
						  "/.media/bg.png", "/.media/bglist.png", "/.media/bg-1024.png", "/.media/notes.txt",
						  "/.media/screenshot/.hidden.png", "/.media/screenshot/Game.png.tmp",
						  "/.media/wheel/Game.png", "/.media/mix/Game.png"};
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
	CHECK(all_wanted, "collect finds screenshot, boxart and boxart2d art, nested game folders too");
	CHECK(none_skipped, "collect leaves the root .media pictures, backgrounds, non-PNGs, hidden and tmp files, and the full-colour wheel and mix out");
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

	// --- 6. Scraper_deleteMediaArtwork (Reset artwork, per .media) ---
	char reset_media[700];
	snprintf(reset_media, sizeof(reset_media), "%s/Reset (RS)/.media", ROMS_PATH);
	const char* variants[] = {"screenshot", "boxart", "boxart2d", "wheel", "mix"};
	for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		snprintf(p, sizeof(p), "%s/%s", reset_media, variants[i]);
		mkdir_p(p);
		snprintf(p, sizeof(p), "%s/%s/Game.png", reset_media, variants[i]);
		write_byte(p);
	}
	snprintf(p, sizeof(p), "%s/boxart/Other.png", reset_media);
	write_byte(p);
	snprintf(p, sizeof(p), "%s/wheel/notes.txt", reset_media); // not a PNG: kept, so wheel/ stays
	write_byte(p);
	const char* root_kept[] = {"Game.png", "bg.png", "bglist.png"};
	for (size_t i = 0; i < sizeof(root_kept) / sizeof(root_kept[0]); i++) {
		snprintf(p, sizeof(p), "%s/%s", reset_media, root_kept[i]);
		write_byte(p);
	}
	CHECK(Scraper_deleteMediaArtwork(reset_media) == 6, "reset deletes every variant folder's PNGs");
	bool roots_kept = true;
	for (size_t i = 0; i < sizeof(root_kept) / sizeof(root_kept[0]); i++) {
		snprintf(p, sizeof(p), "%s/%s", reset_media, root_kept[i]);
		roots_kept = roots_kept && file_size(p) == 1;
	}
	CHECK(roots_kept, "reset keeps the root .media/Game.png, bg.png and bglist.png");
	bool emptied_gone = true;
	const char* emptied[] = {"screenshot", "boxart", "boxart2d", "mix"};
	for (size_t i = 0; i < sizeof(emptied) / sizeof(emptied[0]); i++) {
		snprintf(p, sizeof(p), "%s/%s", reset_media, emptied[i]);
		struct stat st;
		emptied_gone = emptied_gone && stat(p, &st) != 0;
	}
	CHECK(emptied_gone, "reset removes the emptied variant folders");
	snprintf(p, sizeof(p), "%s/wheel/notes.txt", reset_media);
	CHECK(file_size(p) == 1, "reset keeps non-PNG files, and their folder");
	snprintf(p, sizeof(p), "%s/Missing (MS)/.media", ROMS_PATH);
	CHECK(Scraper_deleteMediaArtwork(p) == 0, "reset of a missing .media deletes nothing");

	// --- 7. Scraper_deleteAllArtwork (Reset artwork, every Roms folder, nested game folders too) ---
	char all_root[700];
	snprintf(all_root, sizeof(all_root), "%s/resetall", dir);
	const char* all_deleted[] = {"/GB/.media/screenshot/Game.png", "/GB/Hacks/.media/screenshot/x.png",
								 "/GB/Hacks/Deep/.media/mix/y.png"};
	const char* all_kept[] = {"/GB/.media/Game.png", "/GB/Hacks/.media/Hack.png", "/GB/Hacks/.media/bg.png",
							  "/GB/.hidden/.media/screenshot/z.png"};
	for (size_t i = 0; i < sizeof(all_deleted) / sizeof(all_deleted[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", all_root, all_deleted[i]);
		*strrchr(p, '/') = '\0';
		mkdir_p(p);
		snprintf(p, sizeof(p), "%s%s", all_root, all_deleted[i]);
		write_byte(p);
	}
	for (size_t i = 0; i < sizeof(all_kept) / sizeof(all_kept[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", all_root, all_kept[i]);
		*strrchr(p, '/') = '\0';
		mkdir_p(p);
		snprintf(p, sizeof(p), "%s%s", all_root, all_kept[i]);
		write_byte(p);
	}
	CHECK(Scraper_deleteAllArtwork(all_root) == 3, "reset all deletes the variant PNGs at every depth");
	bool all_gone = true, all_kept_ok = true;
	for (size_t i = 0; i < sizeof(all_deleted) / sizeof(all_deleted[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", all_root, all_deleted[i]);
		all_gone = all_gone && file_size(p) < 0;
	}
	for (size_t i = 0; i < sizeof(all_kept) / sizeof(all_kept[0]); i++) {
		snprintf(p, sizeof(p), "%s%s", all_root, all_kept[i]);
		all_kept_ok = all_kept_ok && file_size(p) == 1;
	}
	CHECK(all_gone, "reset all clears a nested game folder's .media/screenshot/ (and deeper)");
	CHECK(all_kept_ok, "reset all keeps the root .media pictures at any depth, and skips hidden folders");
	snprintf(p, sizeof(p), "%s/missing", dir);
	CHECK(Scraper_deleteAllArtwork(p) == 0, "reset all of a missing Roms folder deletes nothing");

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
