// Host test for workspace/all/nextui/artbg.c (the "Background" game-art
// compositor). Pure SDL2 + libm; no device or GFX involved. Asserts the output
// geometry, right-alignment, the diagonal fade, monotonic opacity, and that
// straight alpha is preserved for a fully transparent source, and that the art
// covers the whole fade (no hard edge where the image runs out).
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "artbg.h"

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

static const Uint8 ART_R = 200, ART_G = 100, ART_B = 50;

// Fade boundaries, measured in from the right edge (artbg.c tunables). A wide
// (16:9) panel uses a narrower strip: 20% / 40% instead of 40% / 60%.
#define FEATHER 0.25f
static int is_wide(int w, int h) {
	return (float)w >= 1.5f * (float)h;
}
static float top_frac(int w, int h) {
	return 1.0f - (is_wide(w, h) ? 0.20f : 0.40f);
}
static float bot_frac(int w, int h) {
	return 1.0f - (is_wide(w, h) ? 0.40f : 0.60f);
}

static SDL_Surface* make_art(int w, int h, Uint8 a) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) {
		fprintf(stderr, "make_art: %s\n", SDL_GetError());
		exit(2);
	}
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, ART_R, ART_G, ART_B, a));
	return s;
}

// Alpha at a SCREEN column/row. The composed surface represents screen columns
// x0..W-1; columns left of x0 are transparent by construction (they are why x0
// was chosen there), so report 0 for them.
static Uint8 alpha_at(SDL_Surface* out, int x0, int screen_x, int y) {
	if (screen_x < x0)
		return 0;
	int col = screen_x - x0;
	if (col < 0 || col >= out->w || y < 0 || y >= out->h)
		return 0;
	Uint32 px = ((Uint32*)out->pixels)[(size_t)y * (out->pitch / 4) + col];
	Uint8 r, g, b, a;
	SDL_GetRGBA(px, out->format, &r, &g, &b, &a);
	return a;
}

static void color_at(SDL_Surface* out, int x0, int screen_x, int y,
					 Uint8* r, Uint8* g, Uint8* b, Uint8* a) {
	int col = screen_x - x0;
	Uint32 px = ((Uint32*)out->pixels)[(size_t)y * (out->pitch / 4) + col];
	SDL_GetRGBA(px, out->format, r, g, b, a);
}

int main(void) {
	const Uint32 FMT = SDL_PIXELFORMAT_ARGB8888;

	// 1. Output size == (W - x0) x H for two resolutions, opaque 640x480 art.
	int rw[2] = {1280, 1024};
	int rh[2] = {720, 768};
	for (int k = 0; k < 2; k++) {
		int W = rw[k], H = rh[k];
		SDL_Surface* art = make_art(640, 480, 255);
		SDL_Surface* out = ArtBg_compose(art, W, H, FMT);
		char msg[96];
		snprintf(msg, sizeof(msg), "%dx%d: compose returns a surface", W, H);
		CHECK(out != NULL, msg);
		if (out) {
			int x0 = ArtBg_originX(W, H);
			snprintf(msg, sizeof(msg), "%dx%d: output size is (W-x0)=%d x H=%d",
					 W, H, W - x0, H);
			CHECK(out->w == W - x0 && out->h == H, msg);
			SDL_FreeSurface(out);
		}
		SDL_FreeSurface(art);
	}

	// The remaining checks use the primary resolution.
	const int W = 1280, H = 720;
	const int x0 = ArtBg_originX(W, H);
	SDL_Surface* art = make_art(640, 480, 255);
	SDL_Surface* out = ArtBg_compose(art, W, H, FMT);
	if (!out) {
		printf("FAIL - compose returned NULL at primary resolution\n");
		SDL_FreeSurface(art);
		printf("FAILED\n");
		return 1;
	}
	SDL_LockSurface(out);

	// 2. Right-alignment: the rightmost column (output col W-1-x0) at row H/2 is
	// opaque and carries the source colour.
	{
		Uint8 a = alpha_at(out, x0, W - 1, H / 2);
		CHECK(a >= 250, "right edge (row H/2) is opaque (alpha >= 250)");
		Uint8 r, g, b, aa;
		color_at(out, x0, W - 1, H / 2, &r, &g, &b, &aa);
		CHECK(abs(r - ART_R) <= 4 && abs(g - ART_G) <= 4 && abs(b - ART_B) <= 4,
			  "right edge carries the source colour");
	}

	// 3. Diagonal fade. The bottom boundary sits at 1 - art_width, the top one
	// ART_BG_LEAN further right.
	{
		int top = 0, bot = H - 1;
		const float TOPF = top_frac(W, H), BOTF = bot_frac(W, H);
		int xa = (int)(TOPF * W) - (int)(FEATHER * W) - 2; // left of top boundary
		// Probe 70% of the way from each boundary to the right screen edge: the
		// ramp reaches full opacity at 85% of that span, so this is deep in it.
		int xc = (int)(TOPF * W + 0.70f * (1.0f - TOPF) * W); // right of top boundary
		int xb = (int)(BOTF * W + 0.70f * (1.0f - BOTF) * W); // right of bottom boundary
		int xd = (int)(BOTF * W) - (int)(FEATHER * W) - 2;	  // left of bottom boundary
		CHECK(alpha_at(out, x0, xa, top) == 0, "top row: left of boundary is alpha 0");
		CHECK(alpha_at(out, x0, xc, top) > 128, "top row: right of boundary is opaque-ish");
		CHECK(alpha_at(out, x0, xb, bot) > 128, "bottom row: right of boundary is opaque-ish");
		CHECK(alpha_at(out, x0, xd, bot) == 0, "bottom row: left of boundary is alpha 0");
	}

	// 4. Monotonic: alpha never decreases left to right along row H/2.
	{
		int prev = -1, ok = 1;
		for (int x = x0; x < W; x++) {
			int a = alpha_at(out, x0, x, H / 2);
			if (a < prev) {
				ok = 0;
				break;
			}
			prev = a;
		}
		CHECK(ok, "alpha is monotonic non-decreasing along row H/2");
	}

	SDL_UnlockSurface(out);
	SDL_FreeSurface(out);
	SDL_FreeSurface(art);

	// 5. Straight alpha: a fully transparent source yields all-transparent
	// output (the fade multiplies source alpha, it does not replace it).
	{
		SDL_Surface* art0 = make_art(640, 480, 0);
		SDL_Surface* out0 = ArtBg_compose(art0, W, H, FMT);
		CHECK(out0 != NULL, "transparent source: compose returns a surface");
		if (out0) {
			SDL_LockSurface(out0);
			int all_zero = 1;
			int pitch = out0->pitch / 4;
			Uint32* px = (Uint32*)out0->pixels;
			for (int y = 0; y < out0->h && all_zero; y++) {
				for (int x = 0; x < out0->w; x++) {
					Uint8 r, g, b, a;
					SDL_GetRGBA(px[(size_t)y * pitch + x], out0->format, &r, &g, &b, &a);
					if (a != 0) {
						all_zero = 0;
						break;
					}
				}
			}
			SDL_UnlockSurface(out0);
			CHECK(all_zero, "transparent source: every output pixel has alpha 0");
			SDL_FreeSurface(out0);
		}
		SDL_FreeSurface(art0);
	}

	// 6. No hard edge: once the fade starts painting on a row, every column to
	// its right is painted too (a 16:9 panel used to run out of image partway
	// through the ramp, leaving a visible vertical cut).
	{
		SDL_Surface* a2 = make_art(640, 480, 255);
		int fail_rows = 0;
		int wide_rw[2] = {1280, 1024};
		int wide_rh[2] = {720, 768};
		for (int k = 0; k < 2; k++) {
			int Wk = wide_rw[k], Hk = wide_rh[k];
			SDL_Surface* o = ArtBg_compose(a2, Wk, Hk, FMT);
			if (!o) {
				fail_rows++;
				continue;
			}
			SDL_LockSurface(o);
			int xk = ArtBg_originX(Wk, Hk);
			for (int y = 0; y < Hk; y += 37) {
				int started = 0;
				for (int x = xk; x < Wk; x++) {
					Uint8 a = alpha_at(o, xk, x, y);
					if (a > 0)
						started = 1;
					else if (started) { // went transparent again: a hard edge
						fail_rows++;
						break;
					}
				}
			}
			SDL_UnlockSurface(o);
			SDL_FreeSurface(o);
		}
		CHECK(fail_rows == 0, "art covers the whole fade on 16:9 and 4:3 (no hard edge)");
		SDL_FreeSurface(a2);
	}

	// 7. Height fit: the whole height of a landscape source is shown (no
	// vertical crop), so a marker row at the very top of the art appears in
	// the composed surface's top row.
	{
		SDL_Surface* a3 = make_art(640, 480, 255);
		SDL_LockSurface(a3);
		Uint32* p3 = (Uint32*)a3->pixels;
		int p3pitch = a3->pitch / 4;
		Uint32 marker = SDL_MapRGBA(a3->format, 10, 240, 20, 255);
		for (int x = 0; x < a3->w; x++) {
			p3[x] = marker;									// first source row
			p3[(size_t)(a3->h - 1) * p3pitch + x] = marker; // last source row
		}
		SDL_UnlockSurface(a3);

		int wide_rw[2] = {1280, 1024};
		int wide_rh[2] = {720, 768};
		int ok_rows = 0;
		for (int k = 0; k < 2; k++) {
			int Wk = wide_rw[k], Hk = wide_rh[k];
			SDL_Surface* o = ArtBg_compose(a3, Wk, Hk, FMT);
			if (!o)
				continue;
			SDL_LockSurface(o);
			int xk = ArtBg_originX(Wk, Hk);
			Uint8 r1, g1, b1, a1, r2, g2, b2, a2;
			color_at(o, xk, Wk - 1, 0, &r1, &g1, &b1, &a1);
			color_at(o, xk, Wk - 1, Hk - 1, &r2, &g2, &b2, &a2);
			if (abs(g1 - 240) <= 6 && abs(g2 - 240) <= 6)
				ok_rows++;
			SDL_UnlockSurface(o);
			SDL_FreeSurface(o);
		}
		CHECK(ok_rows == 2, "art height matches the screen (top and bottom rows kept)");
		SDL_FreeSurface(a3);
	}

	// 8. The full 30% shift is applied on both resolutions: a marker column at
	// the source's right edge lands 30% of the drawn width off screen, i.e.
	// the drawn image's right edge sits at W + 0.30 * drawn width.
	{
		int wide_rw[2] = {1280, 1024};
		int wide_rh[2] = {720, 768};
		int ok_res = 0;
		for (int k = 0; k < 2; k++) {
			int Wk = wide_rw[k], Hk = wide_rh[k];
			// Source with a distinct column 1/4 of the way in.
			SDL_Surface* a4 = make_art(640, 480, 255);
			SDL_LockSurface(a4);
			Uint32* p4 = (Uint32*)a4->pixels;
			int p4pitch = a4->pitch / 4;
			Uint32 mark = SDL_MapRGBA(a4->format, 5, 5, 250, 255);
			for (int y = 0; y < a4->h; y++)
				p4[(size_t)y * p4pitch + (a4->w / 4)] = mark;
			SDL_UnlockSurface(a4);

			SDL_Surface* o = ArtBg_compose(a4, Wk, Hk, FMT);
			if (o) {
				SDL_LockSurface(o);
				int xk = ArtBg_originX(Wk, Hk);
				// Expected geometry: height fit, then shifted right 30%.
				double drawn_w = 640.0 * (double)Hk / 480.0;
				double left = (double)Wk - drawn_w + 0.30 * drawn_w;
				int want = (int)(left + drawn_w * 0.25 + 0.5);
				int found = -1;
				for (int x = xk; x < Wk; x++) {
					Uint8 r, g, b, a;
					color_at(o, xk, x, Hk / 2, &r, &g, &b, &a);
					if (a > 0 && b > 200 && r < 80) {
						found = x;
						break;
					}
				}
				if (found >= 0 && abs(found - want) <= 3)
					ok_res++;
				SDL_UnlockSurface(o);
				SDL_FreeSurface(o);
			}
			SDL_FreeSurface(a4);
		}
		CHECK(ok_res == 2, "full 30% shift applied on both 16:9 and 4:3");
	}

	// 9. A stacked dual-screen source (DS: 320x480) contributes only its top
	// half: the bottom half's colour must not appear anywhere in the output.
	{
		SDL_Surface* ds = SDL_CreateRGBSurfaceWithFormat(0, 320, 480, 32, FMT);
		SDL_Rect top = {0, 0, 320, 240}, bottom = {0, 240, 320, 240};
		SDL_FillRect(ds, &top, SDL_MapRGBA(ds->format, 20, 200, 40, 255));
		SDL_FillRect(ds, &bottom, SDL_MapRGBA(ds->format, 200, 20, 40, 255));

		int seen_top = 0, seen_bottom = 0;
		SDL_Surface* o = ArtBg_compose(ds, 1280, 720, FMT);
		if (o) {
			SDL_LockSurface(o);
			int pitch = o->pitch / 4;
			Uint32* px = (Uint32*)o->pixels;
			for (int y = 0; y < o->h; y += 11) {
				for (int x = 0; x < o->w; x += 11) {
					Uint8 r, g, b, a;
					SDL_GetRGBA(px[(size_t)y * pitch + x], o->format, &r, &g, &b, &a);
					if (a < 200)
						continue;
					if (g > 150 && r < 100)
						seen_top = 1;
					if (r > 150 && g < 100)
						seen_bottom = 1;
				}
			}
			SDL_UnlockSurface(o);
			SDL_FreeSurface(o);
		}
		CHECK(seen_top && !seen_bottom, "stacked DS source uses its top screen only");
		SDL_FreeSurface(ds);
	}

	printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
