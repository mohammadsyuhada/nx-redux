// The shared main-menu tile painter (Grid and Carousel). Every shape is a cached white
// anti-aliased mask blitted with a colour/alpha mod, so drawing honours dst's clip rect and the
// plain/lit crossfade is only alpha and colour changes, no per-frame rasterising.

#include "tiles.h"

#include "api.h"
#include "defines.h"
#include "ui_text_sizes.h"
#include "grid_layout.h"
#include "infoband.h"
#include "menuart.h"
#include "row_model.h"
#include "ui_accent.h"
#include "ui_font.h"
#include "ui_fade.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define TILE_RADIUS_DP 14.0f
#define TILE_RING_DP 3.0f
#define TILE_BORDER_DP 1.0f
#define TILE_BORDER_ALPHA 20 // white at 8%
#define TILE_PLAIN_ALPHA 209 // content at 82% (100% lit)

#define LOGO_INSET_X_DP 22.0f
#define LOGO_INSET_Y_DP 30.0f
#define TOOL_ICON_DP 46.0f
#define TOOL_GAP_DP 12.0f
#define TOOL_PAD_DP 12.0f
#define TOOL_TEXT_SP 14.0f
#define TOOL_TEXT_MIN_SP 11.0f // the Grid's tool names shrink to fit, never below this
// the Carousel's tool variant (§8b.3): the icon at 30% of the tile height, the name 20 sp at the full 340 dp tile,
// both scaling with the tile; the name shrinks so "Achievements" fits whole
#define CAROUSEL_TOOL_ICON_SHARE 0.30f
#define CAROUSEL_TOOL_TEXT_SP 20.0f
#define CAROUSEL_TILE_W_SPEC 340.0f
#define TOOL_LONGEST_WORD "Achievements"
#define WORD_PAD_DP 14.0f
#define GRID_EMBLEM_DP 36.0f // a logo-less console's emblem over its name, × the tile scale
#define GRID_EMBLEM_GAP_DP 6.0f
#define WORD_TEXT_SP 17.0f
#define WORD_MAX_LINES 4

#define CAPTION_PAD_X_DP 12.0f
#define CAPTION_PAD_B_DP 10.0f
#define CAPTION_GAP_DP 3.0f
#define CAPTION_NAME_SP 14.0f
#define CAPTION_INFO_SP 11.0f
#define CAPTION_FADE_EDGE 0.85f
#define CAPTION_FADE_SHARE 0.55f
#define CAPTION_FADE_OVER_DP 48.0f	  // the fade starts at least this far over the label's top (× the tile scale)
#define CAPTION_FADE_OVER_LABELS 2.0f // ...and at least twice the label's height over it: a long, soft ramp
#define CAPTION_FADE_HOLD_PAD_DP 4.0f // ...and is at its darkest from this far over the label's top down

#define MAX_LINES 4
#define TEXT_SHADOW_ALPHA 153 // black at 60%
#define LINE_MAX 256

///////////////////////////////////////////////////////////////////////////////
// Shape masks: white ARGB with alpha = coverage of a rounded rect (FILL), or of the band between it and
// the same shape inset by `inset` px (OUTLINE).

typedef enum { SHAPE_FILL = 1,
			   SHAPE_OUTLINE } ShapeKind;

// 16: the Grid's tile plus the Carousel's two sizes (centre and side), each with its fill, outline and ring masks.
#define MASK_SLOTS 16
typedef struct {
	ShapeKind kind; // 0 = empty
	int w, h, rad, inset;
	SDL_Surface* surface;
	unsigned stamp;
} MaskSlot;

static MaskSlot masks[MASK_SLOTS];
static unsigned mask_clock = 0;

// Coverage (0..1) of pixel (px, py) by a w x h rounded rect at the origin with radius rad: its centre's
// distance to the inner (radius-shrunk) rect against the radius, with a 1 px ramp.
static float roundedCoverage(int px, int py, int w, int h, float rad) {
	if (w <= 0 || h <= 0)
		return 0.0f;
	float cx = px + 0.5f, cy = py + 0.5f;
	if (rad < 0.5f)
		return (cx > 0 && cy > 0 && cx < w && cy < h) ? 1.0f : 0.0f;
	float ix = cx < rad ? rad : (cx > w - rad ? w - rad : cx);
	float iy = cy < rad ? rad : (cy > h - rad ? h - rad : cy);
	float dx = cx - ix, dy = cy - iy;
	float cov = rad - sqrtf(dx * dx + dy * dy) + 0.5f;
	return cov <= 0.0f ? 0.0f : (cov >= 1.0f ? 1.0f : cov);
}

static int clampRadius(int rad, int w, int h) {
	int m = (w < h ? w : h) / 2;
	return rad < 0 ? 0 : (rad > m ? m : rad);
}

static SDL_Surface* buildMask(ShapeKind kind, int w, int h, int rad, int inset) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	if (SDL_MUSTLOCK(s))
		SDL_LockSurface(s);
	int iw = w - 2 * inset, ih = h - 2 * inset;
	int irad = clampRadius(rad - inset, iw, ih);
	for (int y = 0; y < h; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
		for (int x = 0; x < w; x++) {
			float cov = roundedCoverage(x, y, w, h, (float)rad);
			if (kind == SHAPE_OUTLINE && cov > 0.0f) {
				cov -= roundedCoverage(x - inset, y - inset, iw, ih, (float)irad);
				if (cov < 0.0f)
					cov = 0.0f;
			}
			Uint32 a = (Uint32)(cov * 255.0f + 0.5f);
			row[x] = (a << 24) | 0x00FFFFFF;
		}
	}
	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
	return s;
}

static SDL_Surface* getMask(ShapeKind kind, int w, int h, int rad, int inset) {
	rad = clampRadius(rad, w, h);
	for (int i = 0; i < MASK_SLOTS; i++) {
		MaskSlot* m = &masks[i];
		if (m->kind == kind && m->surface && m->w == w && m->h == h && m->rad == rad && m->inset == inset) {
			m->stamp = ++mask_clock;
			return m->surface;
		}
	}
	SDL_Surface* s = buildMask(kind, w, h, rad, inset);
	if (!s)
		return NULL;
	MaskSlot* victim = &masks[0];
	for (int i = 0; i < MASK_SLOTS; i++) {
		if (!masks[i].surface) {
			victim = &masks[i];
			break;
		}
		if (masks[i].stamp < victim->stamp)
			victim = &masks[i];
	}
	if (victim->surface)
		GFX_freeSurfaceAndTexture(victim->surface);
	*victim = (MaskSlot){kind, w, h, rad, inset, s, ++mask_clock};
	return s;
}

// A rounded rect (or its inset outline) in grey level c at alpha a.
// The white mask tinted to colour c at alpha a.
static void blitShapeColor(SDL_Surface* dst, ShapeKind kind, int x, int y, int w, int h, int rad, int inset,
						   SDL_Color c, Uint8 a) {
	if (w <= 0 || h <= 0 || a == 0)
		return;
	SDL_Surface* m = getMask(kind, w, h, rad, inset);
	if (!m)
		return;
	SDL_SetSurfaceColorMod(m, c.r, c.g, c.b);
	SDL_SetSurfaceAlphaMod(m, a);
	SDL_BlitSurface(m, NULL, dst, &(SDL_Rect){x, y, w, h});
}

// The white mask tinted to grey level c at alpha a.
static void blitShape(SDL_Surface* dst, ShapeKind kind, int x, int y, int w, int h, int rad, int inset, Uint8 c,
					  Uint8 a) {
	blitShapeColor(dst, kind, x, y, w, h, rad, inset, (SDL_Color){c, c, c, 255}, a);
}

///////////////////////////////////////////////////////////////////////////////
// The screenshot, copied onto an opaque black scratch tile with the rounded corners cut into its alpha.

static SDL_Surface* scratch = NULL;

// Scale an ARGB8888 surface's alpha by the rounded-rect coverage in its four rad x rad corners (the only
// place it can be partial).
static void cutCorners(SDL_Surface* s, int rad) {
	rad = clampRadius(rad, s->w, s->h);
	if (rad <= 0)
		return;
	if (SDL_MUSTLOCK(s))
		SDL_LockSurface(s);
	int xs[2] = {0, s->w - rad}, ys[2] = {0, s->h - rad};
	for (int cy = 0; cy < 2; cy++) {
		for (int cx = 0; cx < 2; cx++) {
			for (int y = ys[cy]; y < ys[cy] + rad; y++) {
				Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
				for (int x = xs[cx]; x < xs[cx] + rad; x++) {
					float cov = roundedCoverage(x, y, s->w, s->h, (float)rad);
					if (cov >= 1.0f)
						continue;
					Uint32 a = (Uint32)((row[x] >> 24) * cov + 0.5f);
					row[x] = (row[x] & 0x00FFFFFF) | (a << 24);
				}
			}
		}
	}
	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
}

static void drawPicture(SDL_Surface* dst, SDL_Rect r, int rad, SDL_Surface* pic) {
	if (!scratch || scratch->w != r.w || scratch->h != r.h) {
		if (scratch)
			GFX_freeSurfaceAndTexture(scratch);
		scratch = SDL_CreateRGBSurfaceWithFormat(0, r.w, r.h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!scratch)
			return;
	}
	// opaque black first: a picture with alpha still lands on the tile's black base
	SDL_SetSurfaceBlendMode(scratch, SDL_BLENDMODE_NONE);
	SDL_FillRect(scratch, NULL, SDL_MapRGBA(scratch->format, 0, 0, 0, 255));
	if (pic->w == r.w && pic->h == r.h)
		SDL_BlitSurface(pic, NULL, scratch, NULL);
	else
		SDL_BlitScaled(pic, NULL, scratch, NULL); // the caller should pass it pre-cropped; never leave a gap
	cutCorners(scratch, rad);
	SDL_SetSurfaceBlendMode(scratch, SDL_BLENDMODE_BLEND);
	SDL_BlitSurface(scratch, NULL, dst, &(SDL_Rect){r.x, r.y, r.w, r.h});
}

///////////////////////////////////////////////////////////////////////////////
// Text

typedef struct {
	char line[MAX_LINES][LINE_MAX];
	int n;
} Lines;

static int textWidth(TTF_Font* f, const char* text) {
	int w = 0;
	if (text[0])
		GFX_measureText(f, text, &w, NULL);
	return w;
}

bool Tiles_camelSplit(char* buf, size_t size) {
	size_t len = strlen(buf);
	if (strchr(buf, ' ') || len + 1 >= size)
		return false;
	for (size_t i = 1; i < len; i++) {
		if (islower((unsigned char)buf[i - 1]) && isupper((unsigned char)buf[i])) {
			memmove(buf + i + 1, buf + i, len - i + 1);
			buf[i] = ' ';
			return true;
		}
	}
	return false;
}

float Tiles_fitWordsSp(const char* text, float sp_max, float sp_min, bool bold, int max_w) {
	char work[LINE_MAX];
	float sp = sp_max;
	// sp_max itself first (the size the caller draws at when it fits), then whole-sp steps: a tile's scale
	// animates sp_max every frame, and fractional steps below it would open new font sizes each frame
	for (; sp > sp_min; sp = (sp == sp_max) ? ceilf(sp_max) - 1.0f : sp - 1.0f) {
		TTF_Font* f = UIFont_get(sp, bold);
		if (!f)
			break;
		snprintf(work, sizeof(work), "%s", text ? text : "");
		bool fits = true;
		char* save = NULL;
		for (char* tok = strtok_r(work, " ", &save); tok && fits; tok = strtok_r(NULL, " ", &save))
			fits = textWidth(f, tok) <= max_w;
		if (fits)
			return sp;
	}
	return sp_min;
}

// Word-wrap text to max_w in at most max_lines lines (the last one ellipsised); a word wider than the
// line is ellipsised too. camel_split: a one-word name that doesn't fit first breaks at its first
// lowercase→uppercase boundary ("RetroAchievements" → "Retro" / "Achievements").
static void layoutLines(TTF_Font* f, const char* text, int max_w, int max_lines, bool camel_split, Lines* out) {
	out->n = 0;
	if (!f || !text || !text[0] || max_w <= 0 || max_lines <= 0)
		return;
	if (max_lines > MAX_LINES)
		max_lines = MAX_LINES;
	char buf[LINE_MAX];
	snprintf(buf, sizeof(buf), "%s", text);
	if (camel_split && textWidth(f, buf) > max_w)
		Tiles_camelSplit(buf, sizeof(buf));
	GFX_wrapText(f, buf, max_w, max_lines);
	char* p = buf;
	while (p && out->n < max_lines) {
		char* nl = strchr(p, '\n');
		if (nl)
			*nl = '\0';
		if (textWidth(f, p) > max_w)
			GFX_truncateText(f, p, out->line[out->n], max_w, 0);
		else
			snprintf(out->line[out->n], LINE_MAX, "%s", p);
		out->n++;
		p = nl ? nl + 1 : NULL;
	}
}

// White text tinted to colour c at alpha a. Rendered and freed here, not through GFX_getCachedText: everything drawn
// with it is baked into a cached surface (tiles, captions, slot items), and its one-off lines would evict the shared
// text cache's per-frame users (page title, tab labels, hint bar, list rows).
static void blitTextColor(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, SDL_Color c, Uint8 a) {
	if (!f || !text[0] || a == 0)
		return;
	SDL_Surface* s = GFX_renderText(f, text, COLOR_WHITE);
	if (!s)
		return;
	SDL_SetSurfaceColorMod(s, c.r, c.g, c.b);
	SDL_SetSurfaceAlphaMod(s, a);
	SDL_BlitSurface(s, NULL, dst, &(SDL_Rect){x, y, s->w, s->h});
	GFX_freeSurfaceAndTexture(s);
}

static SDL_Color greyColor(Uint8 c) {
	return (SDL_Color){c, c, c, 255};
}

// White text tinted to grey level c at alpha a.
static void blitText(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, Uint8 c, Uint8 a) {
	blitTextColor(dst, f, text, x, y, greyColor(c), a);
}

// A line of text with its dark shadow under it (the same white glyphs tinted black at 60% of a, SCALE1(1) right and
// down), the glyphs rendered once for both: a caption's name is made while a held D-pad scrolls, and rendering each
// line twice was half its cost.
static void blitTextShadowed(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, SDL_Color c, Uint8 a) {
	if (!f || !text[0] || a == 0)
		return;
	SDL_Surface* s = GFX_renderText(f, text, COLOR_WHITE);
	if (!s)
		return;
	SDL_SetSurfaceColorMod(s, c.r, c.g, c.b);
	SDL_SetSurfaceAlphaMod(s, a);
	InfoBand_blitShadowed(s, dst, x, y, (Uint8)(TEXT_SHADOW_ALPHA * a / 255));
	GFX_freeSurfaceAndTexture(s);
}

// Lines centred on cx, the block's top at y.
static void blitLinesCentred(SDL_Surface* dst, TTF_Font* f, const Lines* l, int cx, int y, SDL_Color c, Uint8 a) {
	int lh = TTF_FontHeight(f);
	for (int i = 0; i < l->n; i++)
		blitTextColor(dst, f, l->line[i], cx - textWidth(f, l->line[i]) / 2, y + i * lh, c, a);
}

// A name as the tile's whole content ("word": title tile, logo fallback) at 17 sp (× the tile scale), centred.
// count_h > 0: a count line (count_h px, gap under the name) follows, and the lines are capped so it fits the tile.
// Returns the block's bottom (px), r's centre when nothing is drawn.
static int drawWord(SDL_Surface* dst, SDL_Rect r, const char* name, float s, SDL_Color c, Uint8 a, int gap,
					int count_h) {
	if (!name || !name[0])
		return r.y + r.h / 2;
	int pad = NX_DPF(WORD_PAD_DP * s);
	TTF_Font* f = UIFont_get(WORD_TEXT_SP * s, false);
	if (!f)
		return r.y + r.h / 2;
	int lh = TTF_FontHeight(f);
	// a count under the name (count_h > 0) caps the lines so it stays inside the tile, clear of the selected outline
	int edge = NX_DPF(GRID_SEL_OUTLINE_DP);
	int max_lines = GridLayout_wordMaxLines(r.h, pad, lh, gap, count_h, edge < 1 ? 1 : edge, WORD_MAX_LINES);
	Lines l;
	layoutLines(f, name, r.w - 2 * pad, max_lines, false, &l);
	int y = r.y + GridLayout_wordTop(r.h, l.n, lh);
	blitLinesCentred(dst, f, &l, r.x + r.w / 2, y, c, a);
	return y + l.n * lh;
}

// The selected main-menu tile's "N games" (Grid): centred on cx, its top at y, in colour c (opaque) at alpha a.
static void drawCount(SDL_Surface* dst, const char* count, float sp, int cx, int y, SDL_Color c, Uint8 a) {
	if (!count || !count[0] || a == 0)
		return;
	TTF_Font* f = UIFont_get(sp, false);
	if (!f)
		return;
	blitTextColor(dst, f, count, cx - textWidth(f, count) / 2, y, (SDL_Color){c.r, c.g, c.b, 255}, a);
}

// textWidth that measures 0 for a NULL font or text (rowview.c's textW).
static int textWidthOrZero(TTF_Font* f, const char* t) {
	int w = 0;
	if (f && t && t[0])
		GFX_measureText(f, t, &w, NULL);
	return w;
}

void Tiles_longestWord(TTF_Font* f, const char* name, char* out, size_t size) {
	out[0] = '\0';
	int best = -1;
	const char* p = name;
	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		const char* q = p;
		while (*q && *q != ' ' && *q != '\t')
			q++;
		if (q > p) {
			char word[256];
			snprintf(word, sizeof(word), "%.*s", (int)(q - p), p);
			int w = textWidthOrZero(f, word);
			if (w > best) {
				best = w;
				snprintf(out, size, "%s", word);
			}
		}
		p = q;
	}
}

float Tiles_collNameSp(const char* name, float start, float count_sp, int avail) {
	char word[256];
	TTF_Font* f = UIFont_get(start, false);
	Tiles_longestWord(f, name, word, sizeof(word));
	float sp = Row_collNameSp(start, (float)textWidthOrZero(f, word), (float)avail, count_sp);
	// widths don't scale exactly with the size (hinting, NX_SP's rounding): a whole sp more while it still overflows
	float floor_sp = Row_collNameFloor(start, count_sp);
	TTF_Font* fs;
	while (sp > floor_sp && (fs = UIFont_get(sp, false)) && textWidthOrZero(fs, word) > avail) {
		float next = sp == start ? ceilf(start) - 1.0f : sp - 1.0f;
		sp = next > floor_sp ? next : floor_sp;
	}
	return sp;
}

static int textBlockStepColor(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w,
							  int max_lines, bool camel_split, SDL_Color c, Uint8 alpha, bool shadow, int line_h);

// A collection (Grid), and a console without a logo (§8.4): the name in the off-white (TILE_MENU_GREY) at 20 sp
// × the tile scale, shrinking in whole sp until its longest word fits, never below max(0.75 × start, 1.25 × the
// count), then up to 3 lines with "…" at a 1.15 line height; under it (4 dp) the reserved "N games" line at
// max(10, count_spec_sp × the tile scale), at count_a.
// With an emblem (a logo-less console's, MenuArt file), it sits over the name (36 dp × the tile scale, at most 30% of
// the tile, 6 dp above the name) and the whole block is centred.
static void drawNameTile(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float s, SDL_Color c, Uint8 a,
						 float count_spec_sp, Uint8 count_a, const char* emblem) {
	const char* name = t->name;
	if (!name || !name[0])
		return;
	SDL_Surface* icon = NULL;
	if (emblem) {
		int box = NX_DPF(GRID_EMBLEM_DP * s);
		if (box > (int)(r.h * 0.3f))
			box = (int)(r.h * 0.3f);
		icon = box > 0 ? MenuArt_get(emblem, box, box) : NULL;
	}
	int top = icon ? icon->h + NX_DPF(GRID_EMBLEM_GAP_DP * s) : 0; // the room over the name
	int pad = NX_DPF(WORD_PAD_DP * s);
	int avail = r.w - 2 * pad;
	float start = GRID_COLL_NAME_SP * s;
	float count_sp = GridLayout_countSp(count_spec_sp, s);
	TTF_Font* f = UIFont_get(start, false);
	if (!f)
		return;
	float sp = Tiles_collNameSp(name, start, count_sp, avail);
	// fonts last: a font pointer is only good until the next UIFont_get
	TTF_Font* fc = UIFont_get(count_sp, false);
	int count_h = fc ? TTF_FontHeight(fc) : 0;
	int step = Row_lineStep(NX_SP(sp), GRID_COLL_LINE);
	int gap = NX_DPF(GRID_COLL_COUNT_GAP_DP);
	int max_lines = GridLayout_collMaxLines(r.h - 2 * pad - top, step, gap, count_h);
	f = UIFont_get(sp, false);
	if (!f)
		return;
	int nh = textBlockStepColor(NULL, f, name, 0, 0, avail, max_lines, false, c, a, false, step);
	// the text block centred in what's left under the emblem: the emblem's top then lands on name_y, so emblem, name
	// and count are centred together
	GridCollText lay = GridLayout_collText(r.h - top, step > 0 && nh > 0 ? nh / step : 1, step, gap, count_h);
	if (icon) {
		SDL_SetSurfaceColorMod(icon, c.r, c.g, c.b);
		SDL_SetSurfaceAlphaMod(icon, a);
		SDL_BlitSurface(icon, NULL, dst, &(SDL_Rect){r.x + (r.w - icon->w) / 2, r.y + lay.name_y, icon->w, icon->h});
		SDL_SetSurfaceColorMod(icon, 255, 255, 255);
		SDL_SetSurfaceAlphaMod(icon, 255);
	}
	textBlockStepColor(dst, f, name, r.x + r.w / 2, r.y + top + lay.name_y, avail, max_lines, false, c, a, false,
					   step);
	drawCount(dst, t->count, count_sp, r.x + r.w / 2, r.y + top + lay.count_y, greyColor(TILE_COUNT_GREY), count_a);
}

// The Carousel's tool name font: 20 sp at the full-size tile, scaled with the tile's width, shrunk (never grown)
// until "Achievements" fits avail px.
static TTF_Font* carouselToolFont(int tile_w, int avail) {
	float px_per_dp = FIXED_SCALE * 30.0f / 42.0f;
	float px_per_sp = FIXED_SCALE * 12.0f / 14.0f;
	float sp = CAROUSEL_TOOL_TEXT_SP * (tile_w / px_per_dp) / CAROUSEL_TILE_W_SPEC;
	TTF_Font* f = UIFont_get(sp, false);
	int w = f ? textWidth(f, TOOL_LONGEST_WORD) : 0;
	if (!f || avail <= 0 || w <= avail)
		return f;
	sp = sp * (float)avail / (float)w;
	f = UIFont_get(sp, false);
	// NX_SP rounds to the nearest px: one px smaller when that rounding still overflows
	if (f && textWidth(f, TOOL_LONGEST_WORD) > avail && sp > 2.0f / px_per_sp)
		f = UIFont_get(sp - 1.0f / px_per_sp, false);
	return f;
}

// Tool: the icon and the name centred together as one group (name alone when there's no icon).
static void drawTool(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float s, SDL_Color c, Uint8 a) {
	int pad = NX_DPF(TOOL_PAD_DP * s);
	int icon_px = t->carousel ? (int)(r.h * CAROUSEL_TOOL_ICON_SHARE + 0.5f) : NX_DPF(TOOL_ICON_DP * s);
	SDL_Surface* icon = t->icon_file ? MenuArt_get(t->icon_file, icon_px, icon_px) : NULL;
	// fonts after MenuArt_get: a font pointer is only good until the next UIFont_get
	char name[LINE_MAX];
	snprintf(name, sizeof(name), "%s", t->name ? t->name : "");
	TTF_Font* f;
	if (t->carousel) {
		f = carouselToolFont(r.w, r.w - 2 * pad);
	} else {
		// the Grid: TEXT_GRID_TOOL per device (px, as sp; 0: 14 sp × the tile scale); a one-word name too wide breaks at its lower→upper case
		// boundary first ("Retro / Achievements"), then the size shrinks (to 11 sp) until its longest word fits, as on
		// Home
		float tool_px = TextPx_for(TEXT_GRID_TOOL, UIScale_deviceIndex(UI_DEVICE_NAME));
		float sp_max = tool_px > 0 ? tool_px / (FIXED_SCALE * 12.0f / 14.0f) : TOOL_TEXT_SP * s;
		float sp_min = sp_max < TOOL_TEXT_MIN_SP ? sp_max : TOOL_TEXT_MIN_SP;
		f = UIFont_get(sp_max, false);
		if (f && textWidth(f, name) > r.w - 2 * pad)
			Tiles_camelSplit(name, sizeof(name));
		f = UIFont_get(Tiles_fitWordsSp(name, sp_max, sp_min, false, r.w - 2 * pad), false);
	}
	Lines l = {.n = 0};
	if (f)
		layoutLines(f, name, r.w - 2 * pad, 2, true, &l);
	int lh = f ? TTF_FontHeight(f) : 0;
	int gap = (icon && l.n) ? NX_DPF(TOOL_GAP_DP * s) : 0;
	int group_h = (icon ? icon->h : 0) + gap + l.n * lh;
	int y = r.y + (r.h - group_h) / 2;
	if (icon) {
		SDL_SetSurfaceColorMod(icon, c.r, c.g, c.b);
		SDL_SetSurfaceAlphaMod(icon, a);
		SDL_BlitSurface(icon, NULL, dst, &(SDL_Rect){r.x + (r.w - icon->w) / 2, y, icon->w, icon->h});
		SDL_SetSurfaceColorMod(icon, 255, 255, 255);
		SDL_SetSurfaceAlphaMod(icon, 255);
		y += icon->h + gap;
	}
	if (l.n)
		blitLinesCentred(dst, f, &l, r.x + r.w / 2, y, c, a);
}

// Console logo, inset 22 dp at the sides and 30 dp top and bottom (scaled with the tile); the name when
// there's no logo (the Grid's as a collection tile, the Carousel's as a word). A Grid tile adds "N games" 6 dp under what's drawn (count_a), the two centred as one block.
SDL_Surface* Tiles_cornerMask(int w, int h) {
	SDL_Surface* s = w > 0 && h > 0 ? SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, 0);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	int rad = clampRadius(NX_DPF(TILE_RADIUS_DP), w, h);
	int xs[2] = {0, w - rad}, ys[2] = {0, h - rad};
	for (int cy = 0; cy < 2 && rad > 0; cy++) {
		for (int cx = 0; cx < 2; cx++) {
			for (int y = ys[cy]; y < ys[cy] + rad; y++) {
				Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
				for (int x = xs[cx]; x < xs[cx] + rad; x++) {
					float cov = roundedCoverage(x, y, w, h, (float)rad);
					row[x] = (Uint32)((1.0f - cov) * 255.0f + 0.5f) << 24; // black, where the tile isn't
				}
			}
		}
	}
	return s;
}

SDL_Surface* Tiles_borderOverlay(int w, int h) {
	SDL_Surface* s = w > 0 && h > 0 ? SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, 0);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	int b = NX_DPF(TILE_BORDER_DP);
	blitShape(s, SHAPE_OUTLINE, 0, 0, w, h, NX_DPF(TILE_RADIUS_DP), b < 1 ? 1 : b, 255, 255);
	return s;
}

Uint8 Tiles_borderAlpha(void) {
	return (Uint8)TILE_BORDER_ALPHA;
}

SDL_Surface* Tiles_ringOverlay(int w, int h, SDL_Color c) {
	int ring = NX_DPF(TILE_RING_DP);
	int W = w + 2 * ring, H = h + 2 * ring;
	SDL_Surface* s = w > 0 && h > 0 ? SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
	if (!s)
		return NULL;
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	int rad = NX_DPF(TILE_RADIUS_DP);
	float orad = (float)clampRadius(rad + ring, W, H), irad = (float)clampRadius(rad, w, h);
	Uint32 rgb = (Uint32)c.r << 16 | (Uint32)c.g << 8 | c.b;
	for (int y = 0; y < H; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
		for (int x = 0; x < W; x++) {
			float outer = roundedCoverage(x, y, W, H, orad);
			float inner = x >= ring && x < ring + w && y >= ring && y < ring + h
							  ? roundedCoverage(x - ring, y - ring, w, h, irad)
							  : 0.0f;
			row[x] = (Uint32)(outer * (1.0f - inner) * 255.0f + 0.5f) << 24 | rgb;
		}
	}
	return s;
}

void Tiles_logoBox(int tile_w, int tile_h, float s, int* box_w, int* box_h) {
	if (!(s > 0.0f) || s > 1.0f)
		s = 1.0f;
	*box_w = tile_w - 2 * NX_DPF(LOGO_INSET_X_DP * s);
	*box_h = tile_h - 2 * NX_DPF(LOGO_INSET_Y_DP * s);
}

void Tiles_gridLogoBox(int tile_w, int tile_h, float s, int* box_w, int* box_h) {
	Tiles_logoBox(tile_w, tile_h, s, box_w, box_h);
	TTF_Font* f = UIFont_get(GridLayout_countSp(GRID_LOGO_COUNT_SP, s), false);
	*box_h = GridLayout_logoBoxH(*box_h, NX_DPF(GRID_LOGO_COUNT_GAP_DP), f ? TTF_FontHeight(f) : 0);
}

static void drawLogo(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float s, SDL_Color c, Uint8 a, Uint8 count_a) {
	int box_w, box_h;
	Tiles_logoBox(r.w, r.h, s, &box_w, &box_h);
	int gap = NX_DPF(GRID_LOGO_COUNT_GAP_DP);
	float count_sp = GridLayout_countSp(GRID_LOGO_COUNT_SP, s);
	// the Grid's logo and its count centred as one block, the count's line reserved whether or not it is known yet (so
	// nothing moves when it arrives): the logo's box gives that line up (Tiles_gridLogoBox, as warmLogos decodes it)
	TTF_Font* fcount = !t->carousel ? UIFont_get(count_sp, false) : NULL;
	int count_h = fcount ? TTF_FontHeight(fcount) : 0;
	int logo_box_h = GridLayout_logoBoxH(box_h, gap, count_h);
	SDL_Surface* logo =
		(t->logo_file && box_w > 0 && logo_box_h > 0) ? MenuArt_get(t->logo_file, box_w, logo_box_h) : NULL;
	if (!logo && !t->carousel) {
		// the Grid: drawn like a collection tile with the unknown-console emblem over its name, in the off-white at full
		// alpha, and its 13 sp count
		// 4 dp under the name, the count line reserved unlit
		drawNameTile(dst, r, t, s, c, 255, GRID_LOGO_COUNT_SP, count_a, TILE_UNKNOWN_CONSOLE_ICON);
		return;
	}
	if (!logo) {
		// fonts in order: a font pointer is only good until the next UIFont_get
		TTF_Font* fc = (t->count && t->count[0]) ? UIFont_get(count_sp, false) : NULL;
		int count_h = fc ? TTF_FontHeight(fc) : 0;
		int bottom = drawWord(dst, r, t->name, s, c, a, gap, count_h);
		drawCount(dst, t->count, count_sp, r.x + r.w / 2, bottom + gap, UI_accent(), count_a);
		return;
	}
	int lw = logo->w, lh = logo->h;
	// the logo art is always the fixed off-white (TILE_MENU_GREY, baked into the PNGs); only its alpha follows the
	// selection
	SDL_SetSurfaceAlphaMod(logo, a);
	float logo_y, count_y;
	GridLayout_logoBlock((float)r.y, (float)r.h, (float)lh, (float)gap, (float)count_h, &logo_y, &count_y);
	SDL_BlitSurface(logo, NULL, dst, &(SDL_Rect){r.x + (r.w - lw) / 2, (int)floorf(logo_y + 0.5f), lw, lh});
	SDL_SetSurfaceAlphaMod(logo, 255);
	int y = (int)floorf(count_y + 0.5f);
	// "N games" in the count grey, a step under the logo
	drawCount(dst, t->count, count_sp, r.x + r.w / 2, y, greyColor(TILE_COUNT_GREY), count_a);
}

///////////////////////////////////////////////////////////////////////////////
// The lit game caption: a black fade, 85% under the label and ramping from clear above it (at least the lower 55%), the name (game tiles only: a title tile
// already shows its name) and the count-only info line, built once into a tile-sized surface and blitted
// at the lit alpha. Two slots: the tile fading out and the one fading in.

#define CAPTION_SLOTS 2
#define CAPTION_KEY 1024 // name (<256) + 3 segments (<160 each) + the header: never truncated
typedef struct {
	char key[CAPTION_KEY];
	int w, h, rad;
	SDL_Surface* surface;
	unsigned stamp;
} CaptionSlot;

static CaptionSlot captions[CAPTION_SLOTS];
static unsigned caption_clock = 0;

static void captionKey(const TileSpec* t, float s, char* key, size_t size) {
	// the info font's instance: a reopened font (scale change, eviction) re-keys the info line
	int n = snprintf(key, size, "%p|%d|%d|%d|%d|%d|%s|", (void*)UIFont_get(CAPTION_INFO_SP, false), (int)FIXED_SCALE,
					 (int)t->kind, NX_SP(CAPTION_INFO_SP), NX_SP(CAPTION_NAME_SP * s), NX_DPF(CAPTION_PAD_X_DP * s),
					 (t->kind == TILE_GAME && t->name) ? t->name : "");
	for (int i = 0; t->info && i < t->ninfo && n > 0 && (size_t)n < size; i++)
		n += snprintf(key + n, size - n, "%d:%s|", (int)t->info[i].kind, t->info[i].text);
}

static SDL_Surface* buildCaption(int w, int h, int rad, const TileSpec* t, float s) {
	SDL_Surface* cap = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!cap)
		return NULL;
	SDL_FillRect(cap, NULL, SDL_MapRGBA(cap->format, 0, 0, 0, 0));
	SDL_SetSurfaceBlendMode(cap, SDL_BLENDMODE_BLEND);

	int pad_x = NX_DPF(CAPTION_PAD_X_DP * s);
	int max_w = w - 2 * pad_x;
	int bottom = h - NX_DPF(CAPTION_PAD_B_DP * s);
	// measure first: the fade has to reach over the label's top
	TTF_Font* f_info = (max_w > 0 && t->info && t->ninfo > 0) ? UIFont_get(CAPTION_INFO_SP, false) : NULL;
	int info_h = f_info ? TTF_FontHeight(f_info) : 0;
	TTF_Font* f = (max_w > 0 && t->kind == TILE_GAME && t->name && t->name[0]) ? UIFont_get(CAPTION_NAME_SP * s, false)
																			   : NULL;
	Lines l = {.n = 0};
	if (f)
		layoutLines(f, t->name, max_w, 2, false, &l);
	int lh = f ? TTF_FontHeight(f) : 0;
	int gap = (info_h && l.n) ? NX_DPF(CAPTION_GAP_DP * s) : 0;
	int label_top = bottom - info_h - gap - l.n * lh;

	// the fade backs the label over a screenshot: from twice the label's height (at least CAPTION_FADE_OVER_DP) above
	// the label's top down to the bottom, never less than CAPTION_FADE_SHARE of the tile. A title tile's ground is
	// already black and its own (white) name sits in the lower half, where the fade would grey its later lines.
	if (t->kind == TILE_GAME) {
		int over = (int)((bottom - label_top) * CAPTION_FADE_OVER_LABELS + 0.5f);
		int min_over = NX_DPF(CAPTION_FADE_OVER_DP * s);
		if (over < min_over)
			over = min_over;
		int fade_h = h - (label_top - over);
		int share_h = (int)(h * CAPTION_FADE_SHARE + 0.5f);
		if (fade_h < share_h)
			fade_h = share_h;
		if (fade_h > h)
			fade_h = h;
		// the fade's rows are uniform, so its width only has to cover the tile: ask for it in 64 px buckets and
		// blit the tile's part, so tiles of nearby widths (the lit one, a scale step) share one cached fade
		int fade_w = (w + 63) & ~63;
		// held at the full 85% under the label (and a few dp over it), ramping from clear only above that
		int hold = h - label_top + NX_DPF(CAPTION_FADE_HOLD_PAD_DP * s);
		if (hold > fade_h - 1)
			hold = fade_h - 1;
		SDL_Surface* fade = UI_bandFadeSurface(fade_w, fade_h, CAPTION_FADE_EDGE, hold); // cache-owned
		if (fade)																		 // over clear pixels: black at the row's alpha, as SDL's blend
			UI_blitFade(fade, &(SDL_Rect){0, 0, w, fade_h}, cap, 0, h - fade_h);
	}
	if (max_w <= 0) {
		cutCorners(cap, rad);
		return cap;
	}
	int y = bottom;
	if (f_info && (f_info = UIFont_get(CAPTION_INFO_SP, false))) { // again: the name font was fetched after it
		y -= info_h;
		if (InfoBand_drawSegments(cap, t->info, t->ninfo, pad_x, false, y, max_w, f_info) > 0)
			y -= gap;
		else
			y += info_h;
	}
	if (f) {
		// fonts: InfoBand may have opened others; f is the caption name's, fetched again in case it was evicted
		f = UIFont_get(CAPTION_NAME_SP * s, false);
		y -= l.n * lh;
		for (int i = 0; f && i < l.n; i++)
			blitText(cap, f, l.line[i], pad_x, y + i * lh, 255, 255);
	}
	cutCorners(cap, rad); // the fade is darkest at the bottom corners: keep it inside the tile
	return cap;
}

static SDL_Surface* getCaption(int w, int h, int rad, const TileSpec* t, float s) {
	char key[CAPTION_KEY];
	captionKey(t, s, key, sizeof(key));
	for (int i = 0; i < CAPTION_SLOTS; i++) {
		CaptionSlot* c = &captions[i];
		if (c->surface && c->w == w && c->h == h && c->rad == rad && strcmp(c->key, key) == 0) {
			c->stamp = ++caption_clock;
			return c->surface;
		}
	}
	SDL_Surface* cap = buildCaption(w, h, rad, t, s);
	if (!cap)
		return NULL;
	CaptionSlot* victim = &captions[0];
	for (int i = 0; i < CAPTION_SLOTS; i++) {
		if (!captions[i].surface) {
			victim = &captions[i];
			break;
		}
		if (captions[i].stamp < victim->stamp)
			victim = &captions[i];
	}
	if (victim->surface)
		GFX_freeSurfaceAndTexture(victim->surface);
	snprintf(victim->key, sizeof(victim->key), "%s", key);
	victim->w = w;
	victim->h = h;
	victim->rad = rad;
	victim->surface = cap;
	victim->stamp = ++caption_clock;
	return cap;
}

// The lit caption for a tile at r: a GAME without a picture is drawn as a title tile, so its caption carries only
// the info.
static SDL_Surface* litCaption(SDL_Rect r, const TileSpec* t, float s) {
	TileSpec cap_spec = *t;
	if (t->kind == TILE_GAME && !t->picture)
		cap_spec.kind = TILE_TITLE;
	return getCaption(r.w, r.h, NX_DPF(TILE_RADIUS_DP), &cap_spec, s);
}

///////////////////////////////////////////////////////////////////////////////

void Tiles_draw(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float lit) {
	if (!dst || !t || r.w <= 0 || r.h <= 0)
		return;
	if (!(lit > 0.0f))
		lit = 0.0f;
	if (lit > 1.0f)
		lit = 1.0f;
	float s = t->scale;
	if (!(s > 0.0f) || s > 1.0f)
		s = 1.0f;

	bool game = t->kind == TILE_GAME || t->kind == TILE_TITLE;
	// the Grid's console/collection/tool tiles select with the "Logo" look (a 70% accent outline, the content in the
	// accent, no fill); the game-list Carousel's tool tile keeps its fill: the accent with the ink content
	bool logo_look = !game && !t->carousel;
	int rad = NX_DPF(TILE_RADIUS_DP);
	Uint8 lit_a = (Uint8)(lit * 255.0f + 0.5f);
	SDL_Color ac = UI_accent();

	// the game ring: an accent rounded rect 3 dp larger on each side, under the tile
	if (game && lit_a) {
		int ring = NX_DPF(TILE_RING_DP);
		blitShapeColor(dst, SHAPE_FILL, r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring, rad + ring, 0,
					   UI_accent(), lit_a);
	}
	// the opaque black base
	blitShape(dst, SHAPE_FILL, r.x, r.y, r.w, r.h, rad, 0, 0, 255);
	// the plain look's 1 dp inset border (white 8%), fading out as the tile lights
	if (lit < 1.0f) {
		int b = NX_DPF(TILE_BORDER_DP);
		if (b < 1)
			b = 1;
		blitShape(dst, SHAPE_OUTLINE, r.x, r.y, r.w, r.h, rad, b, 255,
				  (Uint8)(TILE_BORDER_ALPHA * (1.0f - lit) + 0.5f));
	}
	if (logo_look && lit_a) {
		// the selected outline: 1.5 dp of the opaque accent at 70%, inside the tile's edge
		int b = NX_DPF(GRID_SEL_OUTLINE_DP);
		blitShapeColor(dst, SHAPE_OUTLINE, r.x, r.y, r.w, r.h, rad, b < 1 ? 1 : b, (SDL_Color){ac.r, ac.g, ac.b, 255},
					   (Uint8)(GRID_SEL_OUTLINE_ALPHA * lit_a / 255));
	} else if (!game && lit_a) {
		// the Carousel's lit fill: the opaque accent (Color 1; white by default), as every other selection mark
		blitShapeColor(dst, SHAPE_FILL, r.x, r.y, r.w, r.h, rad, 0, (SDL_Color){ac.r, ac.g, ac.b, 255}, lit_a);
	}

	// content: at 82% (a Grid collection's name at 100%, as a logo-less console's: drawLogo) → 100% lit; the Grid's in
	// the fixed off-white (TILE_MENU_GREY), the Carousel's fill kinds white → the accent's ink (Color 5; black by
	// default), the game kinds white
	Uint8 plain_a = (t->kind == TILE_COLLECTION && logo_look) ? 255 : TILE_PLAIN_ALPHA;
	Uint8 a = (Uint8)(plain_a + (255 - plain_a) * lit + 0.5f);
	SDL_Color c;
	if (game)
		c = greyColor(255);
	else if (logo_look)
		c = greyColor(TILE_MENU_GREY); // the Grid: the fixed off-white, lit or not (the alpha marks the selection)
	else {
		// white → the ink; written so the default black ink gives exactly the former 255 · (1 − lit) grey
		SDL_Color ink = UI_onAccent();
		c = (SDL_Color){(Uint8)(ink.r * lit + 255.0f * (1.0f - lit) + 0.5f),
						(Uint8)(ink.g * lit + 255.0f * (1.0f - lit) + 0.5f),
						(Uint8)(ink.b * lit + 255.0f * (1.0f - lit) + 0.5f), 255};
	}
	// "N games": on every Grid console and collection tile
	Uint8 count_a = logo_look ? 255 : 0;
	switch (t->kind) {
	case TILE_LOGO:
		drawLogo(dst, r, t, s, c, a, count_a);
		break;
	case TILE_TOOL:
		drawTool(dst, r, t, s, c, a);
		break;
	case TILE_COLLECTION:
		drawNameTile(dst, r, t, s, c, a, GRID_COLL_COUNT_SP, count_a, NULL);
		break;
	case TILE_GAME:
		if (t->picture) {
			drawPicture(dst, r, rad, t->picture);
			break;
		}
		// no screenshot: a title tile
		drawWord(dst, r, t->name, s, c, a, 0, 0);
		break;
	case TILE_TITLE:
		drawWord(dst, r, t->name, s, c, a, 0, 0);
		break;
	}

	if (game && lit_a && !t->carousel && !t->no_caption) {
		SDL_Surface* cap = litCaption(r, t, s);
		if (cap) {
			SDL_SetSurfaceAlphaMod(cap, lit_a);
			SDL_BlitSurface(cap, NULL, dst, &(SDL_Rect){r.x, r.y, r.w, r.h});
			SDL_SetSurfaceAlphaMod(cap, 255);
		}
	}
}

SDL_Surface* Tiles_captionSurface(SDL_Rect r, const TileSpec* t, float lit) {
	if (!t || r.w <= 0 || r.h <= 0 || t->carousel || !(lit > 0.0f))
		return NULL;
	if (t->kind != TILE_GAME && t->kind != TILE_TITLE)
		return NULL;
	float s = t->scale;
	if (!(s > 0.0f) || s > 1.0f)
		s = 1.0f;
	return litCaption(r, t, s);
}

void Tiles_drawCaption(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float lit) {
	if (!dst || !t || r.w <= 0 || r.h <= 0 || t->carousel || !(lit > 0.0f))
		return;
	if (t->kind != TILE_GAME && t->kind != TILE_TITLE)
		return;
	float s = t->scale;
	if (!(s > 0.0f) || s > 1.0f)
		s = 1.0f;
	SDL_Surface* cap = litCaption(r, t, s);
	if (cap)
		UI_blitBlendOpaque(cap, NULL, dst, r.x, r.y, (int)((lit > 1.0f ? 1.0f : lit) * 255.0f + 0.5f));
}

// x: each line's centre, or (left) every line's left edge.
// align: -1 every line's left edge at x0, 0 centred on x0, 1 its right edge at x0.
static int textBlockAligned(SDL_Surface* dst, TTF_Font* f, const char* text, int x0, int align, int y, int max_w,
							int max_lines, bool camel_split, SDL_Color c, Uint8 alpha, bool shadow, int line_h) {
	if (!f || !text || !text[0])
		return 0;
	Lines l;
	layoutLines(f, text, max_w, max_lines, camel_split, &l);
	int fh = TTF_FontHeight(f);
	int lh = line_h > 0 ? line_h : fh;
	if (dst) {
		int lead = (lh - fh) / 2; // the glyphs centred in their line box (0 at the font's own height)
		for (int i = 0; i < l.n; i++) {
			int lw = align < 0 ? 0 : textWidth(f, l.line[i]);
			int x = align < 0 ? x0 : (align > 0 ? x0 - lw : x0 - lw / 2), ly = y + i * lh + lead;
			if (shadow)
				blitTextShadowed(dst, f, l.line[i], x, ly, c, alpha);
			else
				blitTextColor(dst, f, l.line[i], x, ly, c, alpha);
		}
	}
	return l.n * lh;
}

static int textBlockStepColor(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w,
							  int max_lines, bool camel_split, SDL_Color c, Uint8 alpha, bool shadow, int line_h) {
	return textBlockAligned(dst, f, text, cx, 0, y, max_w, max_lines, camel_split, c, alpha, shadow, line_h);
}

int Tiles_textBlockStep(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w, int max_lines,
						bool camel_split, Uint8 grey, Uint8 alpha, bool shadow, int line_h) {
	return textBlockStepColor(dst, f, text, cx, y, max_w, max_lines, camel_split, greyColor(grey), alpha, shadow,
							  line_h);
}

int Tiles_textBlock(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w, int max_lines,
					bool camel_split, Uint8 grey, Uint8 alpha, bool shadow) {
	return Tiles_textBlockStep(dst, f, text, cx, y, max_w, max_lines, camel_split, grey, alpha, shadow, 0);
}

int Tiles_textBlockLeft(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, int max_w, int max_lines,
						Uint8 grey, bool shadow) {
	return textBlockAligned(dst, f, text, x, -1, y, max_w, max_lines, false, greyColor(grey), 255, shadow, 0);
}

int Tiles_textBlockRight(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, int max_w, int max_lines,
						 Uint8 grey, bool shadow) {
	return textBlockAligned(dst, f, text, x, 1, y, max_w, max_lines, false, greyColor(grey), 255, shadow, 0);
}

// Letters and digits only, lower case: "Artwork Manager", "ArtworkManager" and "artwork-manager" compare equal.
static bool looseMatch(const char* a, size_t alen, const char* b) {
	const char* aend = a + alen;
	while (a < aend || *b) {
		while (a < aend && !isalnum((unsigned char)*a))
			a++;
		while (*b && !isalnum((unsigned char)*b))
			b++;
		int ca = a < aend ? tolower((unsigned char)*a) : 0;
		int cb = tolower((unsigned char)*b);
		if (ca != cb)
			return false;
		if (a < aend)
			a++;
		if (*b)
			b++;
	}
	return true;
}

const char* Tiles_toolIcon(const char* pak_name) {
	static const struct {
		const char* name;
		const char* icon;
	} map[] = {
		{"RetroAchievements", "menu_icon_achievements.png"},
		{"Artwork Manager", "menu_icon_artwork.png"},
		{"Game Tracker", "menu_icon_gametime.png"},
		{"Settings", "menu_icon_settings.png"},
		{"Device Sync", "menu_icon_sync.png"},
		{"Emulator Settings", "menu_icon_emulator.png"},
		{"Files", "menu_icon_files.png"},
		{"Image Viewer", "menu_icon_images.png"},
		{"Media Player", "menu_icon_media.png"},
		{"Music Player", "menu_icon_music.png"},
		{"Xtras", "menu_icon_xtras.png"},
		{"PortMaster", "menu_icon_portmaster.png"},
		{"Cheat Database", "menu_icon_cheats.png"},
	};
	if (!pak_name)
		return NULL;
	size_t len = strlen(pak_name);
	if (len >= 4 && strcasecmp(pak_name + len - 4, ".pak") == 0)
		len -= 4;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (looseMatch(pak_name, len, map[i].name))
			return map[i].icon;
	}
	return NULL;
}

void Tiles_quit(void) {
	for (int i = 0; i < MASK_SLOTS; i++) {
		if (masks[i].surface)
			GFX_freeSurfaceAndTexture(masks[i].surface);
	}
	memset(masks, 0, sizeof(masks));
	for (int i = 0; i < CAPTION_SLOTS; i++) {
		if (captions[i].surface)
			GFX_freeSurfaceAndTexture(captions[i].surface);
	}
	memset(captions, 0, sizeof(captions));
	if (scratch)
		GFX_freeSurfaceAndTexture(scratch);
	scratch = NULL;
}
