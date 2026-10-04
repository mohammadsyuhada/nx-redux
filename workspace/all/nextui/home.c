// The Home tab, B2 (docs/home-b2.md; home.h): the stats strip under the tab row, the top section (Continue and the tool
// squares) and the pinned games in rows under it. home_layout.c owns the geometry and the D-pad in Brick px,
// home_strip.c the strip's text; this file turns them into pixels, draws and handles input.
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
#include <string.h>
#include <time.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "utils.h"

#include "content.h"
#include "contentdim.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gamelist.h"
#include "home_layout.h"
#include "home_stats.h"
#include "home_strip.h"
#include "homeart.h"
#include "imgloader.h" // screen
#include "infoband.h"
#include "launcher.h"
#include "menuart.h"
#include "menutabs.h"
#include "placeholder_art.h"
#include "ui_accent.h"
#include "ui_font.h"
#include "tiles.h"
#include "ui_buttonhintbar.h"
#include "ui_ease.h"
#include "ui_fade.h"
#include "view_common.h"

#define BORDER_ALPHA 20 // white at 8%
#define SEL_MS 120		// selection crossfade
#define SCROLL_MS 320	// page scroll, UI_easeStandard
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
#define PIN_INFO_PX 28.6f
#define PIN_INFO_LH 36.4f
#define PIN_SIDE 20.0f
#define PIN_UP 16.0f
#define PIN_LINE_GAP 4.0f
#define MORE_PX 30.0f

static const SDL_Color C_WHITE = {255, 255, 255, 255};
static const SDL_Color C_BLACK = {0, 0, 0, 255};
static const SDL_Color C_GREY = {0x99, 0x99, 0x99, 255}; // COLOR_GRAY, the hint grey
static const SDL_Color C_DOT = {0x55, 0x55, 0x55, 255};	 // the strip's middle dot
#define LIT_DIM_ALPHA 140								 // the lit card's secondary text: its ink at 140/255 over the accent (black on white = 0x73)

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
// Secondary text: grey plain; lit, the ink mixed over the accent at LIT_DIM_ALPHA, as one opaque colour so the
// default theme draws exactly the old 0x73 grey: (0 * 140 + 255 * 115 + 127) / 255 = 115.
static SDL_Color cardDim(bool lit) {
	if (!lit)
		return C_GREY;
	SDL_Color bg = cardBg(true), ink = cardInk(true);
	const int a = LIT_DIM_ALPHA;
	return (SDL_Color){(Uint8)((ink.r * a + bg.r * (255 - a) + 127) / 255),
					   (Uint8)((ink.g * a + bg.g * (255 - a) + 127) / 255),
					   (Uint8)((ink.b * a + bg.b * (255 - a) + 127) / 255), 255};
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
static int built_w = 0, built_h = 0, built_scale = 0, built_lines = -1;

static HomeLayout layout;
static Entry* cont = NULL; // owned; NULL = no Continue (the Pick-a-game card takes its slot)
static char cont_preview[MAX_PATH];
static Entry* games[HOME_MAX_PINS];		   // borrowed from stack[0], valid while its serial is built_root
static bool game_plain_dir[HOME_MAX_PINS]; // a legacy pinned folder (no cue/m3u): A opens it
static int ngames = 0;
static Entry* tools[HOME_MAX_PINS];
static int ntools = 0;

static HomeFocus focus = {HOME_SEC_TOP, 0, 0, -1};
static bool focus_can_resume = false;

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

///////////////////////////////////////
// Units and timing

// Screen px per Brick px: the Brick draws at 3x, the Smart Pro S at 2x.
static float unitPx(void) {
	return FIXED_SCALE / 3.0f;
}

static int px(float bpx) {
	return (int)floorf(bpx * unitPx() + 0.5f);
}

// The stats strip keeps the Large scale's size whatever the UI scale: its Brick px at 1:1 (× this in px()).
static float stripK(void) {
	return 3.0f / FIXED_SCALE;
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
	case HOME_TILE_MORE:
		return NULL;
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

static int stripLines(void) {
	HomeStats st;
	currentStats(&st);
	StripInput in = stripInput(&st);
	return HomeStrip_lineCount(&in);
}

// The geometry for this screen and strip; the focus kept in range, the scroll kept, clamped (no tween across it).
static void relayout(int lines) {
	float u = unitPx();
	built_lines = lines;
	HomeLayout_computeStrip(screen->w / u, screen->h / u, barPx() / u, lines, stripK(), ngames, ntools, &layout);
	focus = HomeLayout_clampFocus(&layout, focus);
	scroll_to = HomeLayout_scrollFor(&layout, focus, screen->h / u, barPx() / u, scroll_to);
	scroll_from = scroll_to;
	scroll_tw.active = false;
	sel_tw.active = false;
}

static void rebuild(void) {
	if (!screen || !stack || stack->count < 1)
		return;
	need_rebuild = false;
	built_root = ((Directory*)stack->items[0])->serial;
	built_w = screen->w;
	built_h = screen->h;
	built_scale = FIXED_SCALE;

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
		HomeStats_request();
	}
	// Home's own inputs only: a reset, the root list (the pins are borrowed from it) and the screen. Not the tab
	// generation: stepping tabs alone changes nothing Home shows.
	if (need_rebuild || built_root != ((Directory*)stack->items[0])->serial || built_w != screen->w ||
		built_h != screen->h || built_scale != FIXED_SCALE) {
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
	if (hint_bar)
		SDL_FreeSurface(hint_bar);
	hint_bar = NULL;
	hint_key[0] = '\0';
	if (strip_surf)
		SDL_FreeSurface(strip_surf);
	strip_surf = NULL;
	strip_key = 0;
	cardCacheClear();
	need_rebuild = true;
}

///////////////////////////////////////
// Game info

// The time segment ("Today - 18m 37s") for a game; "" without one.
static void timeText(const char* path, char* out, size_t size) {
	out[0] = '\0';
	GameInfo info;
	if (!path || !GameInfo_get(path, &info) || !info.has_time)
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

// Continue: the art full-bleed under the caption fade; the title (44, one line with "…") and the time (31, grey), 29
// in, baselines 74 and 31 above the bottom (the art: the game's own, else its abstract picture). No picture at all: the
// name centred (up to 2 lines) over the time.
static void composeContinue(SDL_Surface* s, int w, int h) {
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	SDL_Surface* pic = NULL;
	HomeArtState st = HomeArt_continue(cont->path, cont_preview[0] ? cont_preview : NULL, w, h, 0, &pic);
	if (pic) {
		SDL_BlitSurface(pic, NULL, s, &(SDL_Rect){0, 0});
		captionFade(s, w, h);
	}
	int inset = px(CONT_INSET);
	char when[160];
	timeText(cont->path, when, sizeof(when));
	TTF_Font* f = UIFont_getPx(px(CONT_SUB_PX), false);
	if (f && when[0]) {
		char line[LINE_MAX];
		ellipsize(f, when, line, w - 2 * inset);
		drawTextBaseline(s, f, line, C_GREY, inset, h - px(CONT_SUB_UP), 255);
	}
	f = UIFont_getPx(px(CONT_TITLE_PX), false);
	if (!f)
		return;
	if (st == HOMEART_NONE) { // a title card
		char lines[2][LINE_MAX];
		int nl = wrapLines(f, View_displayName(cont), w - 2 * inset, 2, lines);
		int lh = (int)(TTF_FontHeight(f) * 1.1f + 0.5f);
		drawCentredLines(s, f, lines, nl, lh, C_WHITE, w, 0, h - px(CONT_TITLE_UP));
	} else {
		char name[LINE_MAX];
		ellipsize(f, View_displayName(cont), name, w - 2 * inset);
		drawTextBaseline(s, f, name, C_WHITE, inset, h - px(CONT_TITLE_UP), 255);
	}
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
	TTF_Font* big = UIFont_get(24, false);
	int big_h = big ? TTF_FontHeight(big) : 0;
	TTF_Font* small = UIFont_get(14, false);
	int small_h = small ? TTF_FontHeight(small) : 0;
	if (big_h > 0 && small_h > 0) {
		const char* title = "Pick a game";
		const char* sub = "Nothing played yet";
		int gap = NX_DPF(4);
		int y = (h - (big_h + gap + small_h)) / 2;
		big = UIFont_get(24, false);
		drawText(s, big, title, C_WHITE, (w - textW(big, title)) / 2, y, 255);
		y += big_h + gap;
		small = UIFont_get(14, false);
		drawText(s, small, sub, C_GREY, (w - textW(small, sub)) / 2, y, 255);
	}
	tileBorder(s, w, h);
}

// A pinned game: its art; lit, the caption fade and the name (33.8 on a 41.6 line, white)
// over the time (28.6 on a 36.4 line, grey), 20 in, 16 up, 4 apart. No art: the name centred (lit: the time alone
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
	int side = px(PIN_SIDE);
	int info_lh = px(PIN_INFO_LH), name_lh = px(PIN_NAME_LH);
	int info_top = h - px(PIN_UP) - info_lh;
	int name_top = (when[0] ? info_top : h - px(PIN_UP)) - px(PIN_LINE_GAP) - name_lh;
	if (lit && when[0]) {
		TTF_Font* f = UIFont_getPx(px(PIN_INFO_PX), false);
		char line[LINE_MAX];
		ellipsize(f, when, line, w - 2 * side);
		drawInLine(s, f, line, C_GREY, side, info_top, info_lh);
	}
	if (title_tile) {
		// the name above the caption, centred; 15 sp, smaller (to 11) until its longest word fits
		float sp = Tiles_fitWordsSp(View_displayName(e), 15, 11, false, w - 2 * side);
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
		TTF_Font* f = UIFont_getPx(px(PIN_NAME_PX), false);
		char name[LINE_MAX];
		ellipsize(f, View_displayName(e), name, w - 2 * side);
		drawInLine(s, f, name, C_WHITE, side, name_top, name_lh);
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

// A tool's square: its icon centred as a white mask (no label); lit, the accent fills the plate and the icon turns to
// its ink (black), with no ring and no edge.
static void composeTool(SDL_Surface* s, int w, int h, bool lit, int t) {
	SDL_Color bg = cardBg(lit);
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, bg.r, bg.g, bg.b, 255));
	int g = px(layout.glyph);
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

// "+N": the tools past the squares (A opens the Tools tab), 30 px grey; lit, the ink dimmed over the accent.
static void composeMore(SDL_Surface* s, int w, int h, bool lit, int n) {
	SDL_Color bg = cardBg(lit);
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, bg.r, bg.g, bg.b, 255));
	TTF_Font* f = UIFont_getPx(px(MORE_PX), false);
	if (f) {
		char text[16];
		snprintf(text, sizeof(text), "+%d", n);
		drawText(s, f, text, cardDim(lit), (w - textW(f, text)) / 2, (h - TTF_FontHeight(f)) / 2, 255);
	}
	if (!lit)
		tileBorder(s, w, h);
}

///////////////////////////////////////
// The stats strip

static SDL_Color runColour(StripTone t) {
	return t == STRIP_WHITE ? C_WHITE : t == STRIP_DOT ? C_DOT
													   : C_GREY;
}

// One line's runs from x on baseline y; the run that gives way is cut with "…" so the line ends by right.
static void drawStripLine(SDL_Surface* s, TTF_Font* f, StripLine* l, int x, int right, int baseline) {
	float em = (float)px(STRIP_PX * stripK());
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
	for (int i = 0; i < l->n; i++) {
		StripRun* r = &l->runs[i];
		x += (int)(em * r->pad_l + 0.5f);
		drawTextBaseline(s, f, r->text, runColour(r->tone), x, baseline, 255);
		x += textW(f, r->text) + (int)(em * r->pad_r + 0.5f);
	}
}

// The strip under the tab row (not selectable), rendered whole into its own surface when its text changes.
static void drawStrip(SDL_Surface* dst, const HomeStats* st, int scroll_px) {
	if (layout.strip_lines <= 0)
		return;
	StripInput in = stripInput(st);
	StripLine l1, l2;
	HomeStrip_build(&in, &l1, &l2);
	TTF_Font* f = UIFont_getPx(px(STRIP_PX * stripK()), false);
	if (!f)
		return;
	Uint32 key = 2166136261u;
	key = View_fnv(key, &dst->w, sizeof(dst->w));
	key = View_fnv(key, &layout.strip_lines, sizeof(layout.strip_lines));
	int scale = FIXED_SCALE;
	key = View_fnv(key, &scale, sizeof(scale));
	for (int i = 0; i < l1.n; i++)
		key = View_fnvStr(key, l1.runs[i].text);
	for (int i = 0; i < l2.n; i++)
		key = View_fnvStr(key, l2.runs[i].text);
	int top = px(layout.strip_base[0]) - TTF_FontAscent(f);
	int last = layout.strip_lines == 2 ? 1 : 0;
	int h = px(layout.strip_base[last]) - TTF_FontDescent(f) - top + 1;
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
		int x = px(layout.strip_x), right = px(layout.strip_right);
		drawStripLine(strip_surf, f, &l1, x, right, px(layout.strip_base[0]) - top);
		if (l2.n > 0)
			drawStripLine(strip_surf, f, &l2, x, right, px(layout.strip_base[1]) - top);
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
			   CARD_MORE } CardKind;

// A tile's identity across frames (the selection crossfade): a top tile by its index, a row pin past them.
static int tileId(HomeFocus f) {
	f = HomeLayout_clampFocus(&layout, f);
	return f.sec == HOME_SEC_PINS ? HOME_MAX_TOP + f.pin : f.top;
}

// How lit a tile is (0..1): the focused tile fades in, the previous one out. (While the tab row has focus the whole
// page dims as one layer: contentdim.c.)
static float litAmount(int id) {
	float p = tweenProgress(&sel_tw, SEL_MS);
	if (id == tileId(focus))
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
	int ref, w, h, scale;
	bool lit;
	Uint32 stamp; // what the card shows (see cardStamp)
	unsigned lru;
	SDL_Surface* surf;
} CardSlot;

static CardSlot card_cache[CARD_CACHE_MAX];
static unsigned card_lru = 0;

static void cardCacheClear(void) {
	for (int i = 0; i < CARD_CACHE_MAX; i++) {
		if (card_cache[i].surf)
			SDL_FreeSurface(card_cache[i].surf);
		card_cache[i] = (CardSlot){0};
	}
}

// Every visible tile in both looks.
static int cardCacheLimit(void) {
	int n = 2 * (layout.ntop + layout.npins) + 2;
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
// w, h, FIXED_SCALE, lit).
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
	case CARD_TOOL:
		hs = fnvStr(hs, tools[ref]->path);
		hs = fnvStr(hs, View_displayName(tools[ref]));
		hs = View_fnv(hs, &layout.glyph, sizeof(layout.glyph));
		break;
	case CARD_MORE:
		break;
	}
	return hs;
}

static void composeCardKind(SDL_Surface* s, CardKind kind, int w, int h, bool lit, int ref) {
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
	case CARD_MORE:
		composeMore(s, w, h, lit, ref);
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
		if (c->kind == kind && c->ref == ref && c->w == w && c->h == h && c->scale == FIXED_SCALE && c->lit == lit) {
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
		SDL_FreeSurface(victim->surf);
		victim->surf = NULL;
	}
	if (!victim->surf)
		victim->surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!victim->surf) {
		victim->used = false;
		return NULL;
	}
	SDL_Surface* surf = victim->surf;
	*victim = (CardSlot){.used = true, .kind = kind, .ref = ref, .w = w, .h = h, .scale = FIXED_SCALE, .lit = lit, .stamp = stamp, .lru = ++card_lru, .surf = surf};
	composeCardKind(surf, kind, w, h, lit, ref);
	return surf;
}

// A cached card at (r.x, r.y): the four corner squares (the only pixels with alpha, see maskCorners) through SDL's
// blend, the opaque rest through UI_blitOpaque. under (optional, see UI_blitOpaque): the card's other look, drawn in the
// same pass with surf at alpha over it.
static void blitCardOver(SDL_Surface* dst, SDL_Surface* surf, SDL_Surface* under, SDL_Rect r, int alpha) {
	if (!surf || alpha <= 0)
		return;
	if (under && (under->w != surf->w || under->h != surf->h))
		under = NULL;
	if (alpha > 255)
		alpha = 255;
	int w = surf->w, h = surf->h;
	int rad = clampRadius(radiusPx(), w, h);
	if (rad > 0) {
		if (under)
			for (int j = 0; j < 2; j++)
				for (int i = 0; i < 2; i++) {
					int x = i ? w - rad : 0, y = j ? h - rad : 0;
					SDL_BlitSurface(under, &(SDL_Rect){x, y, rad, rad}, dst, &(SDL_Rect){r.x + x, r.y + y});
				}
		SDL_SetSurfaceAlphaMod(surf, (Uint8)alpha);
		int xs[2] = {0, w - rad}, ys[2] = {0, h - rad};
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++)
				SDL_BlitSurface(surf, &(SDL_Rect){xs[i], ys[j], rad, rad}, dst, &(SDL_Rect){r.x + xs[i], r.y + ys[j]});
		SDL_SetSurfaceAlphaMod(surf, 255);
	}
	UI_blitOpaque(surf, under, rad, 0, w - 2 * rad, rad, dst, r.x + rad, r.y, alpha);				  // top, between the corners
	UI_blitOpaque(surf, under, 0, rad, w, h - 2 * rad, dst, r.x, r.y + rad, alpha);					  // the full-width middle
	UI_blitOpaque(surf, under, rad, h - rad, w - 2 * rad, rad, dst, r.x + rad, r.y + h - rad, alpha); // bottom
}

static void blitCard(SDL_Surface* dst, SDL_Surface* surf, SDL_Rect r, int alpha) {
	blitCardOver(dst, surf, NULL, r, alpha);
}
static CardKind cardKind(const HomeTile* t) {
	switch (t->kind) {
	case HOME_TILE_CONTINUE:
		return cont ? CARD_CONTINUE : CARD_PICK;
	case HOME_TILE_GAME:
		return CARD_GAME;
	case HOME_TILE_TOOL:
		return CARD_TOOL;
	case HOME_TILE_MORE:
		return CARD_MORE;
	}
	return CARD_PICK;
}

// dst's clip rect is the page band: a tile wholly outside it costs nothing. A lit game (Continue, a pin) and Pick a
// game wear the 6 px ring outside it; a lit tool square (and "+N") its filled look instead.
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
					 (int)(lit * 255 + 0.5f));
		return;
	}
	blitCard(dst, cachedCard(kind, t->ref, r.w, r.h, lit >= 1.0f && lit_differs), r, 255);
}

static void renderHints(SDL_Surface* dst) {
	char* pairs[10] = {NULL};
	int p = 0;
	pairs[p++] = "SELECT";
	pairs[p++] = "RECENT";
	const HomeTile* t = focusedTile();
	Entry* e = tileEntry(t);
	static char tool_name[64]; // the A label on a tool square: its name in capitals
	if (MenuTabs_focused()) {  // the tab row has focus: exactly SELECT RECENT and A OPEN (A returns to Home)
		pairs[p++] = "A";
		pairs[p++] = "OPEN";
	} else if (t && t->kind == HOME_TILE_MORE) { // "+N": A opens the Tools tab
		if (MenuTabs_isVisible(MENU_TAB_TOOLS)) {
			pairs[p++] = "A";
			pairs[p++] = "OPEN";
		}
	} else if (!e) { // Pick a game: A opens the Consoles tab, when there is one
		if (MenuTabs_isVisible(MENU_TAB_CONSOLES)) {
			pairs[p++] = "A";
			pairs[p++] = "OPEN";
		}
	} else {
		pairs[p++] = "MENU";
		pairs[p++] = "OPTIONS";
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
	int n = snprintf(key, sizeof(key), "%dx%d@%d", dst->w, bar_h, FIXED_SCALE);
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
	int bar_h = barPx();
	int body_h = dst->h - 2 * bar_h;
	if (body_h > 0) {
		// the screen under the band is still clear here (nextui.c drew only the bars), so tiles go straight on
		SDL_SetClipRect(dst, &(SDL_Rect){0, bar_h, dst->w, body_h});
		int scroll_px = px(currentScroll());
		HomeStats st;
		currentStats(&st);

		// the page as one layer for the tab-focus dim (contentdim.h); the top band's fade and the hints stay lit
		ContentDim_begin(dst, (SDL_Rect){0, bar_h, dst->w, body_h});
		drawStrip(dst, &st, scroll_px);
		for (int i = 0; i < layout.ntop; i++)
			drawTile(dst, &layout.top[i], i, scroll_px);
		for (int i = 0; i < layout.npins; i++)
			drawTile(dst, &layout.pins[i], HOME_MAX_TOP + i, scroll_px);
		ContentDim_end(dst);
		// scrolled: the top band's part below the tab strip goes over the page (nextui.c drew the strip's part)
		if (scroll_px > 0) {
			int fade_h = bar_h + NX_DP(48);
			SDL_Surface* fade = UI_easedFadeSurface(dst->w, fade_h, 0.9f, 3.5f, true);
			if (fade)
				UI_blitFade(fade, &(SDL_Rect){0, bar_h, dst->w, fade_h - bar_h}, dst, 0, bar_h);
		}
		SDL_SetClipRect(dst, NULL);
	}
	renderHints(dst);
}

bool Home_animating(void) {
	if (!Home_active()) {
		scroll_tw.active = sel_tw.active = false;
		scroll_from = scroll_to;
		return false;
	}
	bool a = tweenTick(&scroll_tw, SCROLL_MS);
	bool b = tweenTick(&sel_tw, SEL_MS);
	if (a && !scroll_tw.active)
		scroll_from = scroll_to;
	return a || b;
}

bool Home_scrolled(void) {
	return Home_active() && px(currentScroll()) > 0;
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
	case HOME_TILE_MORE: // every tool is listed in the Tools tab
		if (MenuTabs_isVisible(MENU_TAB_TOOLS))
			GameList_openTab(MENU_TAB_TOOLS, dirty);
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
		return true; // nothing for Pick a game or "+N"
	}

	static const struct {
		int btn;
		HomeDir dir;
	} dpad[] = {{BTN_UP, HOME_DIR_UP}, {BTN_DOWN, HOME_DIR_DOWN}, {BTN_LEFT, HOME_DIR_LEFT}, {BTN_RIGHT, HOME_DIR_RIGHT}};
	for (size_t i = 0; i < sizeof(dpad) / sizeof(dpad[0]); i++) {
		if (!PAD_justRepeated(dpad[i].btn))
			continue;
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
