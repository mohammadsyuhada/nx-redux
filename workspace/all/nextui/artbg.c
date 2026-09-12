// Self-contained (SDL + libm only) compositor for the "Background" game-art
// style: scale the art to full screen height, right-align it, and fade its left
// edge into transparency along a diagonal so the game list stays readable over
// it. Kept free of api.h/config.h so it can run on the thumbnail worker thread
// and be unit-tested on the host against plain SDL2.
#include <math.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "artbg.h"

// ---------------------------------------------------------------------------
// Geometry tunables. Every value is a fraction of the screen (user-approved,
// to be tuned later) so the look scales across resolutions. The fade boundary
// is a diagonal line that leans left toward the bottom of the screen.
// ---------------------------------------------------------------------------

// Fade-boundary positions, measured IN FROM THE RIGHT EDGE: the diagonal runs
// from 40% in at the top row to 60% in at the bottom row, so it leans left
// going down. Fixed: "Game art width" sizes the thumbnail style only.
#define ART_BG_TOP_FROM_RIGHT 0.40f
#define ART_BG_BOTTOM_FROM_RIGHT 0.60f
// A wide (16:9) panel gets a narrower strip: the same percentages of a 1280px
// screen reach much further across than of a 1024px one, and the narrower box
// also leaves more of a stacked screenshot's top screen visible.
#define ART_BG_WIDE_ASPECT 1.5f // screen w/h at or above this counts as wide
#define ART_BG_TOP_FROM_RIGHT_WIDE 0.20f
#define ART_BG_BOTTOM_FROM_RIGHT_WIDE 0.40f
// Soft-edge width to the left of the boundary, as a fraction of width. Wide,
// so the image's faintest edge starts well before the boundary.
#define ART_BG_FEATHER 0.25f
// Fraction of the span from the boundary to the right screen edge over which
// opacity ramps from 0 up to full: the whole of it, so the image only reaches
// full strength at the screen edge and never steps off the background.
#define ART_BG_FADE_SPAN 1.00f
// A source taller than this (width / height) is treated as a stacked shot —
// a Nintendo DS screenshot is the two screens one above the other, 320x480.
// Fitting the whole thing to the screen height would squeeze both screens
// into a narrow strip, so such a source is scaled to the drawn width instead
// and only the top band that fills the screen height is kept. How much of the
// source that is follows from the screen: no fixed fraction is assumed.
#define ART_BG_TALL_ASPECT 1.0f
// Width of the local fade applied at the image's own left edge, as a fraction
// of the screen width. The diagonal ramp is usually near zero there, but a
// nearly-square source starts further right, where the ramp has risen enough
// for the image's edge to read as a cut; this hides it.
#define ART_BG_EDGE_FADE 0.06f
// How much of the drawn art hangs off the right edge of the screen. Pushing
// the image right brings its centre into the visible strip, so more of the
// screenshot reads, and guarantees the art covers the whole ramp (a 16:9
// panel would otherwise run out of image partway through the fade).
#define ART_BG_OVERFLOW 0.30f
// Peak opacity reached to the right of the ramp.
#define ART_BG_MAX_ALPHA 1.0f

static bool isWideScreen(int screen_w, int screen_h) {
	return screen_h > 0 && (float)screen_w >= ART_BG_WIDE_ASPECT * (float)screen_h;
}

// Fade boundary x, as a fraction of the screen width, at the top and bottom
// rows. Top is further right; the line between them is the diagonal.
static float topBoundary(int screen_w, int screen_h) {
	return 1.0f - (isWideScreen(screen_w, screen_h) ? ART_BG_TOP_FROM_RIGHT_WIDE
													: ART_BG_TOP_FROM_RIGHT);
}
static float bottomBoundary(int screen_w, int screen_h) {
	return 1.0f - (isWideScreen(screen_w, screen_h) ? ART_BG_BOTTOM_FROM_RIGHT_WIDE
													: ART_BG_BOTTOM_FROM_RIGHT);
}

// Leftmost column the fade can touch: the feathered bottom-row boundary. The
// ramp never starts left of it (see ramp_start), whatever the art does, so the
// composed surface always begins here and the renderer can recompute it.
int ArtBg_originX(int screen_w, int screen_h) {
	int x0 = (int)floorf((float)screen_w * bottomBoundary(screen_w, screen_h) -
						 (float)screen_w * ART_BG_FEATHER);
	if (x0 < 0)
		x0 = 0;
	return x0;
}

SDL_Surface* ArtBg_compose(SDL_Surface* art, int screen_w, int screen_h, Uint32 pixel_format) {
	if (!art || art->w <= 0 || art->h <= 0 || screen_w <= 0 || screen_h <= 0)
		return NULL;

	// Straight-alpha compositing by direct pixel writes needs a 32-bit format
	// with an 8-bit alpha channel; the screen format on target hardware is
	// ARGB8888.
	SDL_PixelFormat* fmt = SDL_AllocFormat(pixel_format);
	if (!fmt)
		return NULL;
	if (fmt->BytesPerPixel != 4 || fmt->Amask == 0) {
		SDL_FreeFormat(fmt);
		return NULL;
	}
	const Uint32 amask = fmt->Amask;
	const Uint32 rgbmask = ~amask;
	const Uint8 ashift = fmt->Ashift;
	SDL_FreeFormat(fmt);

	const int W = screen_w;
	const int H = screen_h;

	// A landscape image is scaled so its HEIGHT equals the screen height, aspect
	// kept and nothing cropped, then pushed right by ART_BG_OVERFLOW of its
	// width so more of its middle lands in the visible strip. Wherever that
	// leaves the image starting later than the nominal fade boundary, the fade
	// simply begins at the image's own left edge (see ramp_start below), so
	// there is never a hard vertical cut and the shift never has to be given up.
	const int x0 = ArtBg_originX(W, H);
	const int out_w = W - x0;
	if (out_w <= 0)
		return NULL;

	// Box the art is drawn into: the visible strip plus the overflow that hangs
	// off the right edge.
	int box_w = (int)((float)out_w / (1.0f - ART_BG_OVERFLOW) + 0.5f);
	if (box_w < 1)
		box_w = 1;

	SDL_Rect crop = {0, 0, art->w, art->h};
	int draw_w;
	if ((float)art->w < ART_BG_TALL_ASPECT * (float)art->h) {
		// Stacked source: scale it to the box width and keep the top band that
		// fills the screen height, so as much of the top screen as fits shows.
		int band = (int)((double)H * (double)art->w / (double)box_w + 0.5);
		if (band < 1)
			band = 1;
		// Never cross into the second screen: a few rows of it read as a seam.
		if (band > art->h / 2)
			band = art->h / 2;
		if (band < 1)
			band = 1;
		crop.h = band;
		draw_w = box_w;
	} else {
		// Landscape source: its height matches the screen, nothing cropped.
		draw_w = (int)((double)art->w * (double)H / (double)art->h + 0.5);
		if (draw_w < 1)
			draw_w = 1;
	}
	const int shift = (int)(ART_BG_OVERFLOW * (float)draw_w + 0.5f);
	const int draw_left = W - draw_w + shift;

	// Render the art at final scale into a matching-format buffer. Copy (blend
	// mode NONE) rather than blend so straight alpha is preserved instead of
	// being premultiplied onto the transparent destination.
	SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, draw_w, H, 32, pixel_format);
	if (!scaled)
		return NULL;
	SDL_BlendMode prev_blend;
	SDL_GetSurfaceBlendMode(art, &prev_blend);
	SDL_SetSurfaceBlendMode(art, SDL_BLENDMODE_NONE);
	SDL_BlitScaled(art, &crop, scaled, &(SDL_Rect){0, 0, draw_w, H});
	SDL_SetSurfaceBlendMode(art, prev_blend);

	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, out_w, H, 32, pixel_format);
	if (!out) {
		SDL_FreeSurface(scaled);
		return NULL;
	}
	SDL_FillRect(out, NULL, 0); // fully transparent ground

	if (SDL_LockSurface(scaled) != 0) {
		SDL_FreeSurface(scaled);
		SDL_FreeSurface(out);
		return NULL;
	}
	if (SDL_LockSurface(out) != 0) {
		SDL_UnlockSurface(scaled);
		SDL_FreeSurface(scaled);
		SDL_FreeSurface(out);
		return NULL;
	}

	const Uint32* src_px = (const Uint32*)scaled->pixels;
	Uint32* dst_px = (Uint32*)out->pixels;
	const int src_pitch = scaled->pitch / 4;
	const int draw_cols = scaled->w;
	const int dst_pitch = out->pitch / 4;

	const float Wf = (float)W;
	const float feather = ART_BG_FEATHER * Wf;
	const float edge_fade = ART_BG_EDGE_FADE * Wf > 1.0f ? ART_BG_EDGE_FADE * Wf : 1.0f;
	const float bottom_x = bottomBoundary(W, H);
	const float top_x = topBoundary(W, H);
	const float denom_y = (H > 1) ? (float)(H - 1) : 1.0f;

	for (int y = 0; y < H; y++) {
		// Diagonal boundary x for this row and the ramp geometry around it,
		// precomputed once per row (the inner loop only interpolates x).
		const float xb = Wf * (top_x + (bottom_x - top_x) * (float)y / denom_y);
		// The ramp is purely geometric: clamping its start to the image's edge
		// would flatten the diagonal on every row where the image begins later
		// (the lower rows on a wide panel), turning the fade vertical.
		const float ramp_start = xb - feather;
		float ramp_end = xb + ART_BG_FADE_SPAN * (Wf - xb);
		if (ramp_end <= ramp_start)
			ramp_end = ramp_start + 1.0f;
		const float ramp_len = ramp_end - ramp_start;

		const Uint32* src_row = src_px + (size_t)y * src_pitch;
		Uint32* dst_row = dst_px + (size_t)y * dst_pitch;

		for (int i = 0; i < out_w; i++) {
			const int screen_x = x0 + i;

			// Opacity from the diagonal fade: 0 left of the feathered boundary,
			// a smoothstep ramp up to ART_BG_MAX_ALPHA, then constant max.
			float opacity;
			if ((float)screen_x <= ramp_start) {
				continue; // stays transparent
			} else if ((float)screen_x >= ramp_end || ramp_len <= 0.0f) {
				opacity = ART_BG_MAX_ALPHA;
			} else {
				float f = ((float)screen_x - ramp_start) / ramp_len;
				// Smootherstep (Perlin): zero first AND second derivative at
				// both ends, so neither end of the fade shows a visible step.
				f = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
				// Squared again: the dark end then holds near zero for much
				// longer, which is what makes a bright screenshot blend into
				// the background instead of announcing where it starts.
				opacity = f * f * ART_BG_MAX_ALPHA;
			}

			// Columns past the screen edge are the part that hangs off and
			// is never sampled.
			const int art_col = screen_x - draw_left;
			if (art_col < 0 || art_col >= draw_cols)
				continue; // outside the art -> transparent

			// Local fade at the image's left edge (see ART_BG_EDGE_FADE).
			const float from_edge = (float)(screen_x - draw_left);
			if (from_edge < edge_fade) {
				float e = from_edge / edge_fade;
				e = e * e * (3.0f - 2.0f * e);
				opacity *= e;
			}

			const Uint32 s = src_row[art_col];
			const Uint32 sa = (s & amask) >> ashift; // 0..255 (Aloss == 0)
			const Uint32 na = (Uint32)((float)sa * opacity + 0.5f);
			if (na == 0)
				continue; // source transparent here -> leave ground transparent
			dst_row[i] = (s & rgbmask) | (na << ashift);
		}
	}

	SDL_UnlockSurface(out);
	SDL_UnlockSurface(scaled);
	SDL_FreeSurface(scaled);
	return out;
}
