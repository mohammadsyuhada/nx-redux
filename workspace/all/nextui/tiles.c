// The shared main-menu tile painter (Grid and Carousel). Every shape is a cached white
// anti-aliased mask blitted with a colour/alpha mod, so drawing honours dst's clip rect and the
// plain/lit crossfade is only alpha and colour changes, no per-frame rasterising.

#include "tiles.h"

#include "api.h"
#include "defines.h"
#include "infoband.h"
#include "menuart.h"
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
#define WORD_TEXT_SP 17.0f
#define WORD_TEXT_MIN_SP 11.0f // a collection's name shrinks to fit its longest word, never below this
#define WORD_MAX_LINES 4

#define CAPTION_PAD_X_DP 12.0f
#define CAPTION_PAD_B_DP 10.0f
#define CAPTION_GAP_DP 3.0f
#define CAPTION_NAME_SP 14.0f
#define CAPTION_INFO_SP 11.0f
#define CAPTION_FADE_EDGE 0.85f
#define CAPTION_FADE_SHARE 0.55f

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
		SDL_FreeSurface(victim->surface);
	*victim = (MaskSlot){kind, w, h, rad, inset, s, ++mask_clock};
	return s;
}

// A rounded rect (or its inset outline) in grey level c at alpha a.
static void blitShape(SDL_Surface* dst, ShapeKind kind, int x, int y, int w, int h, int rad, int inset, Uint8 c,
					  Uint8 a) {
	if (w <= 0 || h <= 0 || a == 0)
		return;
	SDL_Surface* m = getMask(kind, w, h, rad, inset);
	if (!m)
		return;
	SDL_SetSurfaceColorMod(m, c, c, c);
	SDL_SetSurfaceAlphaMod(m, a);
	SDL_BlitSurface(m, NULL, dst, &(SDL_Rect){x, y, w, h});
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
			SDL_FreeSurface(scratch);
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
	for (; sp > sp_min; sp -= 1.0f) {
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

// White text tinted to grey level c at alpha a. The cached surface is shared: its mods are restored.
static void blitText(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, Uint8 c, Uint8 a) {
	if (!f || !text[0] || a == 0)
		return;
	SDL_Surface* owned = NULL;
	SDL_Surface* s = GFX_getCachedText(f, text, COLOR_WHITE);
	if (!s)
		s = owned = GFX_renderText(f, text, COLOR_WHITE);
	if (!s)
		return;
	SDL_SetSurfaceColorMod(s, c, c, c);
	SDL_SetSurfaceAlphaMod(s, a);
	SDL_BlitSurface(s, NULL, dst, &(SDL_Rect){x, y, s->w, s->h});
	SDL_SetSurfaceColorMod(s, 255, 255, 255);
	SDL_SetSurfaceAlphaMod(s, 255);
	if (owned)
		SDL_FreeSurface(owned);
}

// The dark shadow under a line of text: the same white glyphs tinted black at 60% of a, SCALE1(1) right and down.
static void blitTextShadow(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, Uint8 a) {
	blitText(dst, f, text, x + SCALE1(1), y + SCALE1(1), 0, (Uint8)(TEXT_SHADOW_ALPHA * a / 255));
}

// Lines centred on cx, the block's top at y.
static void blitLinesCentred(SDL_Surface* dst, TTF_Font* f, const Lines* l, int cx, int y, Uint8 c, Uint8 a) {
	int lh = TTF_FontHeight(f);
	for (int i = 0; i < l->n; i++)
		blitText(dst, f, l->line[i], cx - textWidth(f, l->line[i]) / 2, y + i * lh, c, a);
}

// A name as the tile's whole content ("word": collection, title tile, logo fallback), centred. fit: shrink from
// 17 sp (× the tile scale) down to 11 sp until the longest word fits before a word is ellipsised (collections).
static void drawWord(SDL_Surface* dst, SDL_Rect r, const char* name, float s, Uint8 c, Uint8 a, bool fit) {
	if (!name || !name[0])
		return;
	int pad = NX_DPF(WORD_PAD_DP * s);
	float sp = WORD_TEXT_SP * s;
	if (fit)
		sp = Tiles_fitWordsSp(name, sp, sp < WORD_TEXT_MIN_SP ? sp : WORD_TEXT_MIN_SP, false, r.w - 2 * pad);
	TTF_Font* f = UIFont_get(sp, false);
	if (!f)
		return;
	int lh = TTF_FontHeight(f);
	int max_lines = lh > 0 ? (r.h - 2 * pad) / lh : 1;
	if (max_lines < 1)
		max_lines = 1;
	if (max_lines > WORD_MAX_LINES)
		max_lines = WORD_MAX_LINES;
	Lines l;
	layoutLines(f, name, r.w - 2 * pad, max_lines, false, &l);
	blitLinesCentred(dst, f, &l, r.x + r.w / 2, r.y + (r.h - l.n * lh) / 2, c, a);
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
static void drawTool(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float s, Uint8 c, Uint8 a) {
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
		// the Grid: 14 sp (× the tile scale); a one-word name too wide breaks at its lower→upper case boundary
		// first ("Retro / Achievements"), then the size shrinks (to 11 sp) until its longest word fits, as on Home
		float sp_max = TOOL_TEXT_SP * s;
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
		SDL_SetSurfaceColorMod(icon, c, c, c);
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
// there's no logo.
static void drawLogo(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float s, Uint8 c, Uint8 a) {
	int box_w = r.w - 2 * NX_DPF(LOGO_INSET_X_DP * s);
	int box_h = r.h - 2 * NX_DPF(LOGO_INSET_Y_DP * s);
	SDL_Surface* logo = (t->logo_file && box_w > 0 && box_h > 0) ? MenuArt_get(t->logo_file, box_w, box_h) : NULL;
	if (!logo) {
		drawWord(dst, r, t->name, s, c, a, false);
		return;
	}
	SDL_SetSurfaceColorMod(logo, c, c, c);
	SDL_SetSurfaceAlphaMod(logo, a);
	SDL_BlitSurface(logo, NULL, dst,
					&(SDL_Rect){r.x + (r.w - logo->w) / 2, r.y + (r.h - logo->h) / 2, logo->w, logo->h});
	SDL_SetSurfaceColorMod(logo, 255, 255, 255);
	SDL_SetSurfaceAlphaMod(logo, 255);
}

///////////////////////////////////////////////////////////////////////////////
// The lit game caption: a black fade 0 → 85% over the lower 55%, the name (game tiles only: a title tile
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

	// the fade backs the name over a screenshot; a title tile's ground is already black and its own (white) name sits
	// in the lower half, where the fade would grey its later lines
	if (t->kind == TILE_GAME) {
		int fade_h = (int)(h * CAPTION_FADE_SHARE + 0.5f);
		SDL_Surface* fade = UI_bandFadeSurface(w, fade_h, CAPTION_FADE_EDGE, 0); // linear 0 → 85%, cache-owned
		if (fade)
			UI_blitFade(fade, NULL, cap, 0, h - fade_h); // over clear pixels: black at the row's alpha, as SDL's blend
	}

	int pad_x = NX_DPF(CAPTION_PAD_X_DP * s);
	int max_w = w - 2 * pad_x;
	int y = h - NX_DPF(CAPTION_PAD_B_DP * s);
	if (max_w <= 0) {
		cutCorners(cap, rad);
		return cap;
	}
	TTF_Font* f_info = (t->info && t->ninfo > 0) ? UIFont_get(CAPTION_INFO_SP, false) : NULL;
	if (f_info) {
		y -= TTF_FontHeight(f_info);
		if (InfoBand_drawSegments(cap, t->info, t->ninfo, pad_x, false, y, max_w, f_info) > 0)
			y -= NX_DPF(CAPTION_GAP_DP * s);
		else
			y += TTF_FontHeight(f_info);
	}
	if (t->kind == TILE_GAME && t->name && t->name[0]) {
		TTF_Font* f = UIFont_get(CAPTION_NAME_SP * s, false);
		if (f) {
			Lines l;
			layoutLines(f, t->name, max_w, 2, false, &l);
			int lh = TTF_FontHeight(f);
			y -= l.n * lh;
			for (int i = 0; i < l.n; i++)
				blitText(cap, f, l.line[i], pad_x, y + i * lh, 255, 255);
		}
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
		SDL_FreeSurface(victim->surface);
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
	int rad = NX_DPF(TILE_RADIUS_DP);
	Uint8 lit_a = (Uint8)(lit * 255.0f + 0.5f);

	// the game ring: a white rounded rect 3 dp larger on each side, under the tile
	if (game && lit_a) {
		int ring = NX_DPF(TILE_RING_DP);
		blitShape(dst, SHAPE_FILL, r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring, rad + ring, 0, 255,
				  lit_a);
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
	// the lit white fill (logo/tool/collection)
	if (!game && lit_a)
		blitShape(dst, SHAPE_FILL, r.x, r.y, r.w, r.h, rad, 0, 255, lit_a);

	// content: white at 82% → (fill kinds) black at 100%, (game kinds) white at 100%
	Uint8 a = (Uint8)(TILE_PLAIN_ALPHA + (255 - TILE_PLAIN_ALPHA) * lit + 0.5f);
	Uint8 c = game ? 255 : (Uint8)(255.0f * (1.0f - lit) + 0.5f);
	switch (t->kind) {
	case TILE_LOGO:
		drawLogo(dst, r, t, s, c, a);
		break;
	case TILE_TOOL:
		drawTool(dst, r, t, s, c, a);
		break;
	case TILE_COLLECTION:
		drawWord(dst, r, t->name, s, c, a, true);
		break;
	case TILE_GAME:
		if (t->picture) {
			drawPicture(dst, r, rad, t->picture);
			break;
		}
		// no screenshot: a title tile
		drawWord(dst, r, t->name, s, c, a, false);
		break;
	case TILE_TITLE:
		drawWord(dst, r, t->name, s, c, a, false);
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

int Tiles_textBlock(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w, int max_lines,
					bool camel_split, Uint8 grey, Uint8 alpha, bool shadow) {
	if (!f || !text || !text[0])
		return 0;
	Lines l;
	layoutLines(f, text, max_w, max_lines, camel_split, &l);
	int lh = TTF_FontHeight(f);
	if (dst) {
		for (int i = 0; i < l.n; i++) {
			int x = cx - textWidth(f, l.line[i]) / 2;
			if (shadow)
				blitTextShadow(dst, f, l.line[i], x, y + i * lh, alpha);
			blitText(dst, f, l.line[i], x, y + i * lh, grey, alpha);
		}
	}
	return l.n * lh;
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
			SDL_FreeSurface(masks[i].surface);
	}
	memset(masks, 0, sizeof(masks));
	for (int i = 0; i < CAPTION_SLOTS; i++) {
		if (captions[i].surface)
			SDL_FreeSurface(captions[i].surface);
	}
	memset(captions, 0, sizeof(captions));
	if (scratch)
		SDL_FreeSurface(scratch);
	scratch = NULL;
}
