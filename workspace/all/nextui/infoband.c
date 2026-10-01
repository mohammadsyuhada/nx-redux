// The info band on List screens (main-menu tabs and game lists): a fade that sits on the hint bar, the packed
// scroll arrows on the left and one right-aligned line of info segments ("Today - 1h 5m · 3 of 40 · Next: …").
// The whole band is one cached ARGB block drawn onto a GPU layer, rebuilt only when its content changes.

#include "infoband.h"

#include "api.h"
#include "defines.h"
#include "imgloader.h" // screen
#include "menuart.h"
#include "ui_fade.h"

#include <string.h>

#define SHADOW_ALPHA 153 // black at 60%
#define ARROW_ALPHA 51	 // white at 20%
#define SEPARATOR " \xC2\xB7 "
#define MAX_SEGS 3

// The trophy is 12 dp next to font.small; another font scales it by its line height.
static SDL_Surface* trophyIconFor(TTF_Font* f) {
	int size = NX_DP(12);
	if (f && f != font.small && font.small && TTF_FontHeight(font.small) > 0)
		size = size * TTF_FontHeight(f) / TTF_FontHeight(font.small);
	return MenuArt_get("menu_icon_achievements.png", size, size);
}

static int trophyWidthFor(TTF_Font* f) {
	SDL_Surface* icon = trophyIconFor(f);
	return icon ? icon->w + SCALE1(3) : 0;
}

static int textWidthFor(TTF_Font* f, const char* text) {
	int w = 0;
	if (text[0])
		GFX_measureText(f, text, &w, NULL);
	return w;
}

// Blit src with a black 60% copy under it, SCALE1(1) right/down. The shared (cached) icon gets its
// colour/alpha mods restored.
static void blitIconShadowed(SDL_Surface* icon, SDL_Surface* dst, int x, int y) {
	Uint8 r, g, b, a;
	SDL_GetSurfaceColorMod(icon, &r, &g, &b);
	SDL_GetSurfaceAlphaMod(icon, &a);
	SDL_SetSurfaceColorMod(icon, 0, 0, 0);
	SDL_SetSurfaceAlphaMod(icon, SHADOW_ALPHA);
	SDL_BlitSurface(icon, NULL, dst, &(SDL_Rect){x + SCALE1(1), y + SCALE1(1)});
	SDL_SetSurfaceColorMod(icon, r, g, b);
	SDL_SetSurfaceAlphaMod(icon, a);
	SDL_BlitSurface(icon, NULL, dst, &(SDL_Rect){x, y});
}

// Text with the dark shadow, its top-left at (x, line top + centring); returns its measured width
// (textWidth, the same measure the layout used, so advancing by it lands exactly where the layout said).
static int drawShadowedText(TTF_Font* f, SDL_Surface* dst, const char* text, SDL_Color color, int x, int line_y,
							int line_h) {
	if (!text[0])
		return 0;
	// the cached surface is shared: blit it as is, never change its alpha or format
	SDL_Surface* owned = NULL;
	SDL_Surface* surf = GFX_getCachedText(f, text, color);
	if (!surf)
		surf = owned = GFX_renderText(f, text, color);
	if (!surf)
		return 0;
	int ty = line_y + (line_h - surf->h) / 2;
	SDL_Surface* shadow = GFX_renderText(f, text, COLOR_BLACK);
	if (shadow) {
		SDL_SetSurfaceAlphaMod(shadow, SHADOW_ALPHA);
		SDL_BlitSurface(shadow, NULL, dst, &(SDL_Rect){x + SCALE1(1), ty + SCALE1(1)});
		SDL_FreeSurface(shadow);
	}
	SDL_BlitSurface(surf, NULL, dst, &(SDL_Rect){x, ty});
	if (owned)
		SDL_FreeSurface(owned);
	return textWidthFor(f, text);
}

// The scroll arrows, NX_DP(28) wide (aspect kept) at alpha 51, built once per scale. The asset sheet is
// shared, so each arrow is copied into a scratch surface first (white-transparent ground, so the art's soft
// edges stay white) and the scaled copy is the one faded.
#define ARROW_W NX_DP(28)
static SDL_Surface* arrow_up = NULL;
static SDL_Surface* arrow_down = NULL;
static float arrow_scale = 0;

static SDL_Surface* buildArrow(int asset) {
	SDL_Rect r;
	GFX_assetRect(asset, &r);
	if (r.w <= 0 || r.h <= 0)
		return NULL;
	SDL_Surface* tmp = SDL_CreateRGBSurfaceWithFormat(0, r.w, r.h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!tmp)
		return NULL;
	SDL_FillRect(tmp, NULL, SDL_MapRGBA(tmp->format, 255, 255, 255, 0));
	GFX_blitAsset(asset, NULL, tmp, &(SDL_Rect){0, 0});
	int out_h = (r.h * ARROW_W + r.w / 2) / r.w;
	SDL_Surface* out =
		SDL_CreateRGBSurfaceWithFormat(0, ARROW_W, out_h > 0 ? out_h : 1, 32, SDL_PIXELFORMAT_ARGB8888);
	if (out) {
		SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_NONE); // a straight scaled copy
		SDL_BlitScaled(tmp, NULL, out, NULL);
		SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
		SDL_SetSurfaceAlphaMod(out, ARROW_ALPHA);
	}
	SDL_FreeSurface(tmp);
	return out;
}

static void freeArrows(void) {
	if (arrow_up)
		SDL_FreeSurface(arrow_up);
	if (arrow_down)
		SDL_FreeSurface(arrow_down);
	arrow_up = arrow_down = NULL;
	arrow_scale = 0;
}

static void ensureArrows(void) {
	if (arrow_scale == (float)FIXED_SCALE && arrow_up && arrow_down)
		return;
	freeArrows();
	arrow_up = buildArrow(ASSET_SCROLL_UP);
	arrow_down = buildArrow(ASSET_SCROLL_DOWN);
	arrow_scale = (float)FIXED_SCALE;
}

static SDL_Color segColor(InfoSegKind kind) {
	return kind == INFO_SEG_NEXT ? COLOR_WHITE : COLOR_GRAY;
}

// The cached block and the key it was built for.
static struct {
	SDL_Surface* surf;
	InfoSeg segs[MAX_SEGS];
	int nsegs;
	bool up, down;
	int w, h, text_off, text_h, arrow_x;
	float scale;
	TTF_Font* font;
} block;

static bool keyMatches(const InfoSeg* segs, int nsegs, bool up, bool down, int w, int h, int text_off, int text_h,
					   int arrow_x) {
	if (!block.surf || block.nsegs != nsegs || block.up != up || block.down != down || block.w != w ||
		block.h != h || block.text_off != text_off || block.text_h != text_h || block.arrow_x != arrow_x ||
		block.scale != (float)FIXED_SCALE || block.font != font.small)
		return false;
	for (int i = 0; i < nsegs; i++)
		if (block.segs[i].kind != segs[i].kind || strcmp(block.segs[i].text, segs[i].text) != 0)
			return false;
	return true;
}

static int measureCb(void* ctx, const char* text) {
	return textWidthFor((TTF_Font*)ctx, text);
}

// Fit the segments into avail (infoband_layout.c's rules, host-tested): texts are cut in place.
static int fitSegments(TTF_Font* f, InfoSeg* segs, int n, int avail) {
	return InfoBand_fitSegments(segs, n, avail, textWidthFor(f, SEPARATOR), trophyWidthFor(f), measureCb, f);
}

int InfoBand_drawSegments(SDL_Surface* dst, const InfoSeg* in, int n, int x, bool align_right, int y, int max_w,
						  TTF_Font* f) {
	if (!dst || !in || n <= 0 || max_w <= 0 || !f)
		return 0;
	InfoSeg segs[MAX_SEGS];
	if (n > MAX_SEGS)
		n = MAX_SEGS;
	memcpy(segs, in, sizeof(InfoSeg) * n);
	n = fitSegments(f, segs, n, max_w);
	if (n == 0)
		return 0;

	int line_h = TTF_FontHeight(f);
	SDL_Surface* trophy = trophyIconFor(f);
	int trophy_w = trophyWidthFor(f);
	int sep_w = textWidthFor(f, SEPARATOR);
	int total = 0;
	for (int i = 0; i < n; i++)
		total += (i ? sep_w : 0) + textWidthFor(f, segs[i].text) + (segs[i].kind == INFO_SEG_ACH ? trophy_w : 0);
	if (align_right)
		x -= total;
	for (int i = 0; i < n; i++) {
		if (i)
			x += drawShadowedText(f, dst, SEPARATOR, COLOR_GRAY, x, y, line_h);
		if (segs[i].kind == INFO_SEG_ACH && trophy) {
			blitIconShadowed(trophy, dst, x, y + (line_h - trophy->h) / 2);
			x += trophy_w;
		}
		x += drawShadowedText(f, dst, segs[i].text, segColor(segs[i].kind), x, y, line_h);
	}
	return total;
}

int InfoBand_segmentsWidth(const InfoSeg* in, int n, int max_w, TTF_Font* f) {
	if (!in || n <= 0 || max_w <= 0 || !f)
		return 0;
	InfoSeg segs[MAX_SEGS];
	if (n > MAX_SEGS)
		n = MAX_SEGS;
	memcpy(segs, in, sizeof(InfoSeg) * n);
	n = fitSegments(f, segs, n, max_w);
	int trophy_w = trophyWidthFor(f);
	int sep_w = textWidthFor(f, SEPARATOR);
	int total = 0;
	for (int i = 0; i < n; i++)
		total += (i ? sep_w : 0) + textWidthFor(f, segs[i].text) + (segs[i].kind == INFO_SEG_ACH ? trophy_w : 0);
	return total;
}

static SDL_Surface* buildBlock(const InfoSeg* in, int nsegs, bool up, bool down, int w, int h, int text_off,
							   int text_h, int arrow_x) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 0));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);

	// the fade: ground at 80% across the bottom 2 dp, linear to 0 at the top
	SDL_Surface* fade = UI_bandFadeSurface(w, h, 0.8f, NX_DP(2));
	if (fade)
		SDL_BlitSurface(fade, NULL, s, NULL);

	// the arrows, packed: the first one shown takes slot 1, centred on the text line
	ensureArrows();
	int x = arrow_x;
	if (up && arrow_up) {
		SDL_BlitSurface(arrow_up, NULL, s, &(SDL_Rect){x, text_off + (text_h - arrow_up->h) / 2});
		x += ARROW_W + NX_DP(1);
	}
	if (down && arrow_down)
		SDL_BlitSurface(arrow_down, NULL, s, &(SDL_Rect){x, text_off + (text_h - arrow_down->h) / 2});

	if (nsegs <= 0 || !font.small)
		return s;

	// the text: reserved room for both arrows + a gap on the left (fixed, so the text never moves), right-aligned
	// at w - 24 dp
	int left = arrow_x + ARROW_W * 2 + NX_DP(1) + NX_DP(12);
	int right = w - NX_DP(24);
	InfoBand_drawSegments(s, in, nsegs, right, true, text_off, right - left, font.small);
	return s;
}

void InfoBand_render(const InfoBandLayout* layout, const InfoSeg* segs, int nsegs, bool up, bool down, int layer) {
	if (!screen || !layout)
		return;
	if (nsegs < 0 || !segs)
		nsegs = 0;
	if (nsegs > MAX_SEGS)
		nsegs = MAX_SEGS;
	const InfoBandLayout l = *layout;
	int w = screen->w;
	int h = l.band_bottom - l.band_top;
	int text_off = l.text_top - l.band_top;
	int arrow_x = l.arrow_x > 0 ? l.arrow_x : NX_DP(NX_LIST_INSET_DP);
	if (w <= 0 || h <= 0)
		return;

	if (!keyMatches(segs, nsegs, up, down, w, h, text_off, l.text_h, arrow_x)) {
		if (block.surf)
			SDL_FreeSurface(block.surf);
		block.surf = buildBlock(segs, nsegs, up, down, w, h, text_off, l.text_h, arrow_x);
		if (nsegs)
			memcpy(block.segs, segs, sizeof(InfoSeg) * nsegs);
		block.nsegs = nsegs;
		block.up = up;
		block.down = down;
		block.w = w;
		block.h = h;
		block.text_off = text_off;
		block.text_h = l.text_h;
		block.arrow_x = arrow_x;
		block.scale = (float)FIXED_SCALE;
		block.font = font.small;
	}
	if (block.surf)
		GFX_drawOnLayer(block.surf, 0, l.band_top, w, h, 1.0f, 0, layer);
}

void InfoBand_renderText(const InfoBandLayout* layout, const char* text, bool up, bool down, int layer) {
	InfoSeg seg = {.kind = INFO_SEG_COUNT};
	if (!text || !text[0]) {
		InfoBand_render(layout, NULL, 0, up, down, layer);
		return;
	}
	snprintf(seg.text, sizeof(seg.text), "%s", text);
	InfoBand_render(layout, &seg, 1, up, down, layer);
}

void InfoBand_quit(void) {
	if (block.surf)
		SDL_FreeSurface(block.surf);
	block.surf = NULL;
	freeArrows();
}
