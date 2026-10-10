// The Home tab, B2 (docs/home-b2.md; home.h): the stats strip under the tab row, the top section (Continue and the tool
// squares) and the pinned games in rows under it. home_layout.c owns the geometry and the D-pad in Brick px,
// home_strip.c the strip's text; this file turns them into pixels, draws and handles input. With Layouts > Home
// layout on Carousel, home_carousel_layout.c's row (Continue and the pinned games) and dock (the tools) take the
// place of the top section and the pins; the strip, the cards' looks, the hints, A and MENU are shared. On List
// (home_list_layout.c), gamelist.c draws the rows as its main-menu List over a list of Home's own (Home_listDir); this
// file keeps the strip, the Continue tag, the hints and the input. A List with no Continue (a fresh install) draws
// the Carousel's Pick a game instead.
//
// Drawing: the page is painted straight onto the screen, clipped to the band between the tab strip and the hint bar
// so scrolled tiles are cut at both bars. Each tile is composed once into its own surface (base, picture, fade, text,
// edge, anti-aliased corners) and cached under a key of its kind, size, look (plain/lit) and a stamp of what it shows;
// a frame only blits cached tiles (and the strip, cached whole), and a tile is recomposed only when its key changes. A
// selection crossfade blends the cached lit look over the plain one (the ring at the same alpha).
//
// Frame cost (the Brick's A133 draws the UI in software): SDL's per-pixel-alpha blit runs ~20 ns a pixel there, so a
// tile goes through it only for its four anti-aliased corners. Everything else in a tile is opaque and is copied, or
// for a crossfade lerped, by UI_blitOpaque; the hint bar is cached whole and the fades use UI_blitFade.

#include "home.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_text_sizes.h"
#include "utils.h"

#include "content.h"
#include "contentdim.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gamelist.h"
#include "home_carousel_layout.h"
#include "home_layout.h"
#include "home_list_layout.h"
#include "home_stats.h"
#include "home_strip.h"
#include "homeart.h"
#include "imgloader.h" // screen
#include "infoband.h"
#include "launcher.h"
#include "list_window.h"
#include "menuart.h"
#include "menutabs.h"
#include "placeholder_art.h"
#include "row_model.h"
#include "ui_accent.h"
#include "ui_font.h"
#include "ui_list.h"
#include "tiles.h"
#include "ui_buttonhintbar.h"
#include "ui_contextmenu.h"
#include "ui_ease.h"
#include "ui_fade.h"
#include "view_common.h"

#define BORDER_ALPHA 20 // white at 8%
#define SEL_MS 120		// selection crossfade
#define SCROLL_MS 320	// page scroll, UI_easeStandard
#define SLIDE_MS 300	// the Carousel row's slide, UI_easeStandard (as the game lists' Carousel)
#define ELLIPSIS "\xE2\x80\xA6"
#define LINE_MAX 256

// The B2 type (Brick px; docs/home-b2.md)
#define STRIP_PX 27.0f
#define CONT_TITLE_PX 44.0f
#define CONT_SUB_PX 31.0f
#define CONT_INSET 29.0f
#define CONT_TITLE_UP 74.0f // the title's baseline above the card's bottom
#define CONT_SUB_UP 31.0f
#define PIN_NAME_PX 33.8f
#define PIN_NAME_LH 41.6f

static const SDL_Color C_WHITE = {255, 255, 255, 255};
static const SDL_Color C_BLACK = {0, 0, 0, 255};
static const SDL_Color C_GREY = {0x99, 0x99, 0x99, 255}; // COLOR_GRAY, the hint grey
static const SDL_Color C_DOT = {0x55, 0x55, 0x55, 255};	 // the strip's middle dot

// A card's ground and ink: black / white plain, the accent / its ink lit (selected), opaque (only the List pill wears
// Color 1's opacity).
static SDL_Color opaque(SDL_Color c) {
	c.a = 255;
	return c;
}
static SDL_Color cardBg(bool lit) {
	return lit ? opaque(UI_accent()) : C_BLACK;
}
static SDL_Color cardInk(bool lit) {
	return lit ? opaque(UI_onAccent()) : C_WHITE;
}
// An icon or mark lit: tinted to the ink (black at the default theme), its alpha unchanged.
static void tintLit(SDL_Surface* art) {
	SDL_Color ink = cardInk(true);
	SDL_SetSurfaceColorMod(art, ink.r, ink.g, ink.b);
}

///////////////////////////////////////
// State

static bool need_rebuild = true; // Home_reset: the pins or Continue may have changed
// The stats are asked for each time Home becomes the shown view (boot into Home, a tab step onto it, a list popped
// back to it), so a midnight rollover or an RA sync is picked up on the next show. Not on boot before a show nor on
// Home_reset (a pin from inside Home): the pins and Continue don't feed the stats. A request is cheap when nothing
// changed (home_stats.c compares its input key off the UI thread; a miss re-parses every cached RA game).
static bool was_active = false; // Home_active()'s last answer: the false → true edge sets stats_due
static bool stats_due = false;
static unsigned built_root = 0; // the root Directory's serial (0 = none)
static int built_w = 0, built_h = 0, built_lines = -1;
static float built_scale = 0;

static HomeLayout layout;
static Entry* cont = NULL; // owned; NULL = no Continue (the Pick-a-game card takes its slot)
static char cont_preview[MAX_PATH];
static Entry* games[HOME_MAX_PINS];		   // borrowed from stack[0], valid while its serial is built_root
static bool game_plain_dir[HOME_MAX_PINS]; // a legacy pinned folder (no cue/m3u): A opens it
static int ngames = 0;
static Entry* tools[HOME_MAX_PINS];
static int ntools = 0;

static HomeFocus focus = {HOME_SEC_TOP, 0, 0, -1};

// The Carousel (Layouts > Home layout): built instead of `layout` while it is the stored style (read on each use; the
// settings pak restarts nextui anyway)
static int built_style = -1;
static HomeCarLayout car;
static HomeCarFocus car_focus = {false, 0, 0};
static HomeTile car_tile;												   // the focused item as a tile (focusedTile)
static int car_full_w = 0, car_full_h = 0, car_side_w = 0, car_side_h = 0; // the centre tile and a neighbour (px)
static float pos_from = 0, pos_to = 0;									   // the row's position: the selection's index, tweened between
static Tween slide_tw;
static bool focus_can_resume = false;

// The List (Layouts > Home layout: List, with a Continue): its rows as a Directory of Home's own for gamelist.c's List
// (the entries borrowed: Continue's and stack[0]'s), its serial apart from every Directory_new's (they count up from
// 1), so the pill snaps on a rebuild as on a new list
static Directory hl_dir;
static Array* hl_entries = NULL;
static unsigned hl_gen = 0;
static HomeListGeom hl_geom;
static HomeTile hl_tile;						// the selected row as a tile (focusedTile)
static SDL_Surface* tag_surf[2] = {NULL, NULL}; // the Continue tag, plain and lit (listTag)
static Uint32 tag_key[2] = {0, 0};

static int prev_id = -1; // the tile the selection crossfades from (tileId)
static Tween sel_tw;
static float scroll_from = 0, scroll_to = 0; // Brick px
static Tween scroll_tw;

static SDL_Surface* hint_bar = NULL; // the rendered hint bar (see renderHints)
static char hint_key[128];
static SDL_Surface* strip_surf = NULL; // the strip, rendered whole (see drawStrip)
static Uint32 strip_key = 0;
static int strip_top = 0; // the surface's top on the unscrolled page (px)

static void cardCacheClear(void);
static void stretchFree(void);
static void ringFree(void);

///////////////////////////////////////
// Units and timing

// Home's scale: two thirds of the UI scale (the Brick's 2 at 3.0), so Home is one physical size on every device too.
static float homeScale(void) {
	return roundf(FIXED_SCALE * 2.0f / 3.0f * 16.0f) / 16.0f;
}

// An sp for UIFont_get / Tiles_fitWordsSp / NX_SP (which scale by FIXED_SCALE) that comes out at Home's scale.
static float homeSp(float sp) {
	return sp * homeScale() / FIXED_SCALE;
}

// NX_DP at Home's scale.
static int homeDp(float dp) {
	return (int)(dp * homeScale() * 30.0f / 42.0f + 0.5f);
}

// Screen px per Brick px (Home's layout is in the Brick's 3x px): 2/3 on the Brick.
static float unitPx(void) {
	return homeScale() / 3.0f;
}

static int px(float bpx) {
	return (int)floorf(bpx * unitPx() + 0.5f);
}

// The stats strip is drawn at the UI scale, not Home's: its Brick px at FIXED_SCALE / 3 (× this in px()), 1:1 on the Brick.
static float stripK(void) {
	return FIXED_SCALE / homeScale();
}

// The stats strip's text size in px (ui_text_sizes.h: set per device), and that over the size the UI scale alone gives
// it (STRIP_PX at Home's scale), for the strip's spacing to grow with it.
static int statsPx(void) {
	return (int)TextPx_for(TEXT_HOME_STATS, UIScale_deviceIndex(UI_DEVICE_NAME));
}
static float statsTextK(void) {
	int base = px(STRIP_PX * stripK());
	return base > 0 ? (float)statsPx() / base : 1.0f;
}

// The List with no Continue (a fresh install) draws the Carousel's Pick a game (and its dock) instead.
static bool carousel(void) {
	return built_style == HOME_STYLE_CAROUSEL || (built_style == HOME_STYLE_LIST && !cont);
}

static bool listMode(void) {
	return built_style == HOME_STYLE_LIST && cont;
}

static float currentPos(void) {
	if (!slide_tw.active)
		return pos_to;
	return pos_from + (pos_to - pos_from) * UI_easeStandard(tweenProgress(&slide_tw, SLIDE_MS));
}

static float currentScroll(void) {
	if (!scroll_tw.active)
		return scroll_to;
	return scroll_from + (scroll_to - scroll_from) * UI_easeStandard(tweenProgress(&scroll_tw, SCROLL_MS));
}

static SDL_Rect toScreen(HomeRect r, int scroll_px) {
	int x0 = px(r.x), x1 = px(r.x + r.w);
	int y0 = px(r.y), y1 = px(r.y + r.h);
	return (SDL_Rect){x0, y0 - scroll_px, x1 - x0, y1 - y0};
}

static int radiusPx(void) {
	return px(HOME_RADIUS);
}


///////////////////////////////////////
// Pixels (ARGB8888 surfaces this file creates; software, no locking)

// Source-over of (r, g, b) at coverage a (0..1) onto one pixel, inside the clip rect.
static inline void blendPx(SDL_Surface* s, int x, int y, Uint8 r, Uint8 g, Uint8 b, float a) {
	const SDL_Rect* c = &s->clip_rect;
	if (a <= 0.0f || x < c->x || y < c->y || x >= c->x + c->w || y >= c->y + c->h)
		return;
	Uint32* p = (Uint32*)((Uint8*)s->pixels + y * s->pitch) + x;
	if (a >= 1.0f) {
		*p = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
		return;
	}
	Uint32 d = *p;
	Uint32 col = ((Uint32)r << 16) | ((Uint32)g << 8) | b;
	if ((d >> 24) == 0) { // over clear: the colour at a, premultiplied as SDL's blend leaves it (the screen is
		// composited premultiplied: a straight colour here showed full white at any a, e.g. the ring at 40%)
		Uint32 a8 = (Uint32)(a * 255.0f + 0.5f);
		Uint32 rb = (col & 0x00FF00FFu) * a8 + 0x00800080u;
		rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
		Uint32 gg = ((col >> 8) & 0xFFu) * a8 + 0x80u;
		gg = ((gg + (gg >> 8)) >> 8) & 0xFFu;
		*p = (a8 << 24) | rb | (gg << 8);
		return;
	}
	if ((d >> 24) == 255) { // over opaque: a lerp (exact /255, two lanes per multiply)
		Uint32 a8 = (Uint32)(a * 255.0f + 0.5f), ia = 255 - a8;
		Uint32 rb = (col & 0x00FF00FFu) * a8 + (d & 0x00FF00FFu) * ia + 0x00800080u;
		rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
		Uint32 gg = ((col >> 8) & 0xFFu) * a8 + ((d >> 8) & 0xFFu) * ia + 0x80u;
		gg = ((gg + (gg >> 8)) >> 8) & 0xFFu;
		*p = 0xFF000000u | rb | (gg << 8);
		return;
	}
	float da = ((d >> 24) & 0xFF) / 255.0f;
	float k = da * (1.0f - a);
	float oa = a + k;
	if (oa <= 0.0f) {
		*p = 0;
		return;
	}
	Uint32 orr = (Uint32)((r * a + ((d >> 16) & 0xFF) * k) / oa + 0.5f);
	Uint32 og = (Uint32)((g * a + ((d >> 8) & 0xFF) * k) / oa + 0.5f);
	Uint32 ob = (Uint32)((b * a + (d & 0xFF) * k) / oa + 0.5f);
	*p = ((Uint32)(oa * 255.0f + 0.5f) << 24) | (orr << 16) | (og << 8) | ob;
}

static void fillRectA(SDL_Surface* s, int x, int y, int w, int h, SDL_Color c, int alpha) {
	if (w <= 0 || h <= 0 || alpha <= 0)
		return;
	if (alpha >= 255) {
		SDL_FillRect(s, &(SDL_Rect){x, y, w, h}, SDL_MapRGBA(s->format, c.r, c.g, c.b, 255));
		return;
	}
	SDL_Rect r = {x, y, w, h}, clipped;
	if (!SDL_IntersectRect(&r, &s->clip_rect, &clipped))
		return;
	float a = alpha / 255.0f;
	for (int py = clipped.y; py < clipped.y + clipped.h; py++)
		for (int px = clipped.x; px < clipped.x + clipped.w; px++)
			blendPx(s, px, py, c.r, c.g, c.b, a);
}

static float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// The four r×r corner squares of a rounded rect: coverage of the ring between radius r_out and r_in
// (r_in <= 0: the whole quarter disc), times a. Corner centres sit r in from each edge.
static void paintCorners(SDL_Surface* s, int x, int y, int w, int h, int r, float r_in, SDL_Color c, float a) {
	int xs[2] = {x, x + w - r}, ys[2] = {y, y + h - r};
	float cxs[2] = {x + r, x + w - r}, cys[2] = {y + r, y + h - r};
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++)
			for (int py = ys[j]; py < ys[j] + r; py++)
				for (int px = xs[i]; px < xs[i] + r; px++) {
					float dx = px + 0.5f - cxs[i], dy = py + 0.5f - cys[j];
					float d = sqrtf(dx * dx + dy * dy);
					float cov = clamp01(r - d + 0.5f);
					if (r_in > 0.0f)
						cov -= clamp01(r_in - d + 0.5f);
					if (cov > 0.0f)
						blendPx(s, px, py, c.r, c.g, c.b, cov * a);
				}
}

static int clampRadius(int r, int w, int h) {
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	return r < 0 ? 0 : r;
}

static void fillRounded(SDL_Surface* s, int x, int y, int w, int h, int r, SDL_Color c, int alpha) {
	if (w <= 0 || h <= 0 || alpha <= 0)
		return;
	r = clampRadius(r, w, h);
	fillRectA(s, x, y + r, w, h - 2 * r, c, alpha);
	fillRectA(s, x + r, y, w - 2 * r, r, c, alpha);
	fillRectA(s, x + r, y + h - r, w - 2 * r, r, c, alpha);
	if (r > 0)
		paintCorners(s, x, y, w, h, r, -1.0f, c, alpha / 255.0f);
}

// A t px outline just inside (x, y, w, h), rounded r.
static void strokeRounded(SDL_Surface* s, int x, int y, int w, int h, int r, int t, SDL_Color c, int alpha) {
	if (w <= 0 || h <= 0 || t <= 0 || alpha <= 0)
		return;
	r = clampRadius(r, w, h);
	fillRectA(s, x + r, y, w - 2 * r, t, c, alpha);
	fillRectA(s, x + r, y + h - t, w - 2 * r, t, c, alpha);
	fillRectA(s, x, y + r, t, h - 2 * r, c, alpha);
	fillRectA(s, x + w - t, y + r, t, h - 2 * r, c, alpha);
	if (r > 0)
		paintCorners(s, x, y, w, h, r, (float)(r - t), c, alpha / 255.0f);
}

// Multiply the alpha of the corner squares by the rounded shape's coverage (anti-aliased corners).
static void maskCorners(SDL_Surface* s, int w, int h, int r) {
	r = clampRadius(r, w, h);
	if (r <= 0)
		return;
	int xs[2] = {0, w - r}, ys[2] = {0, h - r};
	float cxs[2] = {(float)r, (float)(w - r)}, cys[2] = {(float)r, (float)(h - r)};
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++)
			for (int py = ys[j]; py < ys[j] + r; py++) {
				Uint32* row = (Uint32*)((Uint8*)s->pixels + py * s->pitch);
				for (int px = xs[i]; px < xs[i] + r; px++) {
					float dx = px + 0.5f - cxs[i], dy = py + 0.5f - cys[j];
					float cov = clamp01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
					if (cov >= 1.0f)
						continue;
					Uint32 a = (Uint32)(((row[px] >> 24) & 0xFF) * cov + 0.5f);
					row[px] = (row[px] & 0x00FFFFFFu) | (a << 24);
				}
			}
}

///////////////////////////////////////
// Text

static int textW(TTF_Font* f, const char* text) {
	int w = 0;
	if (f && text && text[0])
		GFX_measureText(f, text, &w, NULL);
	return w;
}

// Text with its top-left at (x, y), at alpha. Rendered fresh, not through GFX_getCachedText: cards are composed
// rarely (they are cached whole), and Home's many strings would only evict the lists' text from that shared cache.
static void drawText(SDL_Surface* dst, TTF_Font* f, const char* text, SDL_Color c, int x, int y, int alpha) {
	if (!f || !text || !text[0] || alpha <= 0)
		return;
	SDL_Surface* surf = GFX_renderText(f, text, c);
	if (!surf)
		return;
	if (alpha < 255)
		SDL_SetSurfaceAlphaMod(surf, (Uint8)alpha);
	SDL_BlitSurface(surf, NULL, dst, &(SDL_Rect){x, y});
	SDL_FreeSurface(surf);
}

static void drawTextBaseline(SDL_Surface* dst, TTF_Font* f, const char* text, SDL_Color c, int x, int baseline,
							 int alpha) {
	if (f)
		drawText(dst, f, text, c, x, baseline - TTF_FontAscent(f), alpha);
}

// `in` cut to max_w with an ellipsis (out: LINE_MAX). Empty when nothing fits.
static void ellipsize(TTF_Font* f, const char* in, char* out, int max_w) {
	char src[LINE_MAX];
	snprintf(src, sizeof(src), "%s", in ? in : "");
	out[0] = '\0';
	if (!f || max_w <= 0 || !src[0])
		return;
	if (textW(f, src) <= max_w) {
		snprintf(out, LINE_MAX, "%s", src);
		return;
	}
	char cut[LINE_MAX + 8];
	int w = GFX_truncateText(f, src, cut, max_w, 0);
	if (w <= max_w && strcmp(cut, "...") != 0)
		snprintf(out, LINE_MAX, "%s", cut);
}

// Greedy word wrap into at most max_lines lines; the last line takes the rest, ellipsized. Returns the count.
static int wrapLines(TTF_Font* f, const char* text, int max_w, int max_lines, char lines[][LINE_MAX]) {
	char work[LINE_MAX];
	snprintf(work, sizeof(work), "%s", text ? text : "");
	int n = 0;
	const char* p = work;
	while (*p == ' ')
		p++;
	while (*p && n < max_lines) {
		if (n == max_lines - 1) { // the rest, cut to fit
			ellipsize(f, p, lines[n], max_w);
			if (lines[n][0])
				n++;
			break;
		}
		// take words while they fit; a single word too wide is cut on its own line
		char line[LINE_MAX] = "";
		const char* q = p;
		const char* taken_end = p;
		while (*q) {
			const char* word_end = strchr(q, ' ');
			if (!word_end)
				word_end = q + strlen(q);
			char cand[LINE_MAX];
			snprintf(cand, sizeof(cand), "%.*s", (int)(word_end - p), p);
			if (line[0] && textW(f, cand) > max_w)
				break;
			snprintf(line, sizeof(line), "%s", cand);
			taken_end = word_end;
			q = word_end;
			while (*q == ' ')
				q++;
		}
		ellipsize(f, line, lines[n], max_w);
		if (lines[n][0])
			n++;
		p = taken_end;
		while (*p == ' ')
			p++;
	}
	return n;
}


///////////////////////////////////////
// Data

bool Home_active(void) {
	bool active = stack && stack->count == 1 && MenuTabs_current() == MENU_TAB_HOME;
	// gamelist.c asks every frame (input and render), so this sees every show; main thread only
	if (active && !was_active)
		stats_due = true;
	was_active = active;
	return active;
}

static void ensureBuilt(void);

static const HomeTile* focusedTile(void) {
	if (listMode()) { // the selected row: Continue, a game or a tool
		HomeListItem it = HomeList_item(hl_dir.selected, ngames, ntools);
		HomeTileKind kind = it.kind == HOMELIST_GAME ? HOME_TILE_GAME : it.kind == HOMELIST_TOOL ? HOME_TILE_TOOL
																								 : HOME_TILE_CONTINUE;
		hl_tile = (HomeTile){kind, it.ref, {0, 0, 0, 0}};
		return &hl_tile;
	}
	if (carousel()) { // the dock's square, else the row's item: Continue (or Pick a game) first, then the games
		if (car_focus.dock && car_focus.tool < car.ntools)
			car_tile = (HomeTile){HOME_TILE_TOOL, car_focus.tool, {0, 0, 0, 0}};
		else if (car_focus.sel > 0 && car_focus.sel <= ngames)
			car_tile = (HomeTile){HOME_TILE_GAME, car_focus.sel - 1, {0, 0, 0, 0}};
		else
			car_tile = (HomeTile){HOME_TILE_CONTINUE, 0, {0, 0, 0, 0}};
		return &car_tile;
	}
	if (focus.sec == HOME_SEC_PINS && focus.pin >= 0 && focus.pin < layout.npins)
		return &layout.pins[focus.pin];
	if (focus.top >= 0 && focus.top < layout.ntop)
		return &layout.top[focus.top];
	return NULL;
}

static Entry* tileEntry(const HomeTile* t) {
	if (!t)
		return NULL;
	switch (t->kind) {
	case HOME_TILE_CONTINUE:
		return cont;
	case HOME_TILE_GAME:
		return t->ref >= 0 && t->ref < ngames ? games[t->ref] : NULL;
	case HOME_TILE_TOOL:
		return t->ref >= 0 && t->ref < ntools ? tools[t->ref] : NULL;
	}
	return NULL;
}

static bool plainDir(const HomeTile* t) {
	return t && t->kind == HOME_TILE_GAME && t->ref >= 0 && t->ref < ngames && game_plain_dir[t->ref];
}

Entry* Home_focusedEntry(void) {
	if (!Home_active())
		return NULL;
	ensureBuilt(); // the pins are borrowed from stack[0]: never hand out one from a replaced list
	return tileEntry(focusedTile());
}

// The focused game's resume state for the hint (readyResume stats the SD card: once per focus change).
static void readyFocus(void) {
	const HomeTile* t = focusedTile();
	Entry* e = tileEntry(t);
	focus_can_resume = false;
	if (!e || e->type == ENTRY_PAK || plainDir(t))
		return;
	readyResume(e);
	focus_can_resume = resume.can_resume;
}

// The strip's input from the latest stats (not ready before the first result).
static StripInput stripInput(const HomeStats* st) {
	StripInput in = {0};
	in.fresh = !cont && ngames == 0 && ntools == 0;
	in.ready = st->ready;
	in.signed_in = CFG_getRAEnable() && CFG_getRAAuthenticated();
	in.total = st->total;
	in.unlocks = st->unlocks < 0 ? 0 : st->unlocks;
	if (st->ready && st->ntop > 0) {
		in.top_title = st->top[0].title;
		in.top_seconds = st->top[0].seconds;
	}
	return in;
}

static bool currentStats(HomeStats* st) {
	memset(st, 0, sizeof(*st));
	if (!HomeStats_get(st))
		st->ready = false;
	return st->ready;
}

// None with Layouts > Extra info hidden: the page lays out as a fresh install's, without the strip's band.
static int stripLines(void) {
	if (!CFG_getExtraInfo())
		return 0;
	HomeStats st;
	currentStats(&st);
	StripInput in = stripInput(&st);
	return HomeStrip_lineCount(&in);
}

// The strip's ink above and below a baseline (px): its tallest letters ("This", "Most") and its descenders ("played"),
// not the font's ascent and descent, so the spaces the Carousel makes equal around the strip read equal.
static void stripInk(int* up, int* down) {
	*up = *down = 0;
	TTF_Font* f = UIFont_getPx(statsPx(), false);
	if (!f)
		return;
	const char* tall = "ThMdl";
	const char* low = "ypg";
	for (const char* c = tall; *c; c++) {
		int miny, maxy;
		if (TTF_GlyphMetrics(f, (Uint16)*c, NULL, NULL, &miny, &maxy, NULL) == 0 && maxy > *up)
			*up = maxy;
	}
	for (const char* c = low; *c; c++) {
		int miny, maxy;
		if (TTF_GlyphMetrics(f, (Uint16)*c, NULL, NULL, &miny, &maxy, NULL) == 0 && -miny > *down)
			*down = -miny;
	}
	if (*up <= 0) // no metrics: the font's own
		*up = TTF_FontAscent(f);
	if (*down <= 0)
		*down = -TTF_FontDescent(f);
}

// The Carousel's geometry: its centre tile capped at, and its gap the same as, the game lists' Carousel on this screen
// (rowview.c computeGeo: Row_sizes at the UI scale, the selection CAROUSEL_GAME_SEL_K bigger), in Home's units. The row
// snaps to its selection (no slide across a rebuild).
static void carRelayout(int lines) {
	float u = unitPx(), pd = pxPerDp();
	UIDevice dev = UIScale_deviceIndex(UI_DEVICE_NAME);
	int ink_up = 0, ink_down = 0;
	stripInk(&ink_up, &ink_down);
	RowSizes sz = Row_sizes(ROW_CAROUSEL, screen->w / pd, screen->h / pd - 2 * BAR_DP);
	float sel_k = TextPx_for(CAROUSEL_GAME_SEL_K, dev);
	HomeCarOpts o = {stripK(), statsTextK(), ink_up / u, ink_down / u,
					 sz.item_h * (sel_k > 0 ? sel_k : 1.0f) * pd / u, sz.gap * pd / u,
					 (barPx() - SCALE1(2)) / u, // menutabs.c draws the underline SCALE1(2) above the bar's bottom
					 !cont};
	HomeCar_compute(screen->w / u, screen->h / u, barPx() / u, lines, &o, ngames, ntools, &car);
	car_focus = HomeCar_clampFocus(&car, car_focus);
	car_full_w = px(car.tile_w);
	car_full_h = px(car.tile_h);
	car_side_w = (int)(car_full_w * car.side_scale + 0.5f);
	car_side_h = (int)(car_full_h * car.side_scale + 0.5f);
	pos_from = pos_to = (float)car_focus.sel;
	slide_tw.active = false;
	sel_tw.active = false;
	scroll_from = scroll_to = 0;
	scroll_tw.active = false;
}

// The List's strip and first row (home_list_layout.c): the strip's lines 8 dp under the tab row at its own size and
// line step (Grid's: 36 Brick px at the strip's scale), the rows 12 dp under it; no strip, the main menu List's top.
// The window kept on the selection for the rows that now fit.
static void listRelayout(int lines) {
	TTF_Font* f = UIFont_getPx(statsPx(), false);
	HomeListOpts o = {barPx(), NX_DP(8), NX_DP(12), barPx() + NX_DP(12), f ? TTF_FontAscent(f) : 0,
					  f ? -TTF_FontDescent(f) : 0, px(36.0f * stripK() * statsTextK())};
	HomeList_compute(lines, &o, &hl_geom);
	int n = hl_entries ? hl_entries->count : 0;
	hl_dir.selected = ListWindow_reload(n, GameList_rowCountFrom(hl_geom.list_top), hl_dir.selected, &hl_dir.start,
										&hl_dir.end);
	sel_tw.active = false;
	scroll_from = scroll_to = 0;
	scroll_tw.active = false;
}

// The List's rows: Continue, the pinned games, the pinned tools (home_list_layout.c's order). Home_reset puts the
// selection back on Continue; any other rebuild keeps it on its row (clamped).
static void listBuild(bool to_continue) {
	if (!hl_entries)
		hl_entries = Array_new();
	if (!hl_entries)
		return;
	hl_entries->count = 0;
	int n = HomeList_count(ngames, ntools);
	for (int i = 0; i < n; i++) {
		HomeListItem it = HomeList_item(i, ngames, ntools);
		Array_push(hl_entries, it.kind == HOMELIST_CONTINUE ? cont : it.kind == HOMELIST_GAME ? games[it.ref]
																							  : tools[it.ref]);
	}
	Directory* root = stack->items[0];
	hl_dir.path = root->path;
	hl_dir.name = root->name;
	hl_dir.entries = hl_entries;
	hl_dir.serial = 0x80000000u | (++hl_gen & 0x7FFFFFFFu);
	hl_dir.selected = to_continue ? 0 : HomeList_clampSel(hl_dir.selected, ngames, ntools);
}

// The geometry for this screen and strip; the focus kept in range, the scroll kept, clamped (no tween across it).
static void relayout(int lines) {
	float u = unitPx();
	built_lines = lines;
	if (listMode()) {
		listRelayout(lines);
		return;
	}
	if (carousel()) {
		carRelayout(lines);
		return;
	}
	UIDevice dev = UIScale_deviceIndex(UI_DEVICE_NAME);
	HomeLayoutOpts opts = {stripK(), statsTextK(), TextPx_for(HOME_PIN_K, dev), (int)TextPx_for(HOME_TOOL_ROWS, dev),
						   (int)TextPx_for(HOME_WIDE_PIN_COLS, dev)};
	HomeLayout_computeOpts(screen->w / u, screen->h / u, barPx() / u, lines, &opts, ngames, ntools, &layout);
	focus = HomeLayout_clampFocus(&layout, focus);
	scroll_to = HomeLayout_scrollFor(&layout, focus, screen->h / u, barPx() / u, scroll_to);
	scroll_from = scroll_to;
	scroll_tw.active = false;
	sel_tw.active = false;
}

static void rebuild(void) {
	if (!screen || !stack || stack->count < 1)
		return;
	bool reset = need_rebuild;
	need_rebuild = false;
	built_root = ((Directory*)stack->items[0])->serial;
	built_w = screen->w;
	built_h = screen->h;
	built_scale = homeScale();
	built_style = CFG_getHomeStyle();

	if (cont)
		Entry_free(cont);
	cont = Home_continueEntry();
	cont_preview[0] = '\0';
	if (cont) {
		readyResume(cont);
		if (resume.can_resume && resume.has_preview)
			snprintf(cont_preview, sizeof(cont_preview), "%s", resume.preview_path);
	}

	// games (ROMs, folder games, legacy folders) and tools, each in stored order; a missing tool and the Continue game
	// are skipped (both stay pinned)
	Directory* root = stack->items[0];
	ngames = ntools = 0;
	for (int i = 0; i < root->entries->count; i++) {
		Entry* e = root->entries->items[i];
		if (e->type == ENTRY_PAK) {
			if (ntools < HOME_MAX_PINS && exists(e->path))
				tools[ntools++] = e;
			continue;
		}
		if (ngames >= HOME_MAX_PINS || (cont && exactMatch(e->path, cont->path)))
			continue;
		game_plain_dir[ngames] = e->type == ENTRY_DIR && !canPinEntry(e);
		games[ngames++] = e;
	}
	if (listMode())
		listBuild(reset);
	relayout(stripLines());
	// the card cache is kept: each card's key carries a stamp of everything it shows (cardStamp), so a changed pin or
	// Continue recomposes on its own, and a card unchanged since the last visit is blitted without being composed again
	readyFocus();
}

static void ensureBuilt(void) {
	if (!screen || !Home_active())
		return;
	if (stats_due) {
		stats_due = false;
		if (CFG_getExtraInfo()) // hidden, the strip isn't drawn: nothing to count
			HomeStats_request();
	}
	// Home's own inputs only: a reset, the root list (the pins are borrowed from it) and the screen. Not the tab
	// generation: stepping tabs alone changes nothing Home shows.
	if (need_rebuild || built_root != ((Directory*)stack->items[0])->serial || built_w != screen->w ||
		built_h != screen->h || built_scale != homeScale() || built_style != CFG_getHomeStyle()) {
		rebuild();
		return;
	}
	// the stats' first result (or a month with no play) changes the strip's line count, and with it the top section
	int lines = stripLines();
	if (lines != built_lines)
		relayout(lines);
}

void Home_reset(void) {
	need_rebuild = true;
}

void Home_quit(void) {
	if (cont)
		Entry_free(cont);
	cont = NULL;
	ngames = ntools = 0;
	if (hl_entries)
		Array_free(hl_entries); // the list only: its entries are borrowed
	hl_entries = NULL;
	hl_dir.entries = NULL;
	for (int i = 0; i < 2; i++) {
		GFX_freeSurfaceAndTexture(tag_surf[i]);
		tag_surf[i] = NULL;
	}
	if (hint_bar)
		SDL_FreeSurface(hint_bar);
	hint_bar = NULL;
	hint_key[0] = '\0';
	if (strip_surf)
		SDL_FreeSurface(strip_surf);
	strip_surf = NULL;
	strip_key = 0;
	cardCacheClear();
	stretchFree();
	ringFree();
	need_rebuild = true;
}

///////////////////////////////////////
// Game info

// The time segment ("Today - 18m 37s") for a game; "" without one (and with Layouts > Extra info hidden).
static void timeText(const char* path, char* out, size_t size) {
	out[0] = '\0';
	GameInfo info;
	if (!path || !CFG_getExtraInfo() || !GameInfo_get(path, &info) || !info.has_time)
		return;
	InfoSeg segs[3];
	int n = GameInfo_segments(time(NULL), info.last_played, info.seconds, 0, 0, NULL, false, segs);
	if (n > 0)
		snprintf(out, size, "%s", segs[0].text);
}

///////////////////////////////////////
// Tiles (each composed at 0,0 into its cached surface)

static void tileBorder(SDL_Surface* s, int w, int h) {
	int t = px(HOME_EDGE_W) > 0 ? px(HOME_EDGE_W) : 1;
	strokeRounded(s, 0, 0, w, h, radiusPx(), t, C_WHITE, BORDER_ALPHA);
}

// The eased black gradient over the bottom 80%: 0.92 · (1 − t³)³, t = 0 at the bottom edge.
static void captionFade(SDL_Surface* s, int w, int h) {
	int fh = (int)(h * 0.8f + 0.5f);
	SDL_Surface* fade = UI_easedFadeSurface(w, fh, 0.92f, 3.0f, false);
	if (fade) // cached: blit right away
		UI_blitFade(fade, NULL, s, 0, h - fh);
}

// Lines of lh px centred as a block in (0, top, w, h), each line centred across.
static void drawCentredLines(SDL_Surface* s, TTF_Font* f, char lines[][LINE_MAX], int n, int lh, SDL_Color c, int w,
							 int top, int h) {
	if (!f || n <= 0)
		return;
	int y = top + (h - n * lh) / 2;
	for (int i = 0; i < n; i++, y += lh) {
		int lw = textW(f, lines[i]);
		drawText(s, f, lines[i], c, (w - lw) / 2, y + (lh - TTF_FontHeight(f)) / 2, 255);
	}
}

// Text in a line box of lh px whose top is y, centred in it.
static void drawInLine(SDL_Surface* s, TTF_Font* f, const char* text, SDL_Color c, int x, int y, int lh) {
	if (f)
		drawText(s, f, text, c, x, y + (lh - TTF_FontHeight(f)) / 2, 255);
}

// The "Continue" badge: a black pill at 62% top-left, the word in white (TEXT_HOME_CONT_BADGE), so the card reads apart
// from the pinned games. Its corner sits three quarters of the caption's inset in; padding 0.26 / 0.74 of the text's
// height (the mockup's 3 and 9 dp round 12 sp). k: the card's size over its full size (a Carousel neighbour's 0.62).
// Returns the pill's bottom (0: none drawn).
static int drawContinueBadge(SDL_Surface* s, int inset, float k) {
	int badge_px = (int)(TextPx_for(TEXT_HOME_CONT_BADGE, UIScale_deviceIndex(UI_DEVICE_NAME)) * k + 0.5f);
	TTF_Font* f = badge_px > 0 ? UIFont_getPx(badge_px, false) : NULL;
	if (!f)
		return 0;
	const char* word = "Continue";
	int th = TTF_FontHeight(f), pad_y = (int)(th * 0.26f + 0.5f), pad_x = (int)(th * 0.74f + 0.5f);
	int bw = textW(f, word) + 2 * pad_x, bh = th + 2 * pad_y, at = (int)(inset * 0.75f + 0.5f);
	fillRounded(s, at, at, bw, bh, bh / 2, C_BLACK, 158);
	drawText(s, f, word, C_WHITE, at + pad_x, at + pad_y, 255);
	return at + bh;
}

// Continue: the art full-bleed under the caption fade; the title (TEXT_HOME_CONT_TITLE, one line with "…") and the time
// (TEXT_HOME_CONT_INFO, grey), 29 in, baselines 74 and 31 above the bottom at the UI scale's sizes (44 and 31), further
// up as the time grows (HomeLayout_captionBaselines) (the art: the game's own, else its abstract picture). No picture
// at all: the name centred (up to 2 lines) over the time.
static void composeContinue(SDL_Surface* s, int w, int h) {
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	SDL_Surface* pic = NULL;
	HomeArtState st = HomeArt_continue(cont->path, cont_preview[0] ? cont_preview : NULL, w, h, 0, &pic);
	if (pic) {
		SDL_BlitSurface(pic, NULL, s, &(SDL_Rect){0, 0});
		captionFade(s, w, h);
	}
	int inset = px(CONT_INSET);
	UIDevice dev = UIScale_deviceIndex(UI_DEVICE_NAME);
	int sub_px = (int)TextPx_for(TEXT_HOME_CONT_INFO, dev), title_px = (int)TextPx_for(TEXT_HOME_CONT_TITLE, dev);
	float sub_up, title_up;
	HomeLayout_captionBaselines(px(CONT_SUB_UP), px(CONT_TITLE_UP), px(CONT_SUB_PX), sub_px, &sub_up, &title_up);
	char when[160];
	timeText(cont->path, when, sizeof(when));
	TTF_Font* f = UIFont_getPx(sub_px, false);
	if (f && when[0]) {
		char line[LINE_MAX];
		ellipsize(f, when, line, w - 2 * inset);
		drawTextBaseline(s, f, line, C_GREY, inset, h - (int)(sub_up + 0.5f), 255);
	}
	f = UIFont_getPx(title_px, false);
	if (!f)
		return;
	if (st == HOMEART_NONE) { // a title card
		char lines[2][LINE_MAX];
		int nl = wrapLines(f, View_displayName(cont), w - 2 * inset, 2, lines);
		int lh = (int)(TTF_FontHeight(f) * 1.1f + 0.5f);
		drawCentredLines(s, f, lines, nl, lh, C_WHITE, w, 0, h - (int)(title_up + 0.5f));
	} else {
		// the name alone (no time, Layouts > Extra info hidden) sits where the time would, as a pin's
		char name[LINE_MAX];
		ellipsize(f, View_displayName(cont), name, w - 2 * inset);
		drawTextBaseline(s, f, name, C_WHITE, inset, h - (int)((when[0] ? title_up : sub_up) + 0.5f), 255);
	}
	drawContinueBadge(s, inset, 1.0f);
	tileBorder(s, w, h);
}

// The accent as 0xRRGGBB (the theme's alpha dropped).
static Uint32 accentRgb(void) {
	SDL_Color a = UI_accent();
	return (Uint32)a.r << 16 | (Uint32)a.g << 8 | a.b;
}

// Pick a game's background: an abstract picture (placeholder_art.c) in the accent's hue, made once per size and accent
// and kept (the card itself is cached too, so this runs on a size or accent change only).
static SDL_Surface* pickArt(int w, int h) {
	static SDL_Surface* art = NULL;
	static Uint32 art_rgb = 0;
	Uint32 rgb = accentRgb();
	if (art && art->w == w && art->h == h && art_rgb == rgb)
		return art;
	if (art)
		SDL_FreeSurface(art);
	art = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!art)
		return NULL;
	SDL_LockSurface(art);
	PlaceholderArt_renderRgb(art->pixels, w, h, art->pitch / 4, "nx-pick-a-game", rgb);
	SDL_UnlockSurface(art);
	SDL_SetSurfaceBlendMode(art, SDL_BLENDMODE_NONE);
	art_rgb = rgb;
	return art;
}

// Pick a game (nothing played yet): the accent's abstract picture full-bleed, the mark over it at 10%, the title and
// its line in white and grey; it lights with Continue's ring (drawTile), so it has one look.
static void composePick(SDL_Surface* s, int w, int h) {
	SDL_Surface* art = pickArt(w, h);
	if (art)
		SDL_BlitSurface(art, NULL, s, &(SDL_Rect){0, 0, w, h});
	else
		SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	// the mark's ink (the PNG is cropped to it) 60% of the card tall, its width following
	SDL_Surface* mark = MenuArt_get("nx_mark.png", w, (int)(h * 0.6f));
	if (mark) {
		SDL_SetSurfaceAlphaMod(mark, 26); // white at 10%
		SDL_BlitSurface(mark, NULL, s, &(SDL_Rect){(w - mark->w) / 2, (h - mark->h) / 2});
		SDL_SetSurfaceAlphaMod(mark, 255);
	}
	// heights first, then each font fetched again right before it draws (a font is only good until the next
	// UIFont_get)
	TTF_Font* big = UIFont_get(homeSp(24), false);
	int big_h = big ? TTF_FontHeight(big) : 0;
	TTF_Font* small = UIFont_get(homeSp(14), false);
	int small_h = small ? TTF_FontHeight(small) : 0;
	if (big_h > 0 && small_h > 0) {
		const char* title = "Pick a game";
		const char* sub = "Nothing played yet";
		int gap = homeDp(4);
		int y = (h - (big_h + gap + small_h)) / 2;
		big = UIFont_get(homeSp(24), false);
		drawText(s, big, title, C_WHITE, (w - textW(big, title)) / 2, y, 255);
		y += big_h + gap;
		small = UIFont_get(homeSp(14), false);
		drawText(s, small, sub, C_GREY, (w - textW(small, sub)) / 2, y, 255);
	}
	tileBorder(s, w, h);
}

// A pinned game: its art; lit, the caption fade and the name (TEXT_HOME_PIN_NAME, up to 2 lines, white) over the time
// (TEXT_HOME_PIN_INFO, grey), placed as the Continue card's caption (its inset and baselines). No art: the name centred (lit: the time alone
// under it).
static void composeGame(SDL_Surface* s, int w, int h, bool lit, int g) {
	Entry* e = games[g];
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	SDL_Surface* pic = NULL;
	HomeArtState st = HomeArt_pin(e->path, w, h, 0, &pic);
	bool title_tile = st == HOMEART_NONE;
	if (pic)
		SDL_BlitSurface(pic, NULL, s, &(SDL_Rect){0, 0});
	char when[160] = "";
	if (lit) {
		captionFade(s, w, h);
		timeText(e->path, when, sizeof(when));
	}
	// the caption as the Continue card's: its inset from the sides, the time's and the name's baselines as far up
	// from the bottom (HomeLayout_captionBaselines at the pin's sizes)
	int side = px(CONT_INSET);
	UIDevice dev = UIScale_deviceIndex(UI_DEVICE_NAME);
	int name_px = (int)TextPx_for(TEXT_HOME_PIN_NAME, dev), info_px = (int)TextPx_for(TEXT_HOME_PIN_INFO, dev);
	int name_lh = (int)(px(PIN_NAME_LH) * (float)name_px / px(PIN_NAME_PX) + 0.5f);
	float sub_up, title_up;
	HomeLayout_captionBaselines(px(CONT_SUB_UP), px(CONT_TITLE_UP), px(CONT_SUB_PX), info_px, &sub_up, &title_up);
	int info_base = h - (int)(sub_up + 0.5f);
	int name_base = when[0] ? h - (int)(title_up + 0.5f) : info_base; // the name alone sits where the time would
	int info_top = info_base;
	if (lit && when[0]) {
		TTF_Font* f = UIFont_getPx(info_px, false);
		if (f) {
			char line[LINE_MAX];
			ellipsize(f, when, line, w - 2 * side);
			drawTextBaseline(s, f, line, C_GREY, side, info_base, 255);
			info_top = info_base - TTF_FontAscent(f);
		}
	}
	if (title_tile) {
		// the name above the caption, centred; 15 sp, smaller (to 11) until its longest word fits
		float sp = Tiles_fitWordsSp(View_displayName(e), homeSp(15), homeSp(11), false, w - 2 * side);
		TTF_Font* f = UIFont_get(sp, false);
		if (f) {
			int caption = lit && when[0] ? h - info_top : 0;
			float lh = NX_SP(sp) * 1.15f;
			int lines_fit = (int)floorf((h - caption) / lh);
			int max_lines = lines_fit < 3 ? lines_fit : 3;
			if (max_lines < 1)
				max_lines = 1;
			char lines[3][LINE_MAX];
			int nl = wrapLines(f, View_displayName(e), w - 2 * side, max_lines, lines);
			drawCentredLines(s, f, lines, nl, (int)(lh + 0.5f), C_WHITE, w, 0, h - caption);
		}
	} else if (lit) {
		// the name on up to 2 lines (the rest "…"), stacked up from where one line would sit
		TTF_Font* f = UIFont_getPx(name_px, false);
		char lines[2][LINE_MAX];
		int nl = f ? wrapLines(f, View_displayName(e), w - 2 * side, 2, lines) : 0;
		for (int i = 0; i < nl; i++)
			drawTextBaseline(s, f, lines[i], C_WHITE, side, name_base - (nl - 1 - i) * name_lh, 255);
	}
	tileBorder(s, w, h);
}

// The tool's bundled icon (the one mapping, in tiles.c): by its shown name, else its pak's file name, else the
// unknown-tool icon.
static const char* toolIcon(Entry* e) {
	const char* file = Tiles_toolIcon(View_displayName(e));
	if (!file) {
		const char* slash = strrchr(e->path, '/');
		file = Tiles_toolIcon(slash ? slash + 1 : e->path);
	}
	return file ? file : TILE_UNKNOWN_TOOL_ICON;
}

// A tool square's glyph (Home's units): Grid's squares', or the Carousel dock's.
static float toolGlyph(void) {
	return carousel() ? car.dock_glyph : layout.glyph;
}

// A tool's square: its icon centred as a white mask (no label); lit, the accent fills the plate and the icon turns to
// its ink (black), with no ring and no edge.
static void composeTool(SDL_Surface* s, int w, int h, bool lit, int t) {
	SDL_Color bg = cardBg(lit);
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, bg.r, bg.g, bg.b, 255));
	int g = px(toolGlyph());
	SDL_Surface* icon = g > 0 ? MenuArt_get(toolIcon(tools[t]), g, g) : NULL;
	if (icon) {
		if (lit)
			tintLit(icon);
		SDL_BlitSurface(icon, NULL, s, &(SDL_Rect){(w - icon->w) / 2, (h - icon->h) / 2});
		SDL_SetSurfaceColorMod(icon, 255, 255, 255);
	}
	if (!lit)
		tileBorder(s, w, h);
}

///////////////////////////////////////
// The Carousel's cards (Continue and the pinned games; Pick a game and the dock's squares are Grid's)

// The picture at the centre tile's size whatever size the card is (a neighbour's is stretched down from it), so each
// game is decoded once.
static HomeArtState carArt(bool is_cont, Entry* e, SDL_Surface** pic) {
	if (is_cont)
		return HomeArt_continue(cont->path, cont_preview[0] ? cont_preview : NULL, car_full_w, car_full_h, 0, pic);
	return HomeArt_pin(e->path, car_full_w, car_full_h, 0, pic);
}

// The game's info segments in order (time, "n of m", "Next: …"; the game-list Carousel caption's, rowview.c
// gameSegments); *row0: those on the first info line (all but Next). 0 without data yet (GameInfo is async: the
// card's stamp carries the text, so the card recomposes when it arrives), and with Layouts > Extra info hidden.
static int carInfo(const char* path, InfoSeg segs[3], int* row0) {
	GameInfo info;
	*row0 = 0;
	if (!path || !CFG_getExtraInfo() || !GameInfo_get(path, &info) || !(info.has_time || info.has_ra))
		return 0;
	int n = GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0, info.has_time ? info.seconds : -1,
							  info.has_ra ? info.unlocked : 0, info.has_ra ? info.total : 0,
							  info.has_ra ? info.next : NULL, true, segs);
	for (int i = 0; i < n; i++)
		if (segs[i].kind != INFO_SEG_NEXT)
			(*row0)++;
	return n;
}

// The selected card's caption, bottom-left as the Continue card's (its inset and baselines at the Continue card's sizes):
// the name on one line ("…"), then the time with the achievements (the trophy, InfoBand's segments), then Next on a
// line of its own while the name still clears `top` (the badge). No info yet: the name alone where the time would sit.
static void drawCarCaption(SDL_Surface* s, int w, int h, Entry* e, int top) {
	int side = px(CONT_INSET);
	UIDevice dev = UIScale_deviceIndex(UI_DEVICE_NAME);
	int name_px = (int)TextPx_for(TEXT_HOME_CONT_TITLE, dev), info_px = (int)TextPx_for(TEXT_HOME_CONT_INFO, dev);
	float sub_up, title_up;
	HomeLayout_captionBaselines(px(CONT_SUB_UP), px(CONT_TITLE_UP), px(CONT_SUB_PX), info_px, &sub_up, &title_up);
	InfoSeg segs[3];
	int n0 = 0;
	int n = carInfo(e->path, segs, &n0);
	// heights first (a font is only good until the next UIFont_get)
	TTF_Font* f = UIFont_getPx(info_px, false);
	int info_h = f ? TTF_FontHeight(f) : 0, info_asc = f ? TTF_FontAscent(f) : 0;
	f = UIFont_getPx(name_px, false);
	int name_asc = f ? TTF_FontAscent(f) : 0;
	int bottom = h - (int)(sub_up + 0.5f), name_gap = (int)(title_up - sub_up + 0.5f);
	bool next = n > n0 && n0 > 0 && bottom - info_h - name_gap - name_asc >= top;
	int row0_base = next ? bottom - info_h : bottom;
	int name_base = n0 > 0 || n > 0 ? row0_base - name_gap : bottom;
	f = UIFont_getPx(info_px, false);
	if (f && n0 > 0)
		InfoBand_drawSegments(s, segs, n0, side, false, row0_base - info_asc, w - 2 * side, f);
	else if (f && n > 0) // Next alone (no time): on the first info line
		InfoBand_drawSegments(s, segs, n, side, false, row0_base - info_asc, w - 2 * side, f);
	if (f && next)
		InfoBand_drawSegments(s, segs + n0, n - n0, side, false, bottom - info_asc, w - 2 * side, f);
	f = UIFont_getPx(name_px, false);
	if (f) {
		char name[LINE_MAX];
		ellipsize(f, View_displayName(e), name, w - 2 * side);
		drawTextBaseline(s, f, name, C_WHITE, side, name_base, 255);
	}
}

// Continue or a pinned game in the row, at the centre's size or a neighbour's (k of it): the picture full-bleed; lit
// (the selection), the caption fade and the caption over it; plain, nothing over the picture (no picture: the name
// centred). Continue wears its badge in both looks.
static void composeCar(SDL_Surface* s, int w, int h, bool lit, bool is_cont, int g) {
	Entry* e = is_cont ? cont : games[g];
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	SDL_Surface* pic = NULL;
	HomeArtState st = carArt(is_cont, e, &pic);
	if (pic) {
		if (pic->w == w && pic->h == h)
			SDL_BlitSurface(pic, NULL, s, &(SDL_Rect){0, 0});
		else
			SDL_BlitScaled(pic, NULL, s, &(SDL_Rect){0, 0, w, h});
	}
	float k = car_full_w > 0 ? (float)w / car_full_w : 1.0f;
	int inset = (int)(px(CONT_INSET) * k + 0.5f);
	if (lit)
		captionFade(s, w, h);
	int badge_bottom = is_cont ? drawContinueBadge(s, inset, k) : 0;
	if (lit) {
		drawCarCaption(s, w, h, e, badge_bottom > 0 ? badge_bottom + inset / 2 : inset);
	} else if (st == HOMEART_NONE) { // a title card: the name centred, up to 3 lines, at the card's scale
		int name_px = (int)(TextPx_for(TEXT_HOME_CONT_TITLE, UIScale_deviceIndex(UI_DEVICE_NAME)) * k + 0.5f);
		TTF_Font* f = name_px > 0 ? UIFont_getPx(name_px, false) : NULL;
		if (f) {
			char lines[3][LINE_MAX];
			int nl = wrapLines(f, View_displayName(e), w - 2 * inset, 3, lines);
			drawCentredLines(s, f, lines, nl, (int)(TTF_FontHeight(f) * 1.1f + 0.5f), C_WHITE, w, badge_bottom,
							 h - badge_bottom);
		}
	}
	tileBorder(s, w, h);
}

///////////////////////////////////////
// The stats strip

static SDL_Color runColour(StripTone t) {
	return t == STRIP_WHITE ? C_WHITE : t == STRIP_DOT ? C_DOT
													   : C_GREY;
}

// One line's runs from x on baseline y; the run that gives way is cut with "…" so the line ends by right. centre: the
// line centred between x and right instead (the Carousel's).
static void drawStripLine(SDL_Surface* s, TTF_Font* f, StripLine* l, int x, int right, int baseline, bool centre) {
	float em = (float)statsPx();
	int fixed = 0, give = -1;
	for (int i = 0; i < l->n; i++) {
		StripRun* r = &l->runs[i];
		fixed += (int)(em * (r->pad_l + r->pad_r) + 0.5f);
		if (r->gives_way)
			give = i;
		else
			fixed += textW(f, r->text);
	}
	if (give >= 0) {
		char cut[LINE_MAX];
		ellipsize(f, l->runs[give].text, cut, right - x - fixed);
		snprintf(l->runs[give].text, sizeof(l->runs[give].text), "%s", cut);
	}
	if (centre) {
		int total = fixed + (give >= 0 ? textW(f, l->runs[give].text) : 0);
		x += (right - x - total) / 2;
	}
	for (int i = 0; i < l->n; i++) {
		StripRun* r = &l->runs[i];
		x += (int)(em * r->pad_l + 0.5f);
		drawTextBaseline(s, f, r->text, runColour(r->tone), x, baseline, 255);
		x += textW(f, r->text) + (int)(em * r->pad_r + 0.5f);
	}
}

// The strip under the tab row (not selectable), rendered whole into its own surface when its text changes. The
// Carousel's: its own baselines (lower), each line centred. The List's: its own baselines, left-aligned at the rows'
// text start (the tab row's gutter).
static void drawStrip(SDL_Surface* dst, const HomeStats* st, int scroll_px) {
	int mode = listMode() ? 2 : carousel() ? 1
										   : 0;
	bool centre = mode == 1;
	int lines, base[2], x, right;
	if (mode == 2) {
		lines = hl_geom.strip_lines;
		base[0] = hl_geom.strip_base[0];
		base[1] = hl_geom.strip_base[1];
		x = NX_DP(NX_MENU_GUTTER_DP);
		right = dst->w - x;
	} else {
		lines = centre ? car.strip_lines : layout.strip_lines;
		const float* b = centre ? car.strip_base : layout.strip_base;
		base[0] = px(b[0]);
		base[1] = px(b[1]);
		x = px(centre ? car.strip_left : layout.strip_x);
		right = px(centre ? car.strip_right : layout.strip_right);
	}
	if (lines <= 0)
		return;
	StripInput in = stripInput(st);
	StripLine l1, l2;
	HomeStrip_build(&in, &l1, &l2);
	TTF_Font* f = UIFont_getPx(statsPx(), false);
	if (!f)
		return;
	Uint32 key = 2166136261u;
	key = View_fnv(key, &dst->w, sizeof(dst->w));
	key = View_fnv(key, &lines, sizeof(lines));
	key = View_fnv(key, &mode, sizeof(mode));
	float scale = homeScale();
	key = View_fnv(key, &scale, sizeof(scale));
	for (int i = 0; i < l1.n; i++)
		key = View_fnvStr(key, l1.runs[i].text);
	for (int i = 0; i < l2.n; i++)
		key = View_fnvStr(key, l2.runs[i].text);
	int top = base[0] - TTF_FontAscent(f);
	int last = lines == 2 ? 1 : 0;
	int h = base[last] - TTF_FontDescent(f) - top + 1;
	if (!strip_surf || strip_key != key || strip_surf->w != dst->w || strip_surf->h != h) {
		if (strip_surf && (strip_surf->w != dst->w || strip_surf->h != h)) {
			SDL_FreeSurface(strip_surf);
			strip_surf = NULL;
		}
		if (!strip_surf)
			strip_surf = SDL_CreateRGBSurfaceWithFormat(0, dst->w, h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!strip_surf)
			return;
		SDL_FillRect(strip_surf, NULL, 0);
		drawStripLine(strip_surf, f, &l1, x, right, base[0] - top, centre);
		if (l2.n > 0)
			drawStripLine(strip_surf, f, &l2, x, right, base[1] - top, centre);
		SDL_SetSurfaceBlendMode(strip_surf, SDL_BLENDMODE_BLEND);
		strip_key = key;
		strip_top = top;
	}
	SDL_BlitSurface(strip_surf, NULL, dst, &(SDL_Rect){0, strip_top - scroll_px});
}

///////////////////////////////////////
// Render

typedef enum { CARD_CONTINUE,
			   CARD_PICK,
			   CARD_GAME,
			   CARD_TOOL,
			   CARD_CAR_CONTINUE, // the Carousel's Continue and pinned games (composeCar)
			   CARD_CAR_GAME } CardKind;

// A tile's identity across frames (the selection crossfade): a top tile by its index, a row pin past them.
static int tileId(HomeFocus f) {
	f = HomeLayout_clampFocus(&layout, f);
	return f.sec == HOME_SEC_PINS ? HOME_MAX_TOP + f.pin : f.top;
}

// The Carousel's: the row as one (its items light with the row's position instead), a dock square by its index.
#define CAR_ID_ROW 100
#define CAR_ID_DOCK 101
static int carId(HomeCarFocus f) {
	return f.dock ? CAR_ID_DOCK + f.tool : CAR_ID_ROW;
}

// How lit a tile is (0..1): the focused tile fades in, the previous one out. (While the tab row has focus the whole
// page dims as one layer: contentdim.c.)
static float litAmount(int id) {
	float p = tweenProgress(&sel_tw, SEL_MS);
	if (id == (carousel() ? carId(car_focus) : tileId(focus)))
		return p;
	if (sel_tw.active && id == prev_id)
		return 1.0f - p;
	return 0.0f;
}

///////////////////////////////////////
// Card cache

#define CARD_CACHE_MAX 48

typedef struct {
	bool used;
	CardKind kind;
	int ref, w, h;
	float scale;
	bool lit;
	Uint32 stamp; // what the card shows (see cardStamp)
	unsigned lru;
	SDL_Surface* surf;
} CardSlot;

static CardSlot card_cache[CARD_CACHE_MAX];
static unsigned card_lru = 0;

static void cardCacheClear(void) {
	for (int i = 0; i < CARD_CACHE_MAX; i++) {
		GFX_freeSurfaceAndTexture(card_cache[i].surf); // a Carousel card may have its sprite texture
		card_cache[i] = (CardSlot){0};
	}
}

// Every visible tile in both looks. The Carousel: the row's items within reach (Row_visibleRange: up to 4 each side) in
// three (the centre's two, a neighbour's), the dock's squares in two.
static int cardCacheLimit(void) {
	int reach = car.nitems < 9 ? car.nitems : 9;
	int n = carousel() ? 3 * reach + 2 * car.ntools + 2 : 2 * (layout.ntop + layout.npins) + 2;
	return n < CARD_CACHE_MAX ? n : CARD_CACHE_MAX;
}

// Home's stamps hash a NULL string as "" (View_fnvStr's Grid rule hashes it apart): kept, so the card stamps don't
// change.
static Uint32 fnvStr(Uint32 h, const char* s) {
	return View_fnvStr(h, s ? s : "");
}


// gen = HomeArt_lastGen() of pic's lookup: a re-decoded art (after HomeArt_forget) may reuse the old pointer
static Uint32 artStamp(Uint32 h, HomeArtState st, SDL_Surface* pic, unsigned gen) {
	h = View_fnv(h, &st, sizeof(st));
	h = View_fnv(h, &gen, sizeof(gen));
	return View_fnv(h, &pic, sizeof(pic));
}
// A stamp of the data a card's composition reads (the same lookups compose does, all cached and cheap). The cache
// outlives rebuilds (and so Home visits), so this must cover every input compose reads beyond the slot's key (kind, ref,
// w, h, homeScale(), lit).
static Uint32 cardStamp(CardKind kind, int ref, int w, int h, bool lit) {
	Uint32 hs = 2166136261u;
	SDL_Surface* pic = NULL;
	char when[160];
	if (lit) { // the lit look's ground and ink (the theme's accent); plain cards use fixed colours
		SDL_Color bg = cardBg(true), ink = cardInk(true);
		hs = View_fnv(hs, &bg, sizeof(bg));
		hs = View_fnv(hs, &ink, sizeof(ink));
	}
	switch (kind) {
	case CARD_CONTINUE: {
		HomeArtState as = HomeArt_continue(cont->path, cont_preview[0] ? cont_preview : NULL, w, h, 0, &pic);
		hs = artStamp(hs, as, pic, HomeArt_lastGen()); // right after the lookup it describes
		hs = fnvStr(hs, cont->path);
		hs = fnvStr(hs, cont_preview);
		hs = fnvStr(hs, View_displayName(cont));
		timeText(cont->path, when, sizeof(when));
		hs = fnvStr(hs, when);
		break;
	}
	case CARD_PICK: {
		Uint32 rgb = accentRgb(); // its picture is in the accent
		hs = View_fnv(hs, &rgb, sizeof(rgb));
		break;
	}
	case CARD_GAME: {
		Entry* e = games[ref];
		HomeArtState as = HomeArt_pin(e->path, w, h, 0, &pic);
		hs = artStamp(hs, as, pic, HomeArt_lastGen()); // right after the lookup it describes
		hs = fnvStr(hs, e->path);
		hs = fnvStr(hs, View_displayName(e));
		if (lit) {
			timeText(e->path, when, sizeof(when));
			hs = fnvStr(hs, when);
		}
		break;
	}
	case CARD_CAR_CONTINUE:
	case CARD_CAR_GAME: {
		bool is_cont = kind == CARD_CAR_CONTINUE;
		Entry* e = is_cont ? cont : games[ref];
		HomeArtState as = carArt(is_cont, e, &pic);
		hs = artStamp(hs, as, pic, HomeArt_lastGen());		// right after the lookup it describes
		hs = View_fnv(hs, &car_full_w, sizeof(car_full_w)); // the picture's size (a neighbour's is stretched from it)
		hs = View_fnv(hs, &car_full_h, sizeof(car_full_h));
		hs = fnvStr(hs, e->path);
		if (is_cont)
			hs = fnvStr(hs, cont_preview);
		hs = fnvStr(hs, View_displayName(e));
		if (lit) {
			InfoSeg segs[3];
			int n0;
			int n = carInfo(e->path, segs, &n0);
			for (int i = 0; i < n; i++) {
				hs = View_fnv(hs, &segs[i].kind, sizeof(segs[i].kind));
				hs = fnvStr(hs, segs[i].text);
			}
		}
		break;
	}
	case CARD_TOOL:
		hs = fnvStr(hs, tools[ref]->path);
		hs = fnvStr(hs, View_displayName(tools[ref]));
		float glyph = toolGlyph();
		hs = View_fnv(hs, &glyph, sizeof(glyph));
		break;
	}
	return hs;
}

static void composeCardKind(SDL_Surface* s, CardKind kind, int w, int h, bool lit, int ref) {
	PLAT_freeSurfaceTexture(s); // a Carousel card's sprite texture holds the old look: made again on its next use
	SDL_SetClipRect(s, &(SDL_Rect){0, 0, w, h});
	switch (kind) {
	case CARD_CONTINUE:
		composeContinue(s, w, h);
		break;
	case CARD_PICK:
		composePick(s, w, h);
		break;
	case CARD_GAME:
		composeGame(s, w, h, lit, ref);
		break;
	case CARD_TOOL:
		composeTool(s, w, h, lit, ref);
		break;
	case CARD_CAR_CONTINUE:
	case CARD_CAR_GAME:
		composeCar(s, w, h, lit, kind == CARD_CAR_CONTINUE, ref);
		break;
	}
	maskCorners(s, w, h, radiusPx());
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
}

// The card's cached surface for this look, recomposed only when its key changed.
static SDL_Surface* cachedCard(CardKind kind, int ref, int w, int h, bool lit) {
	if (kind == CARD_CONTINUE || kind == CARD_PICK)
		lit = false; // Continue and Pick a game light with their ring only
	if (kind == CARD_PICK)
		ref = -1;
	if (kind == CARD_CAR_CONTINUE)
		ref = 0;
	Uint32 stamp = cardStamp(kind, ref, w, h, lit);
	CardSlot* victim = NULL;
	int used = 0;
	for (int i = 0; i < CARD_CACHE_MAX; i++) {
		CardSlot* c = &card_cache[i];
		if (!c->used) {
			if (!victim || victim->used)
				victim = c;
			continue;
		}
		used++;
		if (c->kind == kind && c->ref == ref && c->w == w && c->h == h && c->scale == homeScale() && c->lit == lit) {
			if (c->stamp != stamp) { // same card, new content: recompose in place
				c->stamp = stamp;
				composeCardKind(c->surf, kind, w, h, lit, ref);
			}
			c->lru = ++card_lru;
			return c->surf;
		}
		if (!victim || (victim->used && c->lru < victim->lru))
			victim = c;
	}
	// a free slot while under the limit, else the least recently used card
	if (victim && !victim->used && used >= cardCacheLimit()) {
		victim = NULL;
		for (int i = 0; i < CARD_CACHE_MAX; i++)
			if (card_cache[i].used && (!victim || card_cache[i].lru < victim->lru))
				victim = &card_cache[i];
	}
	if (!victim)
		return NULL;
	if (victim->surf && (victim->surf->w != w || victim->surf->h != h)) {
		GFX_freeSurfaceAndTexture(victim->surf);
		victim->surf = NULL;
	}
	if (!victim->surf)
		victim->surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!victim->surf) {
		victim->used = false;
		return NULL;
	}
	SDL_Surface* surf = victim->surf;
	*victim = (CardSlot){.used = true, .kind = kind, .ref = ref, .w = w, .h = h, .scale = homeScale(), .lit = lit, .stamp = stamp, .lru = ++card_lru, .surf = surf};
	composeCardKind(surf, kind, w, h, lit, ref);
	return surf;
}

// A cached card at (r.x, r.y): the four corner squares (the only pixels with alpha, see maskCorners) through SDL's
// blend, the opaque rest through UI_blitOpaque. under (optional, see UI_blitOpaque): the card's other look, drawn in the
// same pass with surf at alpha over it. dim: black at that alpha over the card (the Carousel's neighbours), the corners
// through a colour mod so the clear around them stays clear.
static void blitCardOver(SDL_Surface* dst, SDL_Surface* surf, SDL_Surface* under, SDL_Rect r, int alpha, Uint8 dim) {
	if (!surf || alpha <= 0)
		return;
	if (under && (under->w != surf->w || under->h != surf->h))
		under = NULL;
	if (alpha > 255)
		alpha = 255;
	int w = surf->w, h = surf->h;
	int rad = clampRadius(radiusPx(), w, h);
	Uint8 cm = (Uint8)(255 - dim);
	if (rad > 0) {
		if (under) {
			SDL_SetSurfaceColorMod(under, cm, cm, cm);
			for (int j = 0; j < 2; j++)
				for (int i = 0; i < 2; i++) {
					int x = i ? w - rad : 0, y = j ? h - rad : 0;
					SDL_BlitSurface(under, &(SDL_Rect){x, y, rad, rad}, dst, &(SDL_Rect){r.x + x, r.y + y});
				}
			SDL_SetSurfaceColorMod(under, 255, 255, 255);
		}
		SDL_SetSurfaceAlphaMod(surf, (Uint8)alpha);
		SDL_SetSurfaceColorMod(surf, cm, cm, cm);
		int xs[2] = {0, w - rad}, ys[2] = {0, h - rad};
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++)
				SDL_BlitSurface(surf, &(SDL_Rect){xs[i], ys[j], rad, rad}, dst, &(SDL_Rect){r.x + xs[i], r.y + ys[j]});
		SDL_SetSurfaceAlphaMod(surf, 255);
		SDL_SetSurfaceColorMod(surf, 255, 255, 255);
	}
	SDL_Rect parts[3] = {{rad, 0, w - 2 * rad, rad},		// top, between the corners
						 {0, rad, w, h - 2 * rad},			// the full-width middle
						 {rad, h - rad, w - 2 * rad, rad}}; // bottom
	for (int i = 0; i < 3; i++) {
		SDL_Rect p = parts[i];
		UI_blitOpaque(surf, under, p.x, p.y, p.w, p.h, dst, r.x + p.x, r.y + p.y, alpha);
		if (dim)
			UI_dimRect(dst, &(SDL_Rect){r.x + p.x, r.y + p.y, p.w, p.h}, dim);
	}
}

static void blitCard(SDL_Surface* dst, SDL_Surface* surf, SDL_Rect r, int alpha) {
	blitCardOver(dst, surf, NULL, r, alpha, 0);
}
static CardKind cardKind(const HomeTile* t) {
	switch (t->kind) {
	case HOME_TILE_CONTINUE:
		return cont ? CARD_CONTINUE : CARD_PICK;
	case HOME_TILE_GAME:
		return CARD_GAME;
	case HOME_TILE_TOOL:
		return CARD_TOOL;
	}
	return CARD_PICK;
}

// dst's clip rect is the page band: a tile wholly outside it costs nothing. A lit game (Continue, a pin) and Pick a
// game wear the 6 px ring outside it; a lit tool square its filled look instead.
static void drawTile(SDL_Surface* dst, const HomeTile* t, int id, int scroll_px) {
	SDL_Rect r = toScreen(t->r, scroll_px);
	int ring = px(HOME_RING);
	const SDL_Rect* band = &dst->clip_rect;
	if (r.w <= 0 || r.h <= 0 || r.y + r.h + ring <= band->y || r.y - ring >= band->y + band->h)
		return; // outside the band
	CardKind kind = cardKind(t);
	float lit = litAmount(id);
	bool ringed = kind == CARD_CONTINUE || kind == CARD_PICK || kind == CARD_GAME;
	bool lit_differs = kind != CARD_CONTINUE && kind != CARD_PICK; // Continue and Pick a game light with their ring only
	if (ringed && lit > 0.0f)
		strokeRounded(dst, r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring, radiusPx() + ring, ring,
					  cardBg(true), (int)(lit * 255 + 0.5f));
	if (lit > 0.0f && lit < 1.0f && lit_differs) { // a crossfade: both looks in one pass
		blitCardOver(dst, cachedCard(kind, t->ref, r.w, r.h, true), cachedCard(kind, t->ref, r.w, r.h, false), r,
					 (int)(lit * 255 + 0.5f), 0);
		return;
	}
	blitCard(dst, cachedCard(kind, t->ref, r.w, r.h, lit >= 1.0f && lit_differs), r, 255);
}

///////////////////////////////////////
// The Carousel row and dock

// Two scratch surfaces over buffers kept between frames: a card stretched (nearest, its alpha copied) to a size between
// the centre's and a neighbour's while it slides (rowview.c's approach: nothing is composed at a tweened size).
static Uint32* stretch_buf[2] = {NULL, NULL};
static size_t stretch_cap[2] = {0, 0};
static SDL_Surface* stretch_view[2] = {NULL, NULL};

static void stretchFree(void) {
	for (int i = 0; i < 2; i++) {
		if (stretch_view[i])
			SDL_FreeSurface(stretch_view[i]);
		free(stretch_buf[i]);
		stretch_view[i] = NULL;
		stretch_buf[i] = NULL;
		stretch_cap[i] = 0;
	}
}

static SDL_Surface* stretched(int which, SDL_Surface* src, int w, int h) {
	if (!src || w <= 0 || h <= 0)
		return NULL;
	size_t need = (size_t)w * h;
	if (need > stretch_cap[which]) {
		Uint32* b = realloc(stretch_buf[which], need * 4);
		if (!b)
			return NULL;
		stretch_buf[which] = b;
		stretch_cap[which] = need;
	}
	if (stretch_view[which])
		SDL_FreeSurface(stretch_view[which]);
	stretch_view[which] =
		SDL_CreateRGBSurfaceWithFormatFrom(stretch_buf[which], w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface* v = stretch_view[which];
	if (!v)
		return NULL;
	SDL_BlendMode bm;
	SDL_GetSurfaceBlendMode(src, &bm);
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE); // a plain copy: SDL's fast stretch
	SDL_BlitScaled(src, NULL, v, NULL);
	SDL_SetSurfaceBlendMode(src, bm);
	SDL_SetSurfaceBlendMode(v, SDL_BLENDMODE_BLEND);
	return v;
}

// GPU sprite mode (Home_render: the Carousel, not under a context menu): the row's cards and their rings go to the GPU
// as sprites instead of being stretched and blended into the screen (10-15 ms a frame on the Brick while the row
// slid), as the game lists' Carousel (rowview.c). The body under them stays clear; the strip and the dock stay
// software (they don't move with the row).
static bool car_sprites = false;
// a look of a card within reach still had no texture after this frame's one (carWarm): keep redrawing (Home_animating)
static bool car_warming = false;

// A sprite's alpha: a at the tab-focus dim (the page dims as one layer: contentdim.c for the software part)
static Uint8 carSpriteAlpha(int a) {
	if (a <= 0)
		return 0;
	return (Uint8)((a > 255 ? 255 : a) * MenuTabs_contentAlpha() + 0.5f);
}

// card's texture into r at alpha a, darkened by dim (a colour mod: its clear corners stay clear)
static void cardSprite(SDL_Surface* dst, SDL_Surface* card, SDL_Rect r, int a, Uint8 dim) {
	Uint8 sa = carSpriteAlpha(a);
	SDL_Texture* t = sa && card ? PLAT_textureForSurface(card) : NULL;
	if (t)
		PLAT_spriteAddShaded(t, NULL, &r, sa, (Uint8)(255 - dim), &dst->clip_rect);
}

// The selection ring around the centre card at full strength, as strokeRounded draws it on the screen: one colour, its
// coverage in the alpha (straight, as a sprite's). Made again when the card's size, the ring or the accent changes.
static SDL_Surface* ring_surf = NULL;
static int ring_t = 0, ring_r = 0;
static Uint32 ring_rgb = 0;

static void ringFree(void) {
	GFX_freeSurfaceAndTexture(ring_surf);
	ring_surf = NULL;
}

static SDL_Surface* ringSurface(void) {
	int t = px(HOME_RING);
	int sw = car_full_w + 2 * t, sh = car_full_h + 2 * t;
	SDL_Color c = cardBg(true);
	Uint32 rgb = ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b;
	int r = clampRadius(radiusPx() + t, sw, sh);
	if (ring_surf && ring_surf->w == sw && ring_surf->h == sh && ring_t == t && ring_r == r && ring_rgb == rgb)
		return ring_surf;
	ringFree();
	if (t <= 0 || sw <= 0 || sh <= 0)
		return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, 0);
	strokeRounded(s, 0, 0, sw, sh, radiusPx() + t, t, c, 255);
	// a partly covered pixel blended over clear is left premultiplied (blendPx): the ring is one colour, so every
	// covered pixel takes it back at its coverage
	for (int y = 0; y < sh; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
		for (int x = 0; x < sw; x++)
			if (row[x] >> 24)
				row[x] = (row[x] & 0xFF000000u) | rgb;
	}
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	ring_surf = s;
	ring_t = t, ring_r = r, ring_rgb = rgb;
	return s;
}

// The ring in the outer rect o at alpha a: its corners 1:1 and its sides stretched along their length (nine pieces less
// the empty middle), so its thickness and corners stay as strokeRounded's at any card size.
static void ringSprites(SDL_Surface* dst, SDL_Rect o, int a) {
	SDL_Surface* s = ringSurface();
	Uint8 sa = carSpriteAlpha(a);
	SDL_Texture* tex = sa && s ? PLAT_textureForSurface(s) : NULL;
	if (!tex)
		return;
	const SDL_Rect* clip = &dst->clip_rect;
	int t = ring_t, R = ring_r, sw = s->w, sh = s->h;
	if (clampRadius(radiusPx() + t, o.w, o.h) != R || o.w < 2 * R || o.h < 2 * R) {
		PLAT_spriteAdd(tex, NULL, &o, sa, clip); // not reached at the Carousel's sizes
		return;
	}
	SDL_Rect src[8] = {{0, 0, R, R},
					   {sw - R, 0, R, R},
					   {0, sh - R, R, R},
					   {sw - R, sh - R, R, R},
					   {R, 0, sw - 2 * R, t},
					   {R, sh - t, sw - 2 * R, t},
					   {0, R, t, sh - 2 * R},
					   {sw - t, R, t, sh - 2 * R}};
	SDL_Rect d[8] = {{o.x, o.y, R, R},
					 {o.x + o.w - R, o.y, R, R},
					 {o.x, o.y + o.h - R, R, R},
					 {o.x + o.w - R, o.y + o.h - R, R, R},
					 {o.x + R, o.y, o.w - 2 * R, t},
					 {o.x + R, o.y + o.h - t, o.w - 2 * R, t},
					 {o.x, o.y + R, t, o.h - 2 * R},
					 {o.x + o.w - t, o.y + R, t, o.h - 2 * R}};
	for (int i = 0; i < 8; i++)
		if (src[i].w > 0 && src[i].h > 0 && d[i].w > 0 && d[i].h > 0)
			PLAT_spriteAdd(tex, &src[i], &d[i], sa, clip);
}

// Row item i with the row at pos: 1:1 from its cached card at a rest size (the centre's, lit; a neighbour's, plain),
// else both looks stretched from the centre's with the lit one crossfading in (1 − d, as the game lists' Carousel); the
// darkening (Row_item's) over it. The ring (the selection's, at the lit amount, gone while the dock has the focus:
// row_lit) outside it.
static void drawCarItem(SDL_Surface* dst, int i, float pos, float row_lit) {
	HomeCarItem it = HomeCar_item(&car, i, pos);
	if (!it.visible || it.darken >= 1.0f)
		return;
	float d = fabsf(i - pos), lit = 1.0f - (d < 1.0f ? d : 1.0f);
	CardKind kind = i > 0 ? CARD_CAR_GAME : cont ? CARD_CAR_CONTINUE
												 : CARD_PICK;
	int ref = i > 0 ? i - 1 : 0;
	bool lit_differs = kind != CARD_PICK; // Pick a game lights with its ring only
	bool full = fabsf(it.scale - 1.0f) < 0.004f, side = fabsf(it.scale - car.side_scale) < 0.004f;
	int w = full ? car_full_w : side ? car_side_w
									 : (int)(car_full_w * it.scale + 0.5f);
	int h = full ? car_full_h : side ? car_side_h
									 : (int)(car_full_h * it.scale + 0.5f);
	// the row's centre from its top in whole px: the centre card's top lands on px(row_y), as Grid's tiles on theirs
	int cx = px(it.r.x + it.r.w / 2), cy = px(car.row_y) + car_full_h / 2;
	SDL_Rect r = {cx - w / 2, cy - h / 2, w, h};
	int ring = px(HOME_RING);
	if (r.x + r.w + ring <= 0 || r.x - ring >= dst->w)
		return;
	Uint8 dim = (Uint8)(it.darken * 255.0f + 0.5f);
	float ring_a = lit * row_lit * (1.0f - it.darken);
	if (car_sprites) { // the same looks as sprites, the GPU scaling the centre's while the item changes size
		if (ring_a > 0.0f)
			ringSprites(dst, (SDL_Rect){r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring},
						(int)(ring_a * 255 + 0.5f));
		if (full || side) {
			cardSprite(dst, cachedCard(kind, ref, w, h, full && lit_differs), r, 255, dim);
			return;
		}
		SDL_Surface* lit_s = lit_differs && lit > 0.004f ? cachedCard(kind, ref, car_full_w, car_full_h, true) : NULL;
		SDL_Surface* plain = !lit_s || lit < 0.996f ? cachedCard(kind, ref, car_full_w, car_full_h, false) : NULL;
		cardSprite(dst, plain, r, 255, dim);
		cardSprite(dst, lit_s, r, plain ? (int)(lit * 255 + 0.5f) : 255, dim);
		return;
	}
	if (ring_a > 0.0f)
		strokeRounded(dst, r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring, radiusPx() + ring, ring,
					  cardBg(true), (int)(ring_a * 255 + 0.5f));
	if (full || side) {
		blitCardOver(dst, cachedCard(kind, ref, w, h, full && lit_differs), NULL, r, 255, dim);
		return;
	}
	SDL_Surface* lit_s = lit_differs && lit > 0.004f ? stretched(0, cachedCard(kind, ref, car_full_w, car_full_h, true), w, h) : NULL;
	SDL_Surface* plain = !lit_s || lit < 0.996f ? stretched(1, cachedCard(kind, ref, car_full_w, car_full_h, false), w, h)
												: NULL;
	if (lit_s && plain)
		blitCardOver(dst, lit_s, plain, r, (int)(lit * 255 + 0.5f), dim);
	else
		blitCardOver(dst, lit_s ? lit_s : plain, NULL, r, 255, dim);
}

// Sprite mode, the row at rest: the textures of the looks a slide shows next (each item within reach at a neighbour's
// size, the centre's size plain and lit, the ring), one a frame, so a slide finds them made instead of making them
// (~3 ms each on the Brick) in its frames.
static void carWarm(int first, int last) {
	SDL_Surface* ring = ringSurface();
	if (ring && !ring->userdata) {
		PLAT_textureForSurface(ring);
		car_warming = true;
		return;
	}
	for (int i = first; i <= last; i++) {
		CardKind kind = i > 0 ? CARD_CAR_GAME : cont ? CARD_CAR_CONTINUE
													 : CARD_PICK;
		int ref = i > 0 ? i - 1 : 0;
		SDL_Surface* looks[3] = {cachedCard(kind, ref, car_side_w, car_side_h, false),
								 cachedCard(kind, ref, car_full_w, car_full_h, false),
								 kind != CARD_PICK ? cachedCard(kind, ref, car_full_w, car_full_h, true) : NULL};
		for (int k = 0; k < 3; k++)
			if (looks[k] && !looks[k]->userdata) {
				PLAT_textureForSurface(looks[k]);
				car_warming = true; // maybe more: the next frame looks again
				return;
			}
	}
}

// The row, farthest items first (a sliding item's ring is never under a neighbour), then the dock's squares (Grid's
// squares and their selection crossfade).
static void drawCarousel(SDL_Surface* dst) {
	float pos = currentPos();
	int first, last;
	Row_visibleRange(car.nitems, pos, &first, &last);
	int order[9], n = 0;
	for (int i = first; i <= last && n < 9; i++)
		order[n++] = i;
	for (int a = 1; a < n; a++) // insertion sort by distance, farthest first
		for (int b = a; b > 0 && fabsf(order[b] - pos) > fabsf(order[b - 1] - pos); b--) {
			int t = order[b];
			order[b] = order[b - 1];
			order[b - 1] = t;
		}
	float row_lit = litAmount(CAR_ID_ROW);
	for (int k = 0; k < n; k++)
		drawCarItem(dst, order[k], pos, row_lit);
	if (car_sprites && !slide_tw.active && !sel_tw.active)
		carWarm(first, last);
	for (int t = 0; t < car.ntools; t++) {
		HomeTile tile = {HOME_TILE_TOOL, t, car.dock[t]};
		drawTile(dst, &tile, CAR_ID_DOCK + t, 0);
	}
}

static void renderHints(SDL_Surface* dst) {
	// Button hints hidden (Layouts > Button hints): no bar at all, a volume or brightness change included
	if (!CFG_getButtonHints())
		return;
	char* pairs[10] = {NULL};
	int p = 0;
	if (CFG_getShowRecentHint()) { // Appearance > Show recent hint (SELECT still opens the Game Switcher)
		pairs[p++] = "SELECT";
		pairs[p++] = "RECENT";
	}
	const HomeTile* t = focusedTile();
	Entry* e = tileEntry(t);
	static char tool_name[64]; // the A label on a tool square: its name in capitals
	if (MenuTabs_focused()) {  // the tab row has focus: exactly SELECT RECENT and A OPEN (A returns to Home)
		pairs[p++] = "A";
		pairs[p++] = "OPEN";
	} else if (!e) { // Pick a game: A opens the Consoles tab, when there is one
		if (MenuTabs_isVisible(MENU_TAB_CONSOLES)) {
			pairs[p++] = "A";
			pairs[p++] = "OPEN";
		}
	} else {
		pairs[p++] = "MENU";
		pairs[p++] = "OPTIONS";
		// Y still starts netplay when the hint is hidden (Appearance > Show netplay hint)
		if (t->kind != HOME_TILE_TOOL && CFG_getShowNetplayHint() && GameList_entryNetplayCapable(e)) {
			pairs[p++] = "Y";
			pairs[p++] = "NETPLAY";
		}
		pairs[p++] = "A";
		if (t->kind == HOME_TILE_TOOL) {
			size_t i = 0;
			for (const char* c = View_displayName(e); *c && i + 1 < sizeof(tool_name); c++)
				tool_name[i++] = (char)toupper((unsigned char)*c);
			tool_name[i] = '\0';
			pairs[p++] = tool_name;
		} else {
			pairs[p++] = plainDir(t) ? "OPEN" : focus_can_resume ? "RESUME"
																 : "PLAY";
		}
	}
	// The bar sits on cleared (transparent) screen and changes only with the focus: render it once into its own
	// surface and copy it (the scrim alone blends ~100k pixels a frame). A hardware hint (volume, brightness) is
	// drawn straight, uncached.
	int bar_h = barPx();
	if (PWR_getShowSetting() || bar_h <= 0 || bar_h > dst->h) {
		UI_renderButtonHintBar(dst, pairs);
		return;
	}
	char key[sizeof(hint_key)];
	int n = snprintf(key, sizeof(key), "%dx%d@%g", dst->w, bar_h, FIXED_SCALE);
	for (int i = 0; pairs[i] && n < (int)sizeof(key); i++)
		n += snprintf(key + n, sizeof(key) - n, "|%s", pairs[i]);
	if (!hint_bar || hint_bar->w != dst->w || hint_bar->h != bar_h || strcmp(key, hint_key) != 0) {
		if (hint_bar && (hint_bar->w != dst->w || hint_bar->h != bar_h)) {
			SDL_FreeSurface(hint_bar);
			hint_bar = NULL;
		}
		if (!hint_bar)
			hint_bar = SDL_CreateRGBSurfaceWithFormat(0, dst->w, bar_h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!hint_bar) {
			UI_renderButtonHintBar(dst, pairs);
			return;
		}
		SDL_FillRect(hint_bar, NULL, 0);
		UI_renderButtonHintBar(hint_bar, pairs); // its bar is the whole surface (bottom-aligned, bar_h tall)
		snprintf(hint_key, sizeof(hint_key), "%s", key);
	}
	SDL_SetSurfaceBlendMode(hint_bar, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(hint_bar, NULL, dst, &(SDL_Rect){0, dst->h - bar_h});
}
void Home_render(SDL_Surface* dst, int lastScreen) {
	(void)lastScreen;
	if (!dst || !Home_active())
		return;
	ensureBuilt();
	if (listMode()) // gamelist.c draws the List (Home_isList)
		return;
	int bar_h = barPx();
	int body_h = dst->h - 2 * bar_h;
	if (body_h > 0) {
		// the screen under the band is still clear here (nextui.c drew only the bars), so tiles go straight on
		SDL_SetClipRect(dst, &(SDL_Rect){0, bar_h, dst->w, body_h});
		int scroll_px = px(currentScroll());
		HomeStats st;
		currentStats(&st);

		// the page as one layer for the tab-focus dim (contentdim.h); the top band's fade and the hints stay lit
		// the body is clear under the page (nextui.c cleared it): the GPU dims it, unless the scrolled page's fade goes
		// over it after (it stays lit)
		SDL_Rect body = {0, bar_h, dst->w, body_h};
		if (scroll_px > 0)
			ContentDim_begin(dst, body);
		else
			ContentDim_beginClear(dst, body);
		drawStrip(dst, &st, scroll_px);
		if (carousel()) {
			// sprites: not under a context menu (it draws on the screen, which they would cover)
			car_sprites = !ContextMenu_isOpen();
			car_warming = false;
			drawCarousel(dst);
		} else {
			for (int i = 0; i < layout.ntop; i++)
				drawTile(dst, &layout.top[i], i, scroll_px);
			for (int i = 0; i < layout.npins; i++)
				drawTile(dst, &layout.pins[i], HOME_MAX_TOP + i, scroll_px);
		}
		ContentDim_end(dst);
		// scrolled: the top band's part below the tab strip goes over the page (nextui.c drew the strip's part)
		if (scroll_px > 0) {
			int fade_h = bar_h + homeDp(48);
			SDL_Surface* fade = UI_easedFadeSurface(dst->w, fade_h, 0.9f, 3.5f, true);
			if (fade)
				UI_blitFade(fade, &(SDL_Rect){0, bar_h, dst->w, fade_h - bar_h}, dst, 0, bar_h);
		}
		SDL_SetClipRect(dst, NULL);
	}
	renderHints(dst);
}

///////////////////////////////////////
// The List (gamelist.c draws its rows over Home_listDir)

bool Home_isList(void) {
	if (!screen || !Home_active())
		return false;
	ensureBuilt();
	return listMode() && hl_entries && hl_entries->count > 0;
}

Directory* Home_listDir(void) {
	return Home_isList() ? &hl_dir : NULL;
}

int Home_listTop(void) {
	return hl_geom.list_top;
}

void Home_listArtPath(char* out, size_t size) {
	out[0] = '\0';
	if (!Home_isList())
		return;
	const HomeTile* t = focusedTile();
	Entry* e = tileEntry(t);
	if (!e || t->kind == HOME_TILE_TOOL || plainDir(t))
		return;
	HomeArt_listPath(e->path, t->kind == HOME_TILE_CONTINUE && cont_preview[0] ? cont_preview : NULL, out, size);
}

const char* Home_listToolIcon(void) {
	if (!Home_isList())
		return NULL;
	const HomeTile* t = focusedTile();
	Entry* e = tileEntry(t);
	return e && t->kind == HOME_TILE_TOOL ? toolIcon(e) : NULL;
}

void Home_renderListStrip(SDL_Surface* dst) {
	if (!Home_isList())
		return;
	HomeStats st;
	currentStats(&st);
	drawStrip(dst, &st, 0);
}

bool Home_listHasStrip(void) {
	return Home_isList() && hl_geom.strip_lines > 0;
}

void Home_renderListHints(SDL_Surface* dst) {
	if (Home_isList())
		renderHints(dst);
}

// The "Continue" tag (the mockup's .ptag): a full pill in the list's text colour at 14% under the word in that colour,
// at the Continue badge's size (TEXT_HOME_CONT_BADGE); lit (under the selection pill), black at 14% over the accent
// with the selected rows' text colour. Each look made once into its own surface (straight alpha, as a text sprite's)
// and remade when its colours or size change.
static SDL_Surface* listTag(bool lit) {
	int tag_px = (int)TextPx_for(TEXT_HOME_CONT_BADGE, UIScale_deviceIndex(UI_DEVICE_NAME));
	TTF_Font* f = tag_px > 0 ? UIFont_getPx(tag_px, false) : NULL;
	if (!f)
		return NULL;
	const char* word = "Continue";
	SDL_Color ink = UI_getListTextColor(lit), ground = lit ? C_BLACK : UI_getListTextColor(false);
	Uint32 key = 2166136261u;
	key = View_fnv(key, &tag_px, sizeof(tag_px));
	key = View_fnv(key, &ink, sizeof(ink));
	key = View_fnv(key, &ground, sizeof(ground));
	int i = lit ? 1 : 0;
	if (tag_surf[i] && tag_key[i] == key)
		return tag_surf[i];
	GFX_freeSurfaceAndTexture(tag_surf[i]);
	tag_surf[i] = NULL;
	HomeListTag t = HomeList_tag(TTF_FontHeight(f), textW(f, word));
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, t.w, t.h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	// the pill's coverage at 14% (36 of 255), its colour straight
	float r = t.h / 2.0f;
	Uint32 rgb = (Uint32)ground.r << 16 | (Uint32)ground.g << 8 | ground.b;
	for (int y = 0; y < t.h; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
		for (int x = 0; x < t.w; x++) {
			float cx = x + 0.5f < r ? r : x + 0.5f > t.w - r ? t.w - r
															 : x + 0.5f;
			float dx = x + 0.5f - cx, dy = y + 0.5f - r;
			float cov = clamp01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
			row[x] = (Uint32)(cov * 36.0f + 0.5f) << 24 | rgb;
		}
	}
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	drawText(s, f, word, ink, t.pad_x, t.pad_y, 255);
	tag_surf[i] = s;
	tag_key[i] = key;
	return s;
}

int Home_listTagWidth(void) {
	SDL_Surface* s = listTag(false);
	int tag_px = (int)TextPx_for(TEXT_HOME_CONT_BADGE, UIScale_deviceIndex(UI_DEVICE_NAME));
	TTF_Font* f = s ? UIFont_getPx(tag_px, false) : NULL;
	return f ? s->w + HomeList_tag(TTF_FontHeight(f), 0).gap : 0;
}

void Home_listDrawTag(SDL_Surface* dst, int x, int text_y, int text_h, bool lit, bool sprite) {
	SDL_Surface* s = listTag(lit);
	if (!s)
		return;
	SDL_Rect r = {x, text_y + (text_h - s->h) / 2, s->w, s->h};
	SDL_Texture* tex = sprite ? PLAT_textureForSurface(s) : NULL;
	if (tex)
		PLAT_spriteAdd(tex, NULL, &r, 255, NULL);
	else
		SDL_BlitSurface(s, NULL, dst, &r);
}

bool Home_animating(void) {
	if (!Home_active()) {
		scroll_tw.active = sel_tw.active = slide_tw.active = false;
		scroll_from = scroll_to;
		pos_from = pos_to;
		return false;
	}
	bool a = tweenTick(&scroll_tw, SCROLL_MS);
	bool b = tweenTick(&sel_tw, SEL_MS);
	bool c = tweenTick(&slide_tw, SLIDE_MS);
	if (a && !scroll_tw.active)
		scroll_from = scroll_to;
	if (c && !slide_tw.active)
		pos_from = pos_to;
	return a || b || c || (carousel() && car_warming);
}

bool Home_scrolled(void) {
	return Home_active() && !carousel() && !listMode() && px(currentScroll()) > 0; // the Carousel never scrolls (the
																				   // List scrolls its rows instead)
}

///////////////////////////////////////
// Input

static void setFocus(HomeFocus f) {
	prev_id = tileId(focus);
	focus = f;
	tweenStart(&sel_tw);
	float u = unitPx();
	float target = HomeLayout_scrollFor(&layout, focus, screen->h / u, barPx() / u, scroll_to);
	if (fabsf(target - scroll_to) > 0.001f) {
		scroll_from = currentScroll();
		scroll_to = target;
		tweenStart(&scroll_tw);
		if (!scroll_tw.active)
			scroll_from = scroll_to;
	}
	readyFocus();
}

// The Carousel's focus: the dock's selection crossfades as Grid's tiles do (the row as one tile), and a new row
// selection slides the row there from wherever it is (a held D-pad retargets the slide, as the game lists' Carousel).
static void setCarFocus(HomeCarFocus f) {
	if (carId(f) != carId(car_focus)) {
		prev_id = carId(car_focus);
		tweenStart(&sel_tw);
	}
	if (f.sel != car_focus.sel) {
		pos_from = currentPos();
		pos_to = (float)f.sel;
		tweenStart(&slide_tw);
		if (!slide_tw.active)
			pos_from = pos_to;
	}
	car_focus = f;
	readyFocus();
}

// Resume (save state) or play, exactly as the list's A/X do; Home marks the launch so the return opens Home.
static void launchGame(Entry* e) {
	readyResume(e);
	resume.should_resume = resume.can_resume;
	MenuTabs_markHomeLaunch();
	Entry_open(e);
}

static void activate(bool* dirty) {
	*dirty = true;
	const HomeTile* t = focusedTile();
	if (!t)
		return;
	Entry* e = tileEntry(t);
	switch (t->kind) {
	case HOME_TILE_CONTINUE:
		if (cont)
			launchGame(cont);
		else
			GameList_openTab(MENU_TAB_CONSOLES, dirty); // Pick a game
		break;
	case HOME_TILE_TOOL:
		if (e && GameList_settingsPinAllows(e)) { // simple mode's Settings PIN
			MenuTabs_markHomeLaunch();
			Entry_open(e);
		}
		break;
	case HOME_TILE_GAME:
		if (!e)
			break;
		if (plainDir(t))
			Entry_open(e); // a legacy pinned folder opens as a list
		else
			launchGame(e);
		break;
	}
}

static bool sameFocus(HomeFocus a, HomeFocus b) {
	return tileId(a) == tileId(b);
}

void Home_focusBottom(void) {
	if (!screen || !Home_active())
		return;
	ensureBuilt();
	if (listMode()) { // the last row, windowed as the List's own wrap to the bottom
		int n = hl_entries ? hl_entries->count : 0;
		if (n > 0) {
			hl_dir.selected = n - 1;
			ListWindow_toBottom(n, GameList_rowCountFrom(hl_geom.list_top), &hl_dir.start, &hl_dir.end);
			readyFocus();
		}
		return;
	}
	if (carousel()) { // the dock when it has tools, else the row as it was
		HomeCarFocus cf = car_focus;
		HomeCar_fromTabs(&car, &cf);
		setCarFocus(cf);
		return;
	}
	HomeFocus f = focus;
	HomeLayout_fromTabs(&layout, &f);
	if (!sameFocus(f, focus))
		setFocus(f);
	else
		focus = f;
}

bool Home_handleInput(unsigned long now, bool* dirty) {
	if (!screen || !Home_active())
		return false;
	ensureBuilt();

	if (PAD_tappedMenu(now)) {
		const HomeTile* t = focusedTile();
		Entry* e = tileEntry(t);
		if (e) {
			GameList_openContextMenuFor(e, t->kind != HOME_TILE_CONTINUE, t->kind == HOME_TILE_CONTINUE);
			*dirty = true;
		}
		return true; // nothing for Pick a game
	}

	static const struct {
		int btn;
		HomeDir dir;
	} dpad[] = {{BTN_UP, HOME_DIR_UP}, {BTN_DOWN, HOME_DIR_DOWN}, {BTN_LEFT, HOME_DIR_LEFT}, {BTN_RIGHT, HOME_DIR_RIGHT}};
	for (size_t i = 0; i < sizeof(dpad) / sizeof(dpad[0]); i++) {
		if (!PAD_justRepeated(dpad[i].btn))
			continue;
		if (listMode()) { // as the main menu's List: UP/DOWN one row (a fresh press wraps), LEFT/RIGHT switch tab
			bool fresh = PAD_justPressed(dpad[i].btn);
			if (dpad[i].dir == HOME_DIR_LEFT || dpad[i].dir == HOME_DIR_RIGHT) {
				if (fresh) // fresh press only, so holding doesn't cycle
					GameList_switchTab(dpad[i].dir == HOME_DIR_LEFT ? -1 : 1, dirty);
				return true;
			}
			int n = hl_entries ? hl_entries->count : 0, sel = hl_dir.selected;
			if (n > 0 && ListWindow_step(n, GameList_rowCountFrom(hl_geom.list_top), dpad[i].dir == HOME_DIR_UP ? -1 : 1,
										 fresh, &sel, &hl_dir.start, &hl_dir.end)) {
				hl_dir.selected = sel;
				readyFocus();
				*dirty = true;
			}
			return true;
		}
		if (carousel()) {
			HomeCarFocus cf = car_focus;
			HomeMoveResult res = HomeCar_move(&car, &cf, dpad[i].dir);
			if (res == HOME_MOVE_MOVED) {
				setCarFocus(cf);
				*dirty = true;
			} else if (PAD_justPressed(dpad[i].btn)) { // a fresh press only: a held key stops at the end
				if (res == HOME_MOVE_TABS) {
					MenuTabs_setFocused(true);
					*dirty = true;
				} else if (res == HOME_MOVE_EDGE_PREV || res == HOME_MOVE_EDGE_NEXT) {
					GameList_switchTab(res == HOME_MOVE_EDGE_PREV ? -1 : 1, dirty);
				}
			}
			return true;
		}
		HomeFocus f = focus;
		HomeMoveResult res = HomeLayout_move(&layout, &f, dpad[i].dir);
		if (res == HOME_MOVE_MOVED) {
			if (!sameFocus(f, focus))
				setFocus(f);
			else
				focus = f; // the memory moved (the section's own tile is the same)
			*dirty = true;
		} else if (PAD_justPressed(dpad[i].btn)) { // a fresh press only: a held key stops at the edge
			if (res == HOME_MOVE_TABS) {
				MenuTabs_setFocused(true);
				*dirty = true;
			} else if (res == HOME_MOVE_EDGE_PREV || res == HOME_MOVE_EDGE_NEXT) {
				GameList_switchTab(res == HOME_MOVE_EDGE_PREV ? -1 : 1, dirty);
			}
		}
		return true;
	}

	if (PAD_justPressed(BTN_A)) {
		activate(dirty);
		return true;
	}
	return false;
}
