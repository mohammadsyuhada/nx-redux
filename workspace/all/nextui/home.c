// The Home tab (see home.h and §9 of the layout reference): Continue and the stats card on top, the pins in rows
// under them. home_layout.c owns the geometry in dp; this file turns it into pixels, draws and handles input.
//
// Drawing: the page is painted straight onto the screen, clipped to the band between the tab strip and the hint bar
// so scrolled cards are cut at both bars. Each card is composed once into its own surface (base, picture, fade, text,
// border, anti-aliased corners) and cached under a key of its kind, size, look (plain/lit, stats face) and a stamp of
// what it shows; a frame only blits cached cards, and a card is recomposed only when its key changes. A selection
// crossfade blends the cached lit look over the plain one (the ring at the same alpha); the stats card's flip blends
// its two cached faces.
//
// Frame cost (the Brick's A133 draws the UI in software): SDL's per-pixel-alpha blit runs ~20 ns a pixel there, so a
// card goes through it only for its four anti-aliased corners. Everything else in a card is opaque and is copied, or
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
#include "homeart.h"
#include "imgloader.h" // screen
#include "infoband.h"
#include "launcher.h"
#include "menuart.h"
#include "menutabs.h"
#include "ui_accent.h"
#include "ui_font.h"
#include "tiles.h"
#include "ui_buttonhintbar.h"
#include "ui_ease.h"
#include "ui_fade.h"

#define RADIUS_DP 14.0f
#define RING_DP 3.0f
#define BORDER_ALPHA 20 // white at 8%
#define SEL_MS 120		// selection crossfade
#define FLIP_MS 200		// stats card face crossfade
#define SCROLL_MS 320	// page scroll, UI_easeStandard
#define CARD_PAD_DP 16.0f
#define TILE_SIDE_DP 12.0f	 // pin tile caption insets
#define TILE_BOTTOM_DP 10.0f // (Continue uses 16 / 12)
#define ELLIPSIS "\xE2\x80\xA6"
#define LINE_MAX 256

static const SDL_Color C_WHITE = {255, 255, 255, 255};
static const SDL_Color C_BLACK = {0, 0, 0, 255};
static const SDL_Color C_GREY = {0x99, 0x99, 0x99, 255}; // COLOR_GRAY
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
// A watermark lit: tinted to the ink (black at the default theme), its alpha unchanged.
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
static int built_w = 0, built_h = 0, built_scale = 0;

static HomeLayout layout;
static Entry* cont = NULL; // owned; NULL = no Continue (the Pick-a-game card takes its slot)
static char cont_preview[MAX_PATH];
static Entry* pins[HOME_MAX_PINS];		  // borrowed from stack[0], valid while its serial is built_root
static bool pin_plain_dir[HOME_MAX_PINS]; // a legacy pinned folder (no cue/m3u): A opens it
static int npins = 0;

static HomeFocus focus = {HOME_FOCUS_CONTINUE, 0};
static HomeFocusMemory mem = {HOME_FOCUS_CONTINUE, 0};
static bool focus_can_resume = false;

typedef struct {
	bool active;
	Uint32 start;
} Tween;

static HomeFocus prev_focus = {HOME_FOCUS_CONTINUE, 0};
static Tween sel_tw;
static float scroll_from = 0, scroll_to = 0; // dp
static Tween scroll_tw;
static bool face_activity = false; // the stats card's face, kept for the session
static Tween flip_tw;

static SDL_Surface* hint_bar = NULL; // the rendered hint bar (see renderHints)
static char hint_key[128];

static void cardCacheClear(void);

///////////////////////////////////////
// Units and timing

static float pxPerDp(void) {
	return FIXED_SCALE * 30.0f / 42.0f;
}

static int barPx(void) {
	return SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2);
}

static bool animationsOn(void) {
	return CFG_getMenuAnimations();
}

static float tweenProgress(const Tween* t, Uint32 ms) {
	if (!t->active)
		return 1.0f;
	Uint32 elapsed = SDL_GetTicks() - t->start;
	return elapsed >= ms ? 1.0f : (float)elapsed / (float)ms;
}

static void tweenStart(Tween* t) {
	t->active = animationsOn();
	t->start = SDL_GetTicks();
}

// One settled frame: a finished tween reports true once more as it clears.
static bool tweenTick(Tween* t, Uint32 ms) {
	if (!t->active)
		return false;
	if (SDL_GetTicks() - t->start >= ms)
		t->active = false;
	return true;
}

static float currentScroll(void) {
	if (!scroll_tw.active)
		return scroll_to;
	return scroll_from + (scroll_to - scroll_from) * UI_easeStandard(tweenProgress(&scroll_tw, SCROLL_MS));
}

static SDL_Rect toScreen(HomeRect r, int scroll_px) {
	int x0 = NX_DPF(r.x), x1 = NX_DPF(r.x + r.w);
	int y0 = NX_DPF(r.y), y1 = NX_DPF(r.y + r.h);
	return (SDL_Rect){x0, y0 - scroll_px, x1 - x0, y1 - y0};
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

static const char* displayName(Entry* e) {
	char* name = e->unique ? e->unique : e->name;
	trimSortingMeta(&name);
	return name;
}

static bool signedIn(void) {
	return CFG_getRAEnable() && CFG_getRAAuthenticated();
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

static bool isGamePin(int i) {
	return i >= 0 && i < npins && layout.pins[i].span == 2;
}

// The heatmap cell (dp): the largest whole size up to 20 that fits the card's height and width.
static float cellDp(void) {
	float by_h = floorf((layout.card.h - 2 * CARD_PAD_DP - 15 - 16 - 16) / 5);
	float by_w = floorf((layout.card.w - 2 * CARD_PAD_DP - 24) / 7);
	return fminf(20, fminf(by_h, by_w));
}

// The 12 dp rule: smaller cells mean the card shows only the stats face.
static bool canFlip(void) {
	return layout.mode != HOME_MODE_FRESH && layout.card.w > 0 && cellDp() >= 12;
}

Entry* Home_focusedEntry(void) {
	if (!Home_active())
		return NULL;
	ensureBuilt(); // the pins are borrowed from stack[0]: never hand out one from a replaced list
	if (focus.area == HOME_FOCUS_CONTINUE)
		return cont;
	if (focus.area == HOME_FOCUS_PIN && focus.pin >= 0 && focus.pin < npins)
		return pins[focus.pin];
	return NULL;
}

// The focused game's resume state for the hint (readyResume stats the SD card: once per focus change).
static void readyFocus(void) {
	Entry* e = Home_focusedEntry();
	focus_can_resume = false;
	if (!e || e->type == ENTRY_PAK || (focus.area == HOME_FOCUS_PIN && pin_plain_dir[focus.pin]))
		return;
	readyResume(e);
	focus_can_resume = resume.can_resume;
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

	// games (ROMs, folder games, legacy folders) in stored order, then tools; a missing tool and the
	// Continue game are skipped (both stay pinned)
	Directory* root = stack->items[0];
	HomePinKind kinds[HOME_MAX_PINS];
	npins = 0;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < root->entries->count && npins < HOME_MAX_PINS; i++) {
			Entry* e = root->entries->items[i];
			bool tool = e->type == ENTRY_PAK;
			if (tool != (pass == 1))
				continue;
			if (tool && !exists(e->path))
				continue;
			if (!tool && cont && exactMatch(e->path, cont->path))
				continue;
			pins[npins] = e;
			pin_plain_dir[npins] = e->type == ENTRY_DIR && !canPinEntry(e);
			kinds[npins] = tool ? HOME_PIN_TOOL : HOME_PIN_GAME;
			npins++;
		}
	}

	float dp = pxPerDp();
	float sh = screen->h / dp;
	HomeLayout_compute(screen->w / dp, sh, cont != NULL, kinds, npins, &layout);
	focus = HomeLayout_clampFocus(&layout, focus);
	if (mem.last_pin >= npins)
		mem.last_pin = npins > 0 ? npins - 1 : 0;
	if (mem.last_top == HOME_FOCUS_CARD && layout.mode == HOME_MODE_FRESH)
		mem.last_top = HOME_FOCUS_CONTINUE;

	// keep the scroll, clamped, and the focused pin in view; no tween across a rebuild
	scroll_to = HomeLayout_scrollFor(&layout, focus, sh, barPx() / dp, scroll_to);
	scroll_from = scroll_to;
	scroll_tw.active = false;
	sel_tw.active = false;
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
	if (!need_rebuild && built_root == ((Directory*)stack->items[0])->serial && built_w == screen->w &&
		built_h == screen->h && built_scale == FIXED_SCALE)
		return;
	rebuild();
}

void Home_reset(void) {
	need_rebuild = true;
}

void Home_quit(void) {
	if (cont)
		Entry_free(cont);
	cont = NULL;
	npins = 0;
	if (hint_bar)
		SDL_FreeSurface(hint_bar);
	hint_bar = NULL;
	hint_key[0] = '\0';
	cardCacheClear();
	need_rebuild = true;
}

///////////////////////////////////////
// Game info

// Time segment (and, signed in, the achievement segments) for a game. full adds Next.
static int infoSegs(const char* path, bool want_time, bool want_ach, bool full, InfoSeg segs[3]) {
	GameInfo info;
	if (!path || !GameInfo_get(path, &info))
		return 0;
	bool ach = want_ach && info.has_ra && signedIn();
	bool tm = want_time && info.has_time;
	if (!ach && !tm)
		return 0;
	return GameInfo_segments(time(NULL), tm ? info.last_played : 0, tm ? info.seconds : -1, ach ? info.unlocked : 0,
							 ach ? info.total : 0, ach && full ? info.next : NULL, full, segs);
}

///////////////////////////////////////
// Cards (each composed at 0,0 into its cached surface)

static void tileBorder(SDL_Surface* s, int w, int h) {
	int t = NX_DPF(1) > 0 ? NX_DPF(1) : 1;
	strokeRounded(s, 0, 0, w, h, NX_DPF(RADIUS_DP), t, C_WHITE, BORDER_ALPHA);
}

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

static void composeContinue(SDL_Surface* s, int w, int h) {
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	SDL_Surface* pic = NULL;
	HomeArtState st = HomeArt_continue(cont->path, cont_preview[0] ? cont_preview : NULL, w, h, 0, &pic);
	bool title_card = st == HOMEART_NONE;
	if (pic) {
		SDL_BlitSurface(pic, NULL, s, &(SDL_Rect){0, 0});
		captionFade(s, w, h);
	}

	// bottom-up: trophy row, time row, the name (a title card has its name centred instead)
	int side = NX_DPF(16);
	int y = h - NX_DPF(12);
	int line_h = font.small ? TTF_FontHeight(font.small) : 0;
	InfoSeg segs[3];
	int n = infoSegs(cont->path, false, true, true, segs);
	if (n > 0) {
		y -= line_h;
		InfoBand_drawSegments(s, segs, n, side, false, y, w - 2 * side, font.small);
	}
	n = infoSegs(cont->path, true, false, false, segs);
	if (n > 0) {
		y -= line_h;
		InfoBand_drawSegments(s, segs, n, side, false, y, w - 2 * side, font.small);
	}
	if (title_card) {
		TTF_Font* f = UIFont_get(24, false);
		char lines[2][LINE_MAX];
		int nl = wrapLines(f, displayName(cont), w - 2 * side, 2, lines);
		drawCentredLines(s, f, lines, nl, (int)(NX_SP(24) * 1.15f + 0.5f), C_WHITE, w, 0, y);
	} else {
		TTF_Font* f = UIFont_get(20, false);
		char name[LINE_MAX];
		ellipsize(f, displayName(cont), name, w - 2 * side);
		if (f && name[0])
			drawText(s, f, name, C_WHITE, side, y - TTF_FontHeight(f), 255);
	}
	tileBorder(s, w, h);
}

static void composePick(SDL_Surface* s, int w, int h, bool lit) {
	SDL_Color bg = cardBg(lit);
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, bg.r, bg.g, bg.b, 255));
	// the mark's ink (the PNG is cropped to it) 60% of the card tall, its width following
	SDL_Surface* mark = MenuArt_get("nx_mark.png", w, (int)(h * 0.6f));
	if (mark) {
		if (lit)
			tintLit(mark);
		SDL_SetSurfaceAlphaMod(mark, lit ? 20 : 26); // the ink (default black) at 8% / white at 10%
		SDL_BlitSurface(mark, NULL, s, &(SDL_Rect){(w - mark->w) / 2, (h - mark->h) / 2});
		SDL_SetSurfaceColorMod(mark, 255, 255, 255);
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
		drawText(s, big, title, cardInk(lit), (w - textW(big, title)) / 2, y, 255);
		y += big_h + gap;
		small = UIFont_get(14, false);
		drawText(s, small, sub, cardDim(lit), (w - textW(small, sub)) / 2, y, 255);
	}
	if (!lit)
		tileBorder(s, w, h);
}

static void composeGame(SDL_Surface* s, int w, int h, bool lit, int i) {
	Entry* e = pins[i];
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, 0, 0, 0, 255));
	SDL_Surface* pic = NULL;
	HomeArtState st = HomeArt_pin(e->path, w, h, 0, &pic);
	bool title_tile = st == HOMEART_NONE;
	if (pic)
		SDL_BlitSurface(pic, NULL, s, &(SDL_Rect){0, 0});

	InfoSeg segs[3];
	int nseg = lit ? infoSegs(e->path, true, true, false, segs) : 0; // count only: time + "n of m"
	int side = NX_DPF(TILE_SIDE_DP);
	if (lit)
		captionFade(s, w, h);
	if (title_tile) {
		// the name above the fade, centred in the tile less the caption room; 15 sp, smaller (to 11) until its
		// longest word fits
		float sp = Tiles_fitWordsSp(displayName(e), 15, 11, false, w - 2 * side);
		TTF_Font* f = UIFont_get(sp, false);
		if (f) {
			int caption = nseg > 0 ? NX_DPF(14 + 8) : 0;
			float lh = NX_SP(sp) * 1.15f;
			int lines_fit = (int)floorf((h - caption) / lh);
			int max_lines = lines_fit < 3 ? lines_fit : 3;
			if (max_lines < 1)
				max_lines = 1;
			char lines[3][LINE_MAX];
			int nl = wrapLines(f, displayName(e), w - 2 * side, max_lines, lines);
			drawCentredLines(s, f, lines, nl, (int)(lh + 0.5f), C_WHITE, w, 0, h - caption);
		}
	}
	if (lit) {
		int y = h - NX_DPF(TILE_BOTTOM_DP);
		TTF_Font* f_info = nseg > 0 ? UIFont_get(11, false) : NULL; // fetched here: only good until the next get
		if (f_info) {
			y -= TTF_FontHeight(f_info);
			InfoBand_drawSegments(s, segs, nseg, side, false, y, w - 2 * side, f_info);
		}
		if (!title_tile) {
			TTF_Font* f = UIFont_get(13, false);
			char name[LINE_MAX];
			ellipsize(f, displayName(e), name, w - 2 * side);
			if (f && name[0])
				drawText(s, f, name, C_WHITE, side, y - TTF_FontHeight(f), 255);
		}
	}
	tileBorder(s, w, h);
}

// The tool's bundled icon (the one mapping, in tiles.c): by its shown name, else its pak's file name, else the
// unknown-tool icon.
static const char* toolIcon(Entry* e) {
	const char* file = Tiles_toolIcon(displayName(e));
	if (!file) {
		const char* slash = strrchr(e->path, '/');
		file = Tiles_toolIcon(slash ? slash + 1 : e->path);
	}
	return file ? file : TILE_UNKNOWN_TOOL_ICON;
}

static void composeTool(SDL_Surface* s, int w, int h, bool lit, int i) {
	Entry* e = pins[i];
	SDL_Color bg = cardBg(lit);
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, bg.r, bg.g, bg.b, 255));
	const char* file = toolIcon(e);
	SDL_Surface* icon = file ? MenuArt_get(file, (int)(w * 0.72f), (int)(h * 0.72f)) : NULL;
	if (icon) {
		if (lit)
			tintLit(icon);
		SDL_SetSurfaceAlphaMod(icon, lit ? 36 : 41); // the ink (default black) at 14% / white at 16%
		SDL_BlitSurface(icon, NULL, s, &(SDL_Rect){(w - icon->w) / 2, (h - icon->h) / 2});
		SDL_SetSurfaceColorMod(icon, 255, 255, 255);
		SDL_SetSurfaceAlphaMod(icon, 255);
	}
	// SemiBold 15 sp, smaller (to 11) until the longest word fits inside the 12 dp padding, one word per line, as
	// many lines as fit; extra words end the last line in an ellipsis, and a word still too wide at 11 is cut
	// A one-word name too wide at 15 sp first breaks at its lowercase→uppercase boundary ("Retro / Achievements").
	int pad = NX_DPF(12);
	char name[LINE_MAX];
	snprintf(name, sizeof(name), "%s", displayName(e));
	TTF_Font* f = UIFont_get(15, true);
	if (f && textW(f, name) > w - 2 * pad)
		Tiles_camelSplit(name, sizeof(name));
	float sp = Tiles_fitWordsSp(name, 15, 11, true, w - 2 * pad);
	f = UIFont_get(sp, true);
	if (f) {
		int lh = (int)(NX_SP(sp) * 1.2f + 0.5f);
		int max_lines = lh > 0 ? (h - 2 * pad) / lh : 1;
		if (max_lines < 1)
			max_lines = 1;
		if (max_lines > 8)
			max_lines = 8;
		char words[8][LINE_MAX];
		int nw = 0;
		char work[LINE_MAX];
		snprintf(work, sizeof(work), "%s", name);
		char* save = NULL;
		for (char* tok = strtok_r(work, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
			if (nw < max_lines) {
				snprintf(words[nw++], LINE_MAX, "%s", tok);
			} else { // more words than lines: the last line ends in an ellipsis
				char more[LINE_MAX];
				snprintf(more, sizeof(more), "%.*s" ELLIPSIS, LINE_MAX - 8, words[max_lines - 1]);
				snprintf(words[max_lines - 1], LINE_MAX, "%s", more);
				break;
			}
		}
		char lines[8][LINE_MAX];
		int nl = 0;
		for (int k = 0; k < nw; k++) {
			ellipsize(f, words[k], lines[nl], w - 2 * pad);
			if (lines[nl][0])
				nl++;
		}
		drawCentredLines(s, f, lines, nl, lh, cardInk(lit), w, 0, h);
	}
	if (!lit)
		tileBorder(s, w, h);
}

///////////////////////////////////////
// Stats card

// Fonts are kept as sizes (sp, regular) and fetched at each use: a UIFont is only good until the next UIFont_get.
typedef struct {
	float lf;
	char ltext[LINE_MAX];
	SDL_Color lc;
	float rf;
	char rtext[64];
	SDL_Color rc;
	bool list;	  // a Most played line: the title is cut to leave room for the time
	int top, bot; // ink above (+) / below (−) the shared baseline
} StatLine;

static TTF_Font* statFont(float sp) {
	return sp > 0 ? UIFont_get(sp, false) : NULL;
}

static void measureLine(StatLine* l) {
	int lt = 0, lb = 0, rt = 0, rb = 0;
	UIFont_inkBounds(statFont(l->lf), l->ltext, &lt, &lb);
	if (l->rtext[0])
		UIFont_inkBounds(statFont(l->rf), l->rtext, &rt, &rb);
	l->top = lt > rt ? lt : rt;
	l->bot = lb < rb ? lb : rb;
}

typedef struct {
	StatLine header;
	StatLine lines[6]; // Achievements, Total played, Most played, up to 3 games
	int nlines;		   // rows before the list (3) + list lines kept
	int gaps[3];	   // header→Achievements, →Total played, →Most played (px)
	int lv_gap;		   // label to value on the three rows (px): 12 dp, 6 when tight
	int height;		   // ink top of the header to the lowest ink of the last line
} StatsBlock;

static int blockHeight(const StatsBlock* b, int nlist) {
	int h = b->header.top - b->header.bot;
	for (int i = 0; i < 3 + nlist; i++) {
		const StatLine* l = &b->lines[i];
		h += (i < 3 ? b->gaps[i] : NX_DPF(4)) + (l->top - l->bot);
	}
	return h;
}

// narrow: the reference's narrow-card sizes (labels 11 sp, large values 14 sp, grey words 10 sp).
static void buildStatsSized(StatsBlock* b, const HomeStats* st, bool lit, int avail, bool narrow) {
	memset(b, 0, sizeof(*b));
	SDL_Color ink = cardInk(lit);
	SDL_Color dim = cardDim(lit);
	float f_head = 12;
	float f_label = narrow ? 11 : 13;
	float f_value = narrow ? 14 : 22;
	float f_word = narrow ? 10 : 16;
	float f_title = 14;
	float f_time = 12;

	b->header = (StatLine){.lf = f_head, .lc = dim};
	snprintf(b->header.ltext, LINE_MAX, "Monthly Activities");
	measureLine(&b->header);

	// value: a large white number, or a grey word ("…", "None", "Sign in")
	StatLine* l = &b->lines[0];
	*l = (StatLine){.lf = f_label, .lc = dim, .rf = f_value, .rc = ink};
	snprintf(l->ltext, LINE_MAX, "Achievements");
	if (st->ready && st->unlocks == -1) {
		l->rf = f_word;
		l->rc = dim;
		snprintf(l->rtext, sizeof(l->rtext), "Sign in");
	} else if (!st->ready) {
		l->rf = f_word;
		l->rc = dim;
		snprintf(l->rtext, sizeof(l->rtext), ELLIPSIS);
	} else {
		snprintf(l->rtext, sizeof(l->rtext), "%d", st->unlocks);
	}

	l = &b->lines[1];
	*l = (StatLine){.lf = f_label, .lc = dim, .rf = f_value, .rc = ink};
	snprintf(l->ltext, LINE_MAX, "Total played");
	if (!st->ready || st->total <= 0) {
		l->rf = f_word;
		l->rc = dim;
		snprintf(l->rtext, sizeof(l->rtext), "%s", st->ready ? "None" : ELLIPSIS);
	} else {
		GameInfo_durationText(st->total, l->rtext, sizeof(l->rtext));
	}

	l = &b->lines[2];
	*l = (StatLine){.lf = f_label, .lc = dim, .rf = f_word, .rc = dim};
	snprintf(l->ltext, LINE_MAX, "Most played");
	int ntop = st->ready ? st->ntop : 0;
	if (!st->ready || ntop <= 0)
		snprintf(l->rtext, sizeof(l->rtext), "%s", st->ready ? "None" : ELLIPSIS);
	for (int i = 0; i < ntop && i < 3; i++) {
		l = &b->lines[3 + i];
		*l = (StatLine){.lf = f_title, .lc = ink, .rf = f_time, .rc = dim, .list = true};
		snprintf(l->ltext, LINE_MAX, "%s", st->top[i].title);
		GameInfo_durationText(st->top[i].seconds, l->rtext, sizeof(l->rtext));
	}
	for (int i = 0; i < 3 + ntop && i < 6; i++)
		measureLine(&b->lines[i]);

	// as many games as fit at the fixed gaps, at least one; then the gaps shrink together (floor 6)
	b->gaps[0] = NX_DPF(12);
	b->gaps[1] = NX_DPF(12);
	b->gaps[2] = NX_DPF(16);
	int k = ntop;
	while (k > 1 && blockHeight(b, k) > avail)
		k--;
	int over = blockHeight(b, k) - avail;
	if (over > 0) {
		int d = (over + 2) / 3;
		int floor6 = NX_DPF(6);
		for (int g = 0; g < 3; g++)
			b->gaps[g] = b->gaps[g] - d > floor6 ? b->gaps[g] - d : floor6;
	}
	b->nlines = 3 + k;
	b->height = blockHeight(b, k);
	b->lv_gap = NX_DPF(12);
}

// Whether every label and its value fit inner_w with `gap` between them.
static bool rowsFit(const StatsBlock* b, int inner_w, int gap) {
	for (int i = 0; i < 3; i++) {
		const StatLine* l = &b->lines[i];
		int lw = textW(statFont(l->lf), l->ltext);
		int rw = l->rtext[0] ? gap + textW(statFont(l->rf), l->rtext) : 0;
		if (lw + rw > inner_w)
			return false;
	}
	return true;
}

// A label that doesn't fit next to its value: first the gap drops to 6 dp, then the narrow sizes, and only then is
// the label cut (drawStatsFace).
static void buildStats(StatsBlock* b, const HomeStats* st, bool lit, int avail, int inner_w) {
	buildStatsSized(b, st, lit, avail, false);
	if (rowsFit(b, inner_w, b->lv_gap))
		return;
	b->lv_gap = NX_DPF(6);
	if (rowsFit(b, inner_w, b->lv_gap))
		return;
	buildStatsSized(b, st, lit, avail, true);
	b->lv_gap = NX_DPF(6);
}

static void drawStatsFace(SDL_Surface* s, const StatsBlock* b, int w, int top, int alpha) {
	int pad = NX_DPF(CARD_PAD_DP);
	int prev_bottom = top + (b->header.top - b->header.bot);
	for (int i = 0; i < b->nlines; i++) {
		const StatLine* l = &b->lines[i];
		int baseline = prev_bottom + (i < 3 ? b->gaps[i] : NX_DPF(4)) + l->top;
		TTF_Font* rf = statFont(l->rf);
		int rw = textW(rf, l->rtext);
		if (l->rtext[0])
			drawTextBaseline(s, rf, l->rtext, l->rc, w - pad - rw, baseline, alpha);
		char left[LINE_MAX];
		int room = w - 2 * pad - (l->rtext[0] ? rw + (i < 3 ? b->lv_gap : NX_DPF(12)) : 0);
		TTF_Font* lf = statFont(l->lf);
		ellipsize(lf, l->ltext, left, room);
		drawTextBaseline(s, lf, left, l->lc, pad, baseline, alpha);
		prev_bottom = baseline - l->bot;
	}
}

static void drawActivityFace(SDL_Surface* s, const HomeStats* st, bool lit, int w, int top, int alpha) {
	int cell = NX_DPF(cellDp());
	int gap = NX_DPF(4);
	int grid_w = 7 * cell + 6 * gap;
	int grid_h = 5 * cell + 4 * gap;
	int x0 = (w - grid_w) / 2;
	int y0 = top + NX_DPF(15 + 16);
	SDL_Color ink = cardInk(lit);
	int radius = NX_DPF(4);
	for (int i = 0; i < HOME_DAYS; i++) {
		if (st->ready && i > st->today)
			continue; // days after today are blank
		int x = x0 + (i % 7) * (cell + gap);
		int y = y0 + (i / 7) * (cell + gap);
		int shade = HomeStats_shadeAlpha(st->ready ? st->days[i] : 0);
		fillRounded(s, x, y, cell, cell, radius, ink, shade * alpha / 255);
		if (st->ready && i == st->today) {
			int o = 2 * NX_DPF(1); // a 1 dp ring, 1 dp outside the cell
			strokeRounded(s, x - o, y - o, cell + 2 * o, cell + 2 * o, radius + o, NX_DPF(1) > 0 ? NX_DPF(1) : 1, ink,
						  alpha);
		}
	}
	if (st->ready && st->total <= 0) {
		TTF_Font* f = UIFont_get(13, false);
		const char* text = "No play yet";
		if (f)
			drawText(s, f, text, cardDim(lit), (w - textW(f, text)) / 2,
					 y0 + (grid_h - TTF_FontHeight(f)) / 2, alpha);
	}
}

// One face of the card (the flip blends the two cached faces; the header is identical in both, so it never fades).
static void composeCard(SDL_Surface* s, int w, int h, bool lit, bool activity, const HomeStats* st) {
	SDL_Color bg = cardBg(lit);
	SDL_FillRect(s, &(SDL_Rect){0, 0, w, h}, SDL_MapRGBA(s->format, bg.r, bg.g, bg.b, 255));
	int pad = NX_DPF(CARD_PAD_DP);
	StatsBlock b;
	buildStats(&b, st, lit, h - 2 * pad, w - 2 * pad);
	bool flips = canFlip();
	// the activity block: header line 15, 16, then 5 cells and 4 gaps of 4
	int act_h = flips ? NX_DPF(15 + 16) + 5 * NX_DPF(cellDp()) + 4 * NX_DPF(4) : 0;
	int block = b.height > act_h ? b.height : act_h;
	int top = (h - block) / 2;
	if (top < pad)
		top = pad;

	// both faces start under the same header, at the taller block's centred top
	drawTextBaseline(s, statFont(b.header.lf), b.header.ltext, b.header.lc, pad, top + b.header.top, 255);
	if (flips && activity)
		drawActivityFace(s, st, lit, w, top, 255);
	else
		drawStatsFace(s, &b, w, top, 255);
	if (!lit)
		tileBorder(s, w, h);
}

///////////////////////////////////////
// Render

typedef enum { CARD_CONTINUE,
			   CARD_PICK,
			   CARD_STATS,
			   CARD_GAME,
			   CARD_TOOL } CardKind;

static bool sameFocus(HomeFocus a, HomeFocus b) {
	return a.area == b.area && (a.area != HOME_FOCUS_PIN || a.pin == b.pin);
}

// How lit a card is (0..1): the focused card fades in, the previous one out. (While the tab row has focus the whole
// page dims as one layer: contentdim.c.)
static float litAmount(HomeFocus f) {
	float p = tweenProgress(&sel_tw, SEL_MS);
	if (sameFocus(f, focus))
		return p;
	if (sel_tw.active && sameFocus(f, prev_focus))
		return 1.0f - p;
	return 0.0f;
}

///////////////////////////////////////
// Card cache

#define CARD_CACHE_MAX 40

typedef struct {
	bool used;
	CardKind kind;
	int pin, w, h, scale;
	bool lit, activity;
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

// Every visible card in both looks, plus the stats card's second face.
static int cardCacheLimit(void) {
	int n = 2 * (npins + 2) + 2;
	return n < CARD_CACHE_MAX ? n : CARD_CACHE_MAX;
}

static Uint32 fnv(Uint32 h, const void* data, size_t n) {
	const unsigned char* p = data;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static Uint32 fnvStr(Uint32 h, const char* s) {
	return s ? fnv(h, s, strlen(s) + 1) : fnv(h, "", 1);
}

static Uint32 segsStamp(Uint32 h, const InfoSeg* segs, int n) {
	h = fnv(h, &n, sizeof(n));
	for (int i = 0; i < n; i++) {
		h = fnv(h, &segs[i].kind, sizeof(segs[i].kind));
		h = fnvStr(h, segs[i].text);
	}
	return h;
}

// gen = HomeArt_lastGen() of pic's lookup: a re-decoded art (after HomeArt_forget) may reuse the old pointer
static Uint32 artStamp(Uint32 h, HomeArtState st, SDL_Surface* pic, unsigned gen) {
	h = fnv(h, &st, sizeof(st));
	h = fnv(h, &gen, sizeof(gen));
	return fnv(h, &pic, sizeof(pic));
}

static Uint32 statsStamp(Uint32 h, const HomeStats* st) {
	h = fnv(h, &st->ready, sizeof(st->ready));
	if (!st->ready)
		return h;
	h = fnv(h, &st->today, sizeof(st->today));
	h = fnv(h, st->days, sizeof(st->days));
	h = fnv(h, &st->total, sizeof(st->total));
	h = fnv(h, &st->unlocks, sizeof(st->unlocks));
	h = fnv(h, &st->ntop, sizeof(st->ntop));
	for (int i = 0; i < st->ntop && i < 3; i++) {
		h = fnv(h, &st->top[i].seconds, sizeof(st->top[i].seconds));
		h = fnvStr(h, st->top[i].title);
	}
	return h;
}

// A stamp of the data a card's composition reads (the same lookups compose does, all cached and cheap). The cache
// outlives rebuilds (and so Home visits), so this must cover every input compose reads beyond the slot's key (kind, pin
// index, w, h, FIXED_SCALE, lit, face).
static Uint32 cardStamp(CardKind kind, int pin, int w, int h, bool lit, const HomeStats* st) {
	Uint32 hs = 2166136261u;
	InfoSeg segs[3];
	SDL_Surface* pic = NULL;
	if (lit) { // the lit look's ground and ink (the theme's accent); plain cards use fixed colours
		SDL_Color bg = cardBg(true), ink = cardInk(true);
		hs = fnv(hs, &bg, sizeof(bg));
		hs = fnv(hs, &ink, sizeof(ink));
	}
	switch (kind) {
	case CARD_CONTINUE: {
		HomeArtState as = HomeArt_continue(cont->path, cont_preview[0] ? cont_preview : NULL, w, h, 0, &pic);
		hs = artStamp(hs, as, pic, HomeArt_lastGen()); // right after the lookup it describes
		hs = fnvStr(hs, cont->path);
		hs = fnvStr(hs, cont_preview);
		hs = fnvStr(hs, displayName(cont));
		hs = segsStamp(hs, segs, infoSegs(cont->path, false, true, true, segs));
		hs = segsStamp(hs, segs, infoSegs(cont->path, true, false, false, segs));
		break;
	}
	case CARD_PICK:
		break;
	case CARD_STATS:
		hs = statsStamp(hs, st);
		// canFlip and cellDp read the layout (dp), which the pixel size alone doesn't pin down
		hs = fnv(hs, &layout.mode, sizeof(layout.mode));
		hs = fnv(hs, &layout.card.w, sizeof(layout.card.w));
		hs = fnv(hs, &layout.card.h, sizeof(layout.card.h));
		break;
	case CARD_GAME: {
		Entry* e = pins[pin];
		HomeArtState as = HomeArt_pin(e->path, w, h, 0, &pic);
		hs = artStamp(hs, as, pic, HomeArt_lastGen()); // right after the lookup it describes
		hs = fnvStr(hs, e->path);
		hs = fnvStr(hs, displayName(e));
		if (lit)
			hs = segsStamp(hs, segs, infoSegs(e->path, true, true, false, segs));
		break;
	}
	case CARD_TOOL:
		hs = fnvStr(hs, pins[pin]->path);
		hs = fnvStr(hs, displayName(pins[pin]));
		break;
	}
	return hs;
}

static void composeCardKind(SDL_Surface* s, CardKind kind, int w, int h, bool lit, bool activity, int pin,
							const HomeStats* st) {
	SDL_SetClipRect(s, &(SDL_Rect){0, 0, w, h});
	switch (kind) {
	case CARD_CONTINUE:
		composeContinue(s, w, h);
		break;
	case CARD_PICK:
		composePick(s, w, h, lit);
		break;
	case CARD_STATS:
		composeCard(s, w, h, lit, activity, st);
		break;
	case CARD_GAME:
		composeGame(s, w, h, lit, pin);
		break;
	case CARD_TOOL:
		composeTool(s, w, h, lit, pin);
		break;
	}
	maskCorners(s, w, h, NX_DPF(RADIUS_DP));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
}

// The card's cached surface for this look, recomposed only when its key changed.
static SDL_Surface* cachedCard(CardKind kind, int pin, int w, int h, bool lit, bool activity, const HomeStats* st) {
	if (kind == CARD_CONTINUE)
		lit = false; // Continue lights with its ring only
	if (kind != CARD_STATS)
		activity = false;
	if (kind != CARD_GAME && kind != CARD_TOOL)
		pin = -1;
	Uint32 stamp = cardStamp(kind, pin, w, h, lit, st);
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
		if (c->kind == kind && c->pin == pin && c->w == w && c->h == h && c->scale == FIXED_SCALE && c->lit == lit &&
			c->activity == activity) {
			if (c->stamp != stamp) { // same card, new content: recompose in place
				c->stamp = stamp;
				composeCardKind(c->surf, kind, w, h, lit, activity, pin, st);
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
	*victim = (CardSlot){.used = true, .kind = kind, .pin = pin, .w = w, .h = h, .scale = FIXED_SCALE, .lit = lit, .activity = activity, .stamp = stamp, .lru = ++card_lru, .surf = surf};
	composeCardKind(surf, kind, w, h, lit, activity, pin, st);
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
	int rad = clampRadius(NX_DPF(RADIUS_DP), w, h);
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

// One look of a card at alpha; the stats card mid-flip blends its incoming face over the outgoing one.
static void drawLook(SDL_Surface* dst, CardKind kind, SDL_Rect r, int pin, bool lit, int alpha, const HomeStats* st) {
	if (kind == CARD_STATS && canFlip()) {
		float p = tweenProgress(&flip_tw, FLIP_MS);
		if (p < 1.0f)
			blitCard(dst, cachedCard(kind, pin, r.w, r.h, lit, !face_activity, st), r, alpha);
		blitCard(dst, cachedCard(kind, pin, r.w, r.h, lit, face_activity, st), r, (int)(alpha * p + 0.5f));
		return;
	}
	blitCard(dst, cachedCard(kind, pin, r.w, r.h, lit, false, st), r, alpha);
}

// dst's clip rect is the page band: a card wholly outside it costs nothing.
static void drawCard(SDL_Surface* dst, CardKind kind, SDL_Rect r, HomeFocus which, int pin, const HomeStats* st) {
	int ring = NX_DPF(RING_DP);
	const SDL_Rect* band = &dst->clip_rect;
	if (r.w <= 0 || r.h <= 0 || r.y + r.h + ring <= band->y || r.y - ring >= band->y + band->h)
		return; // outside the band
	float lit = litAmount(which);
	bool ringed = kind == CARD_CONTINUE || kind == CARD_GAME;
	bool lit_differs = kind != CARD_CONTINUE; // Continue lights with its ring only
	if (ringed && lit > 0.0f)
		strokeRounded(dst, r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring, NX_DPF(RADIUS_DP) + ring, ring,
					  cardBg(true), (int)(lit * 255 + 0.5f));
	bool flipping = kind == CARD_STATS && canFlip() && tweenProgress(&flip_tw, FLIP_MS) < 1.0f;
	if (lit > 0.0f && lit < 1.0f && lit_differs && !flipping) { // a plain crossfade: both looks in one pass
		bool face = kind == CARD_STATS && canFlip() && face_activity;
		blitCardOver(dst, cachedCard(kind, pin, r.w, r.h, true, face, st),
					 cachedCard(kind, pin, r.w, r.h, false, face, st), r, (int)(lit * 255 + 0.5f));
		return;
	}
	if (lit < 1.0f || !lit_differs)
		drawLook(dst, kind, r, pin, false, 255, st);
	if (lit > 0.0f && lit_differs)
		drawLook(dst, kind, r, pin, true, (int)(lit * 255 + 0.5f), st);
}

static void renderHints(SDL_Surface* dst) {
	char* pairs[10] = {NULL};
	int p = 0;
	pairs[p++] = "SELECT";
	pairs[p++] = "RECENT";
	Entry* e = Home_focusedEntry();
	if (MenuTabs_focused()) { // the tab row has focus: exactly SELECT RECENT and A OPEN (A returns to Home)
		pairs[p++] = "A";
		pairs[p++] = "OPEN";
	} else if (focus.area == HOME_FOCUS_CARD) {
		if (canFlip()) {
			pairs[p++] = "A";
			pairs[p++] = face_activity ? "STATS" : "ACTIVITY";
		}
	} else if (!e) { // Pick a game: A opens the Consoles tab, when there is one
		if (MenuTabs_isVisible(MENU_TAB_CONSOLES)) {
			pairs[p++] = "A";
			pairs[p++] = "OPEN";
		}
	} else {
		pairs[p++] = "MENU";
		pairs[p++] = "OPTIONS";
		bool opens = e->type == ENTRY_PAK || (focus.area == HOME_FOCUS_PIN && pin_plain_dir[focus.pin]);
		pairs[p++] = "A";
		pairs[p++] = opens ? "OPEN" : focus_can_resume ? "RESUME"
													   : "PLAY";
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
		// the screen under the band is still clear here (nextui.c drew only the bars), so cards go straight on
		SDL_SetClipRect(dst, &(SDL_Rect){0, bar_h, dst->w, body_h});
		int scroll_px = NX_DPF(currentScroll());
		HomeStats st;
		memset(&st, 0, sizeof(st));
		if (!HomeStats_get(&st))
			st.ready = false;

		// the page as one layer for the tab-focus dim (contentdim.h); the top band's fade and the hints stay lit
		ContentDim_begin(dst, (SDL_Rect){0, bar_h, dst->w, body_h});
		HomeFocus top_focus = {HOME_FOCUS_CONTINUE, 0};
		drawCard(dst, cont ? CARD_CONTINUE : CARD_PICK, toScreen(layout.cont, scroll_px), top_focus, 0, &st);
		if (layout.mode != HOME_MODE_FRESH)
			drawCard(dst, CARD_STATS, toScreen(layout.card, scroll_px), (HomeFocus){HOME_FOCUS_CARD, 0}, 0, &st);
		for (int i = 0; i < layout.npins; i++)
			drawCard(dst, isGamePin(i) ? CARD_GAME : CARD_TOOL, toScreen(layout.pins[i].r, scroll_px),
					 (HomeFocus){HOME_FOCUS_PIN, i}, i, &st);
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
		scroll_tw.active = sel_tw.active = flip_tw.active = false;
		scroll_from = scroll_to;
		return false;
	}
	bool a = tweenTick(&scroll_tw, SCROLL_MS);
	bool b = tweenTick(&sel_tw, SEL_MS);
	bool c = tweenTick(&flip_tw, FLIP_MS);
	if (a && !scroll_tw.active)
		scroll_from = scroll_to;
	return a || b || c;
}

bool Home_scrolled(void) {
	return Home_active() && NX_DPF(currentScroll()) > 0;
}

///////////////////////////////////////
// Input

static void setFocus(HomeFocus f) {
	prev_focus = focus;
	focus = f;
	tweenStart(&sel_tw);
	float dp = pxPerDp();
	float target = HomeLayout_scrollFor(&layout, focus, screen->h / dp, barPx() / dp, scroll_to);
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
	switch (focus.area) {
	case HOME_FOCUS_CONTINUE:
		if (cont)
			launchGame(cont);
		else
			GameList_openTab(MENU_TAB_CONSOLES, dirty); // Pick a game
		break;
	case HOME_FOCUS_CARD:
		if (canFlip()) {
			face_activity = !face_activity;
			tweenStart(&flip_tw);
		}
		break;
	case HOME_FOCUS_PIN: {
		if (focus.pin < 0 || focus.pin >= npins)
			break;
		Entry* e = pins[focus.pin];
		if (e->type == ENTRY_PAK) {
			if (GameList_settingsPinAllows(e)) { // simple mode's Settings PIN
				MenuTabs_markHomeLaunch();
				Entry_open(e);
			}
		} else if (pin_plain_dir[focus.pin]) {
			Entry_open(e); // a legacy pinned folder opens as a list
		} else {
			launchGame(e);
		}
		break;
	}
	}
}

void Home_focusBottom(void) {
	if (!screen || !Home_active())
		return;
	ensureBuilt();
	HomeFocus f = HomeLayout_bottomFrom(&layout, focus, &mem);
	if (!sameFocus(f, focus))
		setFocus(f);
}

bool Home_handleInput(unsigned long now, bool* dirty) {
	if (!screen || !Home_active())
		return false;
	ensureBuilt();

	if (PAD_tappedMenu(now)) {
		Entry* e = Home_focusedEntry();
		if (e) {
			GameList_openContextMenuFor(e, focus.area == HOME_FOCUS_PIN, focus.area == HOME_FOCUS_CONTINUE);
			*dirty = true;
		}
		return true; // nothing for the stats card or Pick a game
	}

	static const struct {
		int btn;
		HomeDir dir;
	} dpad[] = {{BTN_UP, HOME_DIR_UP}, {BTN_DOWN, HOME_DIR_DOWN}, {BTN_LEFT, HOME_DIR_LEFT}, {BTN_RIGHT, HOME_DIR_RIGHT}};
	for (size_t i = 0; i < sizeof(dpad) / sizeof(dpad[0]); i++) {
		if (!PAD_justRepeated(dpad[i].btn))
			continue;
		if (dpad[i].dir == HOME_DIR_UP && focus.area != HOME_FOCUS_PIN) {
			// the top row (Continue, the stats card, Pick a game): a fresh UP focuses the tab row; a held one stops
			if (PAD_justPressed(BTN_UP)) {
				MenuTabs_setFocused(true);
				*dirty = true;
			}
			return true;
		}
		HomeFocus f = focus;
		HomeMoveResult res = HomeLayout_move(&layout, &f, &mem, dpad[i].dir);
		if (res == HOME_MOVE_MOVED && !sameFocus(f, focus)) {
			setFocus(f);
			*dirty = true;
		} else if ((res == HOME_MOVE_EDGE_PREV || res == HOME_MOVE_EDGE_NEXT) && PAD_justPressed(dpad[i].btn)) {
			// a fresh press only: a held key stops at the edge
			GameList_switchTab(res == HOME_MOVE_EDGE_PREV ? -1 : 1, dirty);
		}
		return true;
	}

	if (PAD_justPressed(BTN_A)) {
		activate(dirty);
		return true;
	}
	return false;
}
