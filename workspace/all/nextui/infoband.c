// The info band on List screens (main-menu tabs and game lists): a fade that rises from the hint bar's top, the
// packed scroll arrows on the left and one right-aligned line of info segments ("Today - 1h 5m · 3 of 40 · Next: …").
// The band is fixed just above the hints on both (InfoBand_fixedLayout): it reaches into the bar's empty top, its fill
// stopping at the bar.
// The whole band is one cached ARGB block drawn onto a GPU layer, rebuilt only when its content changes.

#include "infoband.h"

#include "api.h"
#include "defines.h"
#include "imgloader.h" // screen
#include "menuart.h"
#include "ui_fade.h"
#include "ui_font.h"
#include "ui_list.h"
#include "ui_text_sizes.h"

#include <string.h>

#define SHADOW_ALPHA 153				  // black at 60%
#define ARROW_ALPHA UI_SCROLL_ARROW_ALPHA // ui_list.h: every list's arrows (was 20% here: nearly invisible)
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
static void blitIconShadowed(SDL_Surface* icon, SDL_Surface* dst, int x, int y, bool shadowed) {
	if (!shadowed) {
		SDL_BlitSurface(icon, NULL, dst, &(SDL_Rect){x, y});
		return;
	}
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

void InfoBand_blitShadowed(SDL_Surface* s, SDL_Surface* dst, int x, int y, Uint8 shadow_a) {
	if (shadow_a) {
		Uint8 r, g, b, a;
		SDL_GetSurfaceColorMod(s, &r, &g, &b);
		SDL_GetSurfaceAlphaMod(s, &a);
		SDL_SetSurfaceColorMod(s, 0, 0, 0);
		SDL_SetSurfaceAlphaMod(s, shadow_a);
		SDL_BlitSurface(s, NULL, dst, &(SDL_Rect){x + SCALE1(1), y + SCALE1(1), s->w, s->h});
		SDL_SetSurfaceColorMod(s, r, g, b);
		SDL_SetSurfaceAlphaMod(s, a);
	}
	SDL_BlitSurface(s, NULL, dst, &(SDL_Rect){x, y, s->w, s->h});
}

// Text with the dark shadow (unless !shadowed), its top-left at (x, line top + centring); returns its measured width
// (textWidth, the same measure the layout used, so advancing by it lands exactly where the layout said).
static int drawShadowedText(TTF_Font* f, SDL_Surface* dst, const char* text, SDL_Color color, int x, int line_y,
							int line_h, bool shadowed) {
	if (!text[0])
		return 0;
	// Rendered fresh, not from the shared text cache (callers bake the result into their own cached surfaces), except
	// the separator, the same few glyphs every time. The shadow is the same render blitted black: a blended render
	// is its colour over the glyphs' coverage, so a black colour mod gives what rendering it black did, at one
	// render instead of two.
	bool shared = strcmp(text, SEPARATOR) == 0;
	SDL_Surface* surf = shared ? GFX_getCachedText(f, text, color) : NULL;
	if (!surf) {
		shared = false;
		surf = GFX_renderText(f, text, color);
	}
	if (!surf)
		return 0;
	int ty = line_y + (line_h - surf->h) / 2;
	// the shadow at SHADOW_ALPHA, as the black render's: whatever the text's own alpha
	InfoBand_blitShadowed(surf, dst, x, ty, shadowed ? SHADOW_ALPHA : 0);
	if (!shared)
		SDL_FreeSurface(surf);
	return textWidthFor(f, text);
}

// The scroll arrows, white, NX_DP(28) wide (aspect kept) at ARROW_ALPHA, built once per scale. The asset sheet is
// shared, so each arrow is copied into a scratch surface first (whitened, keeping the art's coverage) and the scaled
// copy is the one faded (its pixels' alpha).
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
	// the sheet's arrow art is dark grey (0x26, drawn for light grounds); the band's arrow is white at 20%: keep only
	// its coverage (alpha), in white
	if (SDL_MUSTLOCK(tmp))
		SDL_LockSurface(tmp);
	for (int y = 0; y < tmp->h; y++) {
		Uint32* px = (Uint32*)((Uint8*)tmp->pixels + y * tmp->pitch);
		for (int x = 0; x < tmp->w; x++)
			px[x] |= 0x00FFFFFF;
	}
	if (SDL_MUSTLOCK(tmp))
		SDL_UnlockSurface(tmp);
	int out_h = (r.h * ARROW_W + r.w / 2) / r.w;
	SDL_Surface* out =
		SDL_CreateRGBSurfaceWithFormat(0, ARROW_W, out_h > 0 ? out_h : 1, 32, SDL_PIXELFORMAT_ARGB8888);
	if (out) {
		SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_NONE); // a straight scaled copy
		SDL_BlitScaled(tmp, NULL, out, NULL);
		// ARROW_ALPHA baked into the pixels' alpha (an alpha mod on top of the block's blend and unpremultiply came
		// out squared on screen: 20% showed as 4%)
		if (SDL_MUSTLOCK(out))
			SDL_LockSurface(out);
		for (int y = 0; y < out->h; y++) {
			Uint32* px = (Uint32*)((Uint8*)out->pixels + y * out->pitch);
			for (int x = 0; x < out->w; x++)
				px[x] = (((px[x] >> 24) * ARROW_ALPHA + 127) / 255) << 24 | (px[x] & 0x00FFFFFF);
		}
		if (SDL_MUSTLOCK(out))
			SDL_UnlockSurface(out);
		SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
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

// The band's block (the fade and the arrows) and its info line are cached apart: a selection change brings new text
// (and, while its game info loads, none first) but the same block, so a step rebuilds and uploads only the text's
// strip. Two blocks are kept: with the button hints hidden the block's fade holds differently with and without an
// info line below the band (blockFor's `tall`), and a held D-pad goes between the two every step.
typedef struct {
	SDL_Surface* surf;
	bool up, down, tall;
	int w, h, fill_h, text_off, text_h, arrow_x;
	float scale;
	unsigned stamp; // last use (the older of the two goes)
} BandBlock;
static BandBlock blocks[2];
static unsigned block_stamp = 0;

// The info line's strip: band-relative rows y .. y + surf->h (the text's own line, its shadow and descenders).
static struct {
	SDL_Surface* surf; // NULL: no info line
	InfoSeg segs[MAX_SEGS];
	int nsegs;
	int w, info_off, text_off, text_h, arrow_x, y;
	float scale;
	TTF_Font* font;
	bool valid; // the key above describes surf (a NULL surf included)
} text;

// What the last InfoBand_prepare chose (the block and the text strip drawn by InfoBand_draw), and a number bumped
// whenever either changes.
static struct {
	BandBlock* blk;
	int band_top;
	unsigned gen;
} cur;

static int measureCb(void* ctx, const char* text) {
	return textWidthFor((TTF_Font*)ctx, text);
}

// Fit the segments into avail (infoband_layout.c's rules, host-tested): texts are cut in place.
static int fitSegments(TTF_Font* f, InfoSeg* segs, int n, int avail) {
	return InfoBand_fitSegments(segs, n, avail, textWidthFor(f, SEPARATOR), trophyWidthFor(f), measureCb, f);
}

int InfoBand_drawSegments(SDL_Surface* dst, const InfoSeg* in, int n, int x, bool align_right, int y, int max_w,
						  TTF_Font* f) {
	return InfoBand_drawSegmentsEx(dst, in, n, x, align_right, y, max_w, f, true);
}

int InfoBand_drawSegmentsEx(SDL_Surface* dst, const InfoSeg* in, int n, int x, bool align_right, int y, int max_w,
							TTF_Font* f, bool shadow) {
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
			x += drawShadowedText(f, dst, SEPARATOR, COLOR_GRAY, x, y, line_h, shadow);
		if (segs[i].kind == INFO_SEG_ACH && trophy) {
			blitIconShadowed(trophy, dst, x, y + (line_h - trophy->h) / 2, shadow);
			x += trophy_w;
		}
		x += drawShadowedText(f, dst, segs[i].text, segColor(segs[i].kind), x, y, line_h, shadow);
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

int InfoBand_separatorWidth(TTF_Font* f) {
	return f ? textWidthFor(f, SEPARATOR) : 0;
}

int InfoBand_trophyWidth(TTF_Font* f) {
	return f ? trophyWidthFor(f) : 0;
}

// Un-premultiply the block for its layer. The block is built on a black ground (clear (0,0,0,0), or the band's black
// fill), so SDL's BLEND leaves every pixel's colour multiplied by its alpha (P = c × a); the layer then multiplies by
// alpha again, and the 20% arrows and the text's soft edges came out near black (the fill rows included: an arrow on
// the fade's thin top read 1/4 of its colour). Dividing the colour back out, on every row, gives the straight colour
// the layer expects; the black fill and opaque pixels are unchanged.
static void unpremultiply(SDL_Surface* s) {
	if (!s || s->format->format != SDL_PIXELFORMAT_ARGB8888)
		return;
	if (SDL_MUSTLOCK(s) && SDL_LockSurface(s) != 0)
		return;
	for (int y = 0; y < s->h; y++) {
		Uint32* px = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
		for (int x = 0; x < s->w; x++) {
			Uint32 a = px[x] >> 24;
			if (a == 0 || a == 255 || !(px[x] & 0x00FFFFFF))
				continue;
			Uint32 r = ((px[x] >> 16 & 0xFF) * 255 + a / 2) / a;
			Uint32 g = ((px[x] >> 8 & 0xFF) * 255 + a / 2) / a;
			Uint32 b = ((px[x] & 0xFF) * 255 + a / 2) / a;
			px[x] = a << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
		}
	}
	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
}

static SDL_Surface* buildBlock(bool up, bool down, bool tall, int w, int h, int fill_h, int text_off, int text_h,
							   int arrow_x) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 0));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);

	// the fade: ground at 80% across the 2 dp above the hint bar's top, linear to 0 at the band's top; below the
	// bar's top (the band reaches into it) nothing, the bar's own 80% shows. A fill that reaches the band's
	// bottom (no bar under it) holds the 80% from the text line down instead, or light art would show through under
	// the text; not while an info line sits below the band (tall: the block then reached the screen's bottom).
	int hold = fill_h >= h && !tall ? h - text_off : NX_DP(2);
	SDL_Surface* fade = fill_h > 0 ? UI_bandFadeSurface(w, fill_h, 0.8f, hold) : NULL;
	if (fade) // onto the clear block: its own pixels, copied (no blend)
		UI_fillFade(fade, NULL, s, 0, 0);

	// the arrows, packed: the first one shown takes slot 1, centred on the text line, INFOBAND_ARROW_RAISE up
	ensureArrows();
	int x = arrow_x;
	if (up && arrow_up) {
		SDL_BlitSurface(arrow_up, NULL, s, &(SDL_Rect){x, text_off + (text_h - arrow_up->h) / 2 - INFOBAND_ARROW_RAISE});
		x += ARROW_W + NX_DP(1);
	}
	if (down && arrow_down)
		SDL_BlitSurface(arrow_down, NULL, s, &(SDL_Rect){x, text_off + (text_h - arrow_down->h) / 2 - INFOBAND_ARROW_RAISE});
	unpremultiply(s); // once per content change (the block is cached)
	return s;
}

// The info line on its own strip, rows y .. y + h of the band (h: the text's line with room for its shadow and
// descenders, cut at max_h). Drawn as it was into the block, on clear, then unpremultiplied the same way.
// The info line's font: on its own row (info_off past the band's text line: the hint bar's, while the hints are
// hidden) TEXT_LIST_INFO per device, else font.small.
static TTF_Font* infoFont(int info_off, int text_off) {
	if (info_off == text_off)
		return font.small;
	int px = (int)TextPx_for(TEXT_LIST_INFO, UIScale_deviceIndex(UI_DEVICE_NAME));
	TTF_Font* f = px > 0 && px != SCALE1(FONT_SMALL) ? UIFont_getPx(px, false) : NULL;
	return f ? f : font.small;
}

static SDL_Surface* buildText(const InfoSeg* in, int nsegs, int w, int text_off, int info_off, int text_h, int arrow_x,
							  int max_h, int* y) {
	int pad = NX_DP(4);
	int y0 = info_off - pad > 0 ? info_off - pad : 0;
	int h = info_off + text_h + pad - y0;
	if (y0 + h > max_h)
		h = max_h - y0;
	if (h <= 0 || w <= 0 || !font.small)
		return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 0));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	// reserved room for both arrows + a gap on the left (fixed, so the text never moves), right-aligned at
	// w - 24 dp; on its own line (info_off) from the arrows' x
	int left = info_off != text_off ? arrow_x : arrow_x + ARROW_W * 2 + NX_DP(1) + NX_DP(12);
	int right = w - NX_DP(24);
	TTF_Font* f = infoFont(info_off, text_off);
	// centred in the line box (a font other than font.small has its own height)
	int fy = info_off - y0 + (text_h - TTF_FontHeight(f)) / 2 - (text_h - TTF_FontHeight(font.small)) / 2;
	InfoBand_drawSegments(s, in, nsegs, right, true, fy, right - left, f);
	unpremultiply(s);
	*y = y0;
	return s;
}

static BandBlock* blockFor(bool up, bool down, bool tall, int w, int h, int fill_h, int text_off, int text_h,
						   int arrow_x) {
	BandBlock* victim = &blocks[0];
	for (int i = 0; i < 2; i++) {
		BandBlock* b = &blocks[i];
		if (b->surf && b->up == up && b->down == down && b->tall == tall && b->w == w && b->h == h &&
			b->fill_h == fill_h && b->text_off == text_off && b->text_h == text_h && b->arrow_x == arrow_x &&
			b->scale == (float)FIXED_SCALE) {
			b->stamp = ++block_stamp;
			return b;
		}
		if (!b->surf || b->stamp < victim->stamp)
			victim = b;
	}
	GFX_freeSurfaceAndTexture(victim->surf);
	*victim = (BandBlock){buildBlock(up, down, tall, w, h, fill_h, text_off, text_h, arrow_x),
						  up, down, tall, w, h, fill_h, text_off, text_h, arrow_x, (float)FIXED_SCALE, ++block_stamp};
	return victim->surf ? victim : NULL;
}

unsigned InfoBand_prepare(const InfoBandLayout* layout, const InfoSeg* segs, int nsegs, bool up, bool down) {
	if (!screen || !layout)
		return cur.gen;
	if (nsegs < 0 || !segs)
		nsegs = 0;
	if (nsegs > MAX_SEGS)
		nsegs = MAX_SEGS;
	const InfoBandLayout l = *layout;
	int w = screen->w;
	int h = l.band_bottom - l.band_top;
	int fill_h = (l.fill_bottom < l.band_bottom ? l.fill_bottom : l.band_bottom) - l.band_top;
	int text_off = l.text_top - l.band_top;
	int info_off = (l.info_top > 0 ? l.info_top : l.text_top) - l.band_top;
	// the info's own line below the band: its strip reaches the screen's bottom (the glyphs' descenders and shadow
	// run past text_h), and the block's fade holds as it did when the block reached there too
	bool tall = nsegs > 0 && info_off + l.text_h > h;
	int arrow_x = l.arrow_x > 0 ? l.arrow_x : NX_DP(NX_LIST_INSET_DP);
	if (w <= 0 || h <= 0)
		return cur.gen;

	BandBlock* blk = blockFor(up, down, tall, w, h, fill_h, text_off, l.text_h, arrow_x);
	bool text_same = text.valid && text.nsegs == nsegs && text.w == w && text.info_off == info_off &&
					 text.text_off == text_off && text.text_h == l.text_h && text.arrow_x == arrow_x &&
					 text.scale == (float)FIXED_SCALE && text.font == infoFont(info_off, text_off);
	for (int i = 0; text_same && i < nsegs; i++)
		if (text.segs[i].kind != segs[i].kind || strcmp(text.segs[i].text, segs[i].text) != 0)
			text_same = false;
	if (!text_same) {
		GFX_freeSurfaceAndTexture(text.surf);
		text.surf = nsegs > 0 ? buildText(segs, nsegs, w, text_off, info_off, l.text_h, arrow_x,
										  screen->h - l.band_top, &text.y)
							  : NULL;
		if (nsegs)
			memcpy(text.segs, segs, sizeof(InfoSeg) * nsegs);
		text.nsegs = nsegs;
		text.w = w;
		text.info_off = info_off;
		text.text_off = text_off;
		text.text_h = l.text_h;
		text.arrow_x = arrow_x;
		text.scale = (float)FIXED_SCALE;
		text.font = infoFont(info_off, text_off);
		text.valid = true;
		cur.gen++;
	}
	if (blk != cur.blk || l.band_top != cur.band_top)
		cur.gen++;
	cur.blk = blk;
	cur.band_top = l.band_top;
	return cur.gen;
}

void InfoBand_draw(int layer, SDL_Surface* dst) {
	SDL_Surface* parts[2] = {cur.blk ? cur.blk->surf : NULL, text.surf};
	int ys[2] = {cur.band_top, cur.band_top + text.y};
	for (int i = 0; i < 2; i++) {
		if (!parts[i])
			continue;
		if (dst)
			SDL_BlitSurface(parts[i], NULL, dst, &(SDL_Rect){0, ys[i]});
		else { // their textures made once per content
			SDL_Texture* tex = PLAT_textureForSurface(parts[i]);
			// the block copied onto the (cleared) layer, not blended: a blend leaves its colour times its alpha in the
			// layer and the layer's own blend onto the screen multiplies it again (the arrows' 45% showed as 20%). The
			// text strip blends over it (its glyphs' edges onto the block).
			if (tex && i == 0)
				SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
			PLAT_drawTextureOnLayer(tex, &(SDL_Rect){0, ys[i], parts[i]->w, parts[i]->h}, layer);
		}
	}
}

void InfoBand_render(const InfoBandLayout* layout, const InfoSeg* segs, int nsegs, bool up, bool down, int layer,
					 SDL_Surface* dst) {
	InfoBand_prepare(layout, segs, nsegs, up, down);
	InfoBand_draw(layer, dst);
}

void InfoBand_renderUpArrow(int x, int cy, int layer, SDL_Surface* dst) {
	static SDL_Surface* baked = NULL; // the arrow at ARROW_ALPHA, as the band bakes it into its block
	static float baked_scale = 0;
	ensureArrows();
	if (!arrow_up)
		return;
	if (!baked || baked_scale != (float)FIXED_SCALE) {
		if (baked)
			SDL_FreeSurface(baked);
		baked = SDL_CreateRGBSurfaceWithFormat(0, arrow_up->w, arrow_up->h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!baked)
			return;
		SDL_SetSurfaceBlendMode(baked, SDL_BLENDMODE_BLEND);
		// a straight copy (arrow_up carries ARROW_ALPHA in its pixels; a blend onto clear would darken the colour too:
		// the band undoes that with unpremultiply)
		SDL_Surface* src = SDL_ConvertSurfaceFormat(arrow_up, SDL_PIXELFORMAT_ARGB8888, 0);
		if (!src)
			return;
		for (int y = 0; y < src->h; y++) {
			const Uint32* in = (const Uint32*)((const Uint8*)src->pixels + y * src->pitch);
			Uint32* out = (Uint32*)((Uint8*)baked->pixels + y * baked->pitch);
			for (int x = 0; x < src->w; x++)
				out[x] = in[x];
		}
		SDL_FreeSurface(src);
		baked_scale = (float)FIXED_SCALE;
	}
	int y = cy - baked->h / 2;
	if (dst)
		SDL_BlitSurface(baked, NULL, dst, &(SDL_Rect){x, y});
	else
		GFX_drawOnLayer(baked, x, y, baked->w, baked->h, 1.0f, 0, layer);
}

void InfoBand_renderText(const InfoBandLayout* layout, const char* text, bool up, bool down, int layer,
						 SDL_Surface* dst) {
	InfoSeg seg = {.kind = INFO_SEG_COUNT};
	if (!text || !text[0]) {
		InfoBand_render(layout, NULL, 0, up, down, layer, dst);
		return;
	}
	snprintf(seg.text, sizeof(seg.text), "%s", text);
	InfoBand_render(layout, &seg, 1, up, down, layer, dst);
}

void InfoBand_quit(void) {
	for (int i = 0; i < 2; i++)
		GFX_freeSurfaceAndTexture(blocks[i].surf);
	memset(blocks, 0, sizeof(blocks));
	GFX_freeSurfaceAndTexture(text.surf);
	memset(&text, 0, sizeof(text));
	cur.blk = NULL;
	freeArrows();
}
