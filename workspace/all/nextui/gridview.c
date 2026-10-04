// The Grid style (§8): render and D-pad input over the current Directory `top`. Geometry and moves come from
// grid_layout.c (dp, host-tested), the tile look from tiles.c. Everything else (A, B, X, Y, L1/R1, MENU, SELECT,
// START, the F keys) stays on GameList_handleInput's own paths, so a Grid screen opens, resumes, netplays and
// shows the same context menu as a List one.
//
// Drawing (the Brick's A133 draws the UI in software, where SDL's per-pixel-alpha blit costs ~20 ns a pixel): each
// tile is composed once per look (plain, lit) into its own opaque surface, on the Grid's plain black ground with the
// ring's room around it, and cached under its entry's path, size and scale plus a stamp of what it shows. A frame
// copies the visible tiles straight onto the screen (clipped to the body), or lerps lit over plain in one pass for
// the 120 ms crossfade (UI_blitOpaque), and fills only the black between them; the cut-column shade and the edge
// fades are one darkening pass (UI_darkenColumns) that leaves the selected tile out.

#include "gridview.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_ease.h"
#include "ui_accent.h"
#include "ui_message.h"
#include "utils.h"

#include "collcount.h"
#include "content.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gamelist.h"
#include "grid_layout.h"
#include "home.h"
#include "homeart.h"
#include "imgloader.h" // screen
#include "launcher.h"
#include "list_window.h"
#include "menulogo.h"
#include "menutabs.h"
#include "rowview_shared.h" // the tile kinds, and view_common.h
#include "tiles.h"
#include "types.h"
#include "ui_fade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLIDE_MS 260
#define LIT_MS 120
#define CUT_SHADE_ALPHA 179		  // black at 70% over a column cut by a screen edge
#define EDGE_FADE_SHARE 0.20f	  // the edge fade's width, of the screen's
#define PREFETCH_TWEEN_SHARE 0.6f // a single move's slide and crossfade this far through let prefetch run
#define GRID_BIG_UI_SHRINK 0.85f  // the main menu's tiles at the Large UI scale (computeLayout)

// the slide: the content offset (dp) eases from `off_from` to `off_to`
static float off_from = 0, off_to = 0;
static Tween slide_tw;
// the lit crossfade: `lit_sel` fades in, `lit_prev` fades out
static int lit_sel = -1, lit_prev = -1;
static Tween lit_tw;
// the slide or the crossfade began while the last one ran (a held D-pad's repeats): prefetch builds for the target
static bool slide_retargeted = false, lit_retargeted = false;
// what the state above belongs to: a change of any snaps (no slide in, no crossfade from a stale index)
static unsigned seen_top = 0; // the Directory serial (0 = none)
static unsigned seen_gen = 0;
static int seen_n = -1;
static int seen_screen_w = 0, seen_scale = 0;


///////////////////////////////////////
// Timing

static float currentOffset(void) {
	if (!slide_tw.active)
		return off_to;
	return off_from + (off_to - off_from) * UI_easeStandard(tweenProgress(&slide_tw, SLIDE_MS));
}

static float litAmount(int index) {
	float p = tweenProgress(&lit_tw, LIT_MS);
	if (index == lit_sel)
		return p;
	if (index == lit_prev && lit_tw.active)
		return 1.0f - p;
	return 0.0f;
}

///////////////////////////////////////
// Geometry

static void computeLayout(SDL_Surface* screen, int n, GridLayout* g) {
	float pd = pxPerDp();
	float sw = screen->w / pd, sh = screen->h / pd;
	// wider tiles than the spec shape: the Consoles and Collections tabs twice as wide (wide logos 4-8:1, long names),
	// game lists 1.5x (their screenshots), the Tools tab as specced
	bool root = stack->count == 1;
	MenuTabId tab = MenuTabs_current();
	float mul = !root ? 1.5f : (tab == MENU_TAB_TOOLS ? 1.0f : 2.0f);
	// at the Large UI scale (3x, the Brick's default too): the main menu's tiles (and their logos, icons and names) at
	// GRID_BIG_UI_SHRINK, else they take most of the smaller body
	float size = root && FIXED_SCALE >= 3 ? GRID_BIG_UI_SHRINK : 1.0f;
	// the gap between tiles stays the default scale's px whatever the UI scale
	GridLayout_computeSized(sw, BAR_DP, sh - 2 * BAR_DP, n, mul, (float)NATIVE_SCALE / (float)FIXED_SCALE, size, g);
}

///////////////////////////////////////
// Tiles (their kinds: rowview.c's per-list cache, RowView_syncKinds / RowView_kindFor)

// The count-only info (time, "n of m") of a game tile. Requests it when it isn't ready (latest request wins: call
// it for the tile that matters most last).
static int gameInfo(Entry* e, InfoSeg segs[3]) {
	GameInfo info;
	if (!GameInfo_get(e->path, &info) || !(info.has_time || info.has_ra))
		return 0;
	return GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0, info.has_time ? info.seconds : -1,
							 info.has_ra ? info.unlocked : 0, info.has_ra ? info.total : 0,
							 info.has_ra ? info.next : NULL, false, segs);
}

// "N games" for the main menu's Consoles or Collections tile i ("" while unknown, and on other kinds or lists). A
// collection's count is requested when it isn't known (latest request wins: call it for the selection last).
static void tileCount(int i, char* out, size_t size) {
	out[0] = '\0';
	if (stack->count != 1 || i < 0 || i >= top->entries->count)
		return;
	Entry* e = top->entries->items[i];
	TileKind k = RowView_kindFor(i, e);
	if (k == TILE_LOGO)
		GameInfo_gamesLabel(Content_consoleGameCount(e), out, size);
	else if (k == TILE_COLLECTION)
		GameInfo_gamesLabel(CollCount_get(e->path), out, size);
}

// A console or collection tile's count for its plain look (NULL for other kinds). *asked is set when a collection
// queued a count, so the caller can re-ask for the selection last (CollCount's queue keeps the latest request).
static const char* plainCount(int i, char* out, size_t size, bool* asked) {
	if (stack->count != 1 || i < 0 || i >= top->entries->count)
		return NULL;
	Entry* e = top->entries->items[i];
	TileKind k = RowView_kindFor(i, e);
	if (k == TILE_LOGO) {
		GameInfo_gamesLabel(Content_consoleGameCount(e), out, size);
		return out;
	}
	if (k != TILE_COLLECTION)
		return NULL;
	int n = CollCount_get(e->path);
	if (n < 0)
		*asked = true;
	GameInfo_gamesLabel(n, out, size);
	return out;
}

///////////////////////////////////////
// The tile cache

#define TILE_CACHE_MAX 28 // a sliding screen's columns (+1 each side) in both rows, and the lit looks around the selection
#define TILE_RING_DP 3.0f // tiles.c's game ring, outside the tile

typedef struct {
	bool used, lit;
	int w, h, scale; // the tile (px, without the ring room) and FIXED_SCALE
	char path[MAX_PATH];
	Uint32 stamp;	// what the tile shows (tileStamp)
	char count[32]; // the lit look's "N games" when it was composed ("" none or unknown)
	unsigned lru;
	SDL_Surface* surf; // (w + 2 room) × (h + 2 room), opaque
} TileSlot;

static TileSlot tile_cache[TILE_CACHE_MAX];
static unsigned tile_lru = 0;

// Prefetch (GridView_prefetchStep) works for the last frame's grid: its layout and selection, armed by each render and
// disarmed once everything ahead is composed (or the grid stops drawing). It runs between frames, never forcing one.
static struct {
	bool armed;
	GridLayout g;
	int n, sel;
} pf;

// The ring's room around a tile: the 3 dp ring and its anti-aliased edge.
static int ringRoom(void) {
	return NX_DPF(TILE_RING_DP) + 1;
}

// Everything Tiles_draw reads for a cached look (the lit caption, with the info, is drawn per frame instead). The lit
// look also shows the accent (the ring, or the Logo look's outline and content) and the count.
static Uint32 tileStamp(const TileSpec* t, bool lit) {
	Uint32 h = 2166136261u;
	h = View_fnv(h, &t->kind, sizeof(t->kind));
	h = View_fnv(h, &t->scale, sizeof(t->scale));
	h = View_fnvStr(h, t->name);
	h = View_fnvStr(h, t->logo_file);
	h = View_fnvStr(h, t->icon_file);
	h = View_fnv(h, &t->picture, sizeof(t->picture));
	h = View_fnv(h, &t->picture_gen, sizeof(t->picture_gen)); // a re-decoded art may reuse the old pointer
	if (lit) {
		SDL_Color ac = UI_accent();
		Uint8 rgb[3] = {ac.r, ac.g, ac.b};
		h = View_fnv(h, rgb, sizeof(rgb));
	}
	if (lit || t->kind == TILE_COLLECTION || t->kind == TILE_LOGO) // consoles and collections: on the plain look too
		h = View_fnvStr(h, t->count && t->count[0] ? t->count : "");
	return h;
}

static void composeTile(SDL_Surface* s, int w, int h, const TileSpec* t, bool lit) {
	int room = ringRoom();
	SDL_SetClipRect(s, NULL);
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 255)); // the Grid's black ground: the whole surface opaque
	TileSpec look = *t;
	look.no_caption = true;
	look.info = NULL;
	look.ninfo = 0;
	if (!lit && t->kind != TILE_COLLECTION && t->kind != TILE_LOGO)
		look.count = NULL; // a console's and a collection's count is on both looks
	Tiles_draw(s, (SDL_Rect){room, room, w, h}, &look, lit ? 1.0f : 0.0f);
}

// The tile's cached look (path + lit + size + scale), recomposed in place when what it shows changed. NULL when out
// of memory.
static SDL_Surface* cachedTile(const char* path, int w, int h, const TileSpec* t, bool lit) {
	Uint32 stamp = tileStamp(t, lit);
	int room = ringRoom();
	TileSlot* victim = NULL;
	for (int i = 0; i < TILE_CACHE_MAX; i++) {
		TileSlot* c = &tile_cache[i];
		if (!c->used) {
			if (!victim || victim->used)
				victim = c;
			continue;
		}
		if (c->lit == lit && c->w == w && c->h == h && c->scale == (int)FIXED_SCALE && strcmp(c->path, path) == 0) {
			if (c->stamp != stamp) {
				c->stamp = stamp;
				snprintf(c->count, sizeof(c->count), "%s", lit && t->count ? t->count : "");
				composeTile(c->surf, w, h, t, lit);
			}
			c->lru = ++tile_lru;
			return c->surf;
		}
		if (!victim || (victim->used && c->lru < victim->lru))
			victim = c;
	}
	if (!victim)
		return NULL;
	int sw = w + 2 * room, sh = h + 2 * room;
	if (victim->surf && (victim->surf->w != sw || victim->surf->h != sh)) {
		SDL_FreeSurface(victim->surf);
		victim->surf = NULL;
	}
	if (!victim->surf)
		victim->surf = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!victim->surf) {
		victim->used = false;
		return NULL;
	}
	SDL_Surface* surf = victim->surf;
	victim->used = true;
	victim->lit = lit;
	victim->w = w;
	victim->h = h;
	victim->scale = (int)FIXED_SCALE;
	snprintf(victim->path, sizeof(victim->path), "%s", path);
	victim->stamp = stamp;
	snprintf(victim->count, sizeof(victim->count), "%s", lit && t->count ? t->count : "");
	victim->lru = ++tile_lru;
	composeTile(surf, w, h, t, lit);
	return surf;
}

// The count a cached lit look of path at w×h already shows, or NULL (none cached, or composed without a known count).
static const char* litCount(const char* path, int w, int h) {
	for (int i = 0; i < TILE_CACHE_MAX; i++) {
		const TileSlot* c = &tile_cache[i];
		if (c->used && c->lit && c->w == w && c->h == h && c->scale == (int)FIXED_SCALE && c->count[0] &&
			strcmp(c->path, path) == 0)
			return c->count;
	}
	return NULL;
}

static void tileCacheClear(void) {
	for (int i = 0; i < TILE_CACHE_MAX; i++) {
		if (tile_cache[i].surf)
			SDL_FreeSurface(tile_cache[i].surf);
	}
	memset(tile_cache, 0, sizeof(tile_cache));
	tile_lru = 0;
}

static void fillBlack(SDL_Surface* screen, int x, int y, int w, int h) {
	if (w > 0 && h > 0)
		SDL_FillRect(screen, &(SDL_Rect){x, y, w, h}, SDL_MapRGBA(screen->format, 0, 0, 0, 255));
}

// Black at the per-column alpha over runs [x0, x1) of the rows [y0, y1): the left half and the right half apart, so
// the clear middle costs nothing.
static void darkenRun(SDL_Surface* screen, int w, int x0, int x1, int y0, int y1, const Uint8* col_a) {
	if (x0 < 0)
		x0 = 0;
	if (x1 > w)
		x1 = w;
	if (y1 <= y0)
		return;
	int mid = w / 2;
	if (x0 < mid && x1 > x0)
		UI_darkenColumns(screen, x0, y0, (x1 < mid ? x1 : mid) - x0, y1 - y0, col_a + x0);
	if (x1 > mid && x1 > x0) {
		int a = x0 > mid ? x0 : mid;
		UI_darkenColumns(screen, a, y0, x1 - a, y1 - y0, col_a + a);
	}
}

// The edge darkening over the body (y, h), all but the rect `keep`: the bands above and below it full width, and
// its rows left and right of it.
static void darkenExcept(SDL_Surface* screen, int w, int y, int h, const Uint8* col_a, const SDL_Rect* keep) {
	int ky0 = keep->y < y ? y : keep->y;
	int ky1 = keep->y + keep->h > y + h ? y + h : keep->y + keep->h;
	if (ky1 <= ky0 || keep->x >= w || keep->x + keep->w <= 0) {
		darkenRun(screen, w, 0, w, y, y + h, col_a);
		return;
	}
	darkenRun(screen, w, 0, w, y, ky0, col_a);
	darkenRun(screen, w, 0, keep->x, ky0, ky1, col_a);
	darkenRun(screen, w, keep->x + keep->w, w, ky0, ky1, col_a);
	darkenRun(screen, w, 0, w, ky1, y + h, col_a);
}

// One tile at its px rect r: a copy of the cached look, or for a crossfade lit over plain in one pass, then the lit
// caption over it. Returns whether it painted its whole rect with the ring room (opaque); off screen it paints
// nothing.
static bool drawTile(SDL_Surface* screen, const char* path, SDL_Rect r, const TileSpec* t, float lit) {
	int room = ringRoom();
	int x = r.x - room, y = r.y - room, sw = r.w + 2 * room, sh = r.h + 2 * room;
	const SDL_Rect* clip = &screen->clip_rect;
	if (x >= clip->x + clip->w || x + sw <= clip->x || y >= clip->y + clip->h || y + sh <= clip->y)
		return false;
	SDL_Surface* plain = lit < 1.0f ? cachedTile(path, r.w, r.h, t, false) : NULL;
	SDL_Surface* lit_s = lit > 0.0f ? cachedTile(path, r.w, r.h, t, true) : NULL;
	if ((lit < 1.0f && !plain) || (lit > 0.0f && !lit_s)) {
		// no memory for the cache: the slow path, on its own black
		fillBlack(screen, x, y, sw, sh);
		Tiles_draw(screen, r, t, lit);
		return true;
	}
	if (!lit_s)
		UI_blitOpaque(plain, NULL, 0, 0, sw, sh, screen, x, y, 255);
	else if (!plain)
		UI_blitOpaque(lit_s, NULL, 0, 0, sw, sh, screen, x, y, 255);
	else
		UI_blitOpaque(lit_s, plain, 0, 0, sw, sh, screen, x, y, (int)(lit * 255.0f + 0.5f));
	if (lit > 0.0f)
		Tiles_drawCaption(screen, r, t, lit);
	return true;
}

// What tile i shows (its art requested at tw×th). logo: the caller's buffer for the logo file name.
static void tileSpec(int i, int tw, int th, float scale, TileSpec* t, char logo[64]) {
	Entry* e = top->entries->items[i];
	TileKind kind = RowView_kindFor(i, e);
	*t = (TileSpec){.kind = kind, .name = View_displayName(e), .scale = scale};
	if (kind == TILE_LOGO) {
		const char* slash = strrchr(e->path, '/');
		const char* id = MenuLogo_idForFolder(slash ? slash + 1 : e->path);
		if (id) {
			snprintf(logo, 64, "menu_logo_%s.png", id);
			t->logo_file = logo;
		}
	} else if (kind == TILE_TOOL) {
		t->icon_file = Tiles_toolIcon(t->name);
		if (!t->icon_file) {
			const char* slash = strrchr(e->path, '/');
			t->icon_file = Tiles_toolIcon(slash ? slash + 1 : e->path);
		}
		if (!t->icon_file)
			t->icon_file = TILE_UNKNOWN_TOOL_ICON;
	} else if (kind == TILE_GAME) {
		SDL_Surface* pic = NULL;
		HomeArtState st = HomeArt_pin(e->path, tw, th, 0, &pic); // valid until the next HomeArt_* call
		unsigned gen = HomeArt_lastGen();						 // read at once: changes after HomeArt_forget (Fetch artwork)
		if (st == HOMEART_READY && pic) {
			t->picture = pic; // its screenshot, or its abstract picture (HomeArt_pin's fallback)
			t->picture_gen = gen;
		} else {
			t->kind = TILE_TITLE; // none at all (HomeArt couldn't make one): a title tile
		}
		if (st == HOMEART_LOADING)
			t->name = NULL; // still loading: the black base only, no title flashing in and out
	}
}

///////////////////////////////////////

bool GridView_active(void) {
	return GameList_currentStyle() == MENU_STYLE_GRID && !Home_active();
}

// A list, tab, screen size or scale change: start over with the grid snapped to the selection.
static bool syncList(SDL_Surface* screen, int n, int lastScreen) {
	// the kinds are shared with the Carousel and Backdrop, which may have keyed them to another list since: re-key them
	// on every frame (a no-op while they're this list's), not only when the Grid's own key changes
	RowView_syncKinds(n);
	bool changed = top->serial != seen_top || MenuTabs_generation() != seen_gen || n != seen_n ||
				   screen->w != seen_screen_w || (int)FIXED_SCALE != seen_scale || lastScreen != SCREEN_GAMELIST;
	if (!changed)
		return false;
	seen_top = top->serial;
	seen_gen = MenuTabs_generation();
	seen_n = n;
	seen_screen_w = screen->w;
	seen_scale = (int)FIXED_SCALE;
	return true;
}

void GridView_render(SDL_Surface* screen, int lastScreen) {
	pf.armed = false;
	if (!screen || !top)
		return;
	int bar = barPx();
	int body_h = screen->h - 2 * bar;
	// plain black under the tiles (and under the hint bar's scrim): no backdrop on a Grid screen. The tiles are opaque
	// (ring room included), so only what's between them is filled, below.
	int n = top->entries->count;
	bool snap = syncList(screen, n, lastScreen);
	if (n <= 0 || body_h <= 0) {
		fillBlack(screen, 0, bar, screen->w, screen->h - bar);
		lit_sel = lit_prev = -1;
		lit_tw.active = slide_tw.active = false;
		off_from = off_to = 0;
		UI_renderCenteredMessage(screen, "Empty folder");
		return;
	}

	GridLayout g;
	computeLayout(screen, n, &g);
	int sel = View_selectedIndex(n);
	int sel_col, sel_row;
	GridLayout_cell(&g, sel, &sel_col, &sel_row);

	// the slide toward the selected column's anchored offset
	float target = GridLayout_offsetFor(&g, sel_col);
	if (snap || !animationsOn()) {
		off_from = off_to = target;
		slide_tw.active = false;
	} else if (target != off_to) {
		slide_retargeted = slide_tw.active && tweenProgress(&slide_tw, SLIDE_MS) < 1.0f;
		off_from = currentOffset();
		off_to = target;
		tweenStart(&slide_tw);
	}
	// the lit crossfade from the previous selection
	if (snap || lit_sel < 0) {
		lit_sel = sel;
		lit_prev = -1;
		lit_tw.active = false;
	} else if (sel != lit_sel) {
		lit_retargeted = lit_tw.active && tweenProgress(&lit_tw, LIT_MS) < 1.0f;
		lit_prev = lit_sel;
		lit_sel = sel;
		tweenStart(&lit_tw);
	}
	float off = currentOffset();

	int tw = NX_DPF(g.tile_w), th = NX_DPF(g.tile_h);
	int row_y[2] = {NX_DPF(g.rows_top), NX_DPF(g.rows_top + g.tile_h + g.gap)};
	float scale = GridLayout_tileK(&g);

	// the lit tiles' info first, the selection's last: GameInfo's queue keeps the latest request
	InfoSeg prev_segs[3], sel_segs[3];
	int prev_n = 0, sel_n = 0;
	if (lit_tw.active && lit_prev >= 0 && lit_prev < n) {
		Entry* e = top->entries->items[lit_prev];
		if (RowView_kindFor(lit_prev, e) == TILE_GAME)
			prev_n = gameInfo(e, prev_segs);
	}
	{
		Entry* e = top->entries->items[sel];
		if (RowView_kindFor(sel, e) == TILE_GAME)
			sel_n = gameInfo(e, sel_segs);
	}
	// the main menu's "N games" on the lit tiles, the selection's last (CollCount's queue keeps the latest request)
	char prev_count[32] = "", sel_count[32];
	if (lit_tw.active && lit_prev >= 0 && lit_prev < n)
		tileCount(lit_prev, prev_count, sizeof(prev_count));
	tileCount(sel, sel_count, sizeof(sel_count));

	fillBlack(screen, 0, bar + body_h, screen->w, screen->h - bar - body_h); // under the hint bar's scrim
	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &(SDL_Rect){0, bar, screen->w, body_h});

	int first, last;
	GridLayout_visibleColumns(&g, off, &first, &last);
	int room = ringRoom();
	bool asked = false;		// a plain collection tile queued its count: the selection asks again, last
	int cursor[2] = {0, 0}; // per row: where the black fill between the painted tiles resumes
	for (int col = first; col <= last; col++) {
		int x = NX_DPF(GridLayout_columnX(&g, col, off));
		for (int row = 0; row < 2; row++) {
			int i = GridLayout_index(&g, col, row);
			if (i < 0)
				continue;
			char logo[64];
			TileSpec t;
			tileSpec(i, tw, th, scale, &t, logo);
			if (i == sel) {
				t.info = sel_segs;
				t.ninfo = sel_n;
				t.count = sel_count;
			} else if (i == lit_prev) {
				t.info = prev_segs;
				t.ninfo = prev_n;
				t.count = prev_count;
			}
			char plain[32];
			if (i != sel && i != lit_prev)
				t.count = plainCount(i, plain, sizeof(plain), &asked);
			if (drawTile(screen, ((Entry*)top->entries->items[i])->path, (SDL_Rect){x, row_y[row], tw, th}, &t, litAmount(i))) {
				fillBlack(screen, cursor[row], row_y[row] - room, x - room - cursor[row], th + 2 * room);
				cursor[row] = x + tw + room;
			}
		}
	}
	if (asked)
		tileCount(sel, sel_count, sizeof(sel_count)); // the selection's count stays the latest request
	// the black between and around the tiles: the bands above, between and under the rows, and each row's gaps
	int r0 = row_y[0] - room, r0b = row_y[0] + th + room, r1 = row_y[1] - room, r1b = row_y[1] + th + room;
	fillBlack(screen, 0, bar, screen->w, r0 - bar);
	fillBlack(screen, 0, r0b, screen->w, r1 - r0b);
	fillBlack(screen, 0, r1b, screen->w, screen->h - r1b);
	for (int row = 0; row < 2; row++)
		fillBlack(screen, cursor[row], row_y[row] - room, screen->w - cursor[row], th + 2 * room);

	if (g.sliding) {
		// one darkening pass over the edges: the columns cut by a screen edge under a 70% black layer, and the edge
		// fades (black at the edge → clear over 20% of the width) on a side with more to scroll. Per column: the
		// cut-column shade applies over the whole body height, where outside the tiles it only meets black.
		static Uint8 col_a[4096];
		int w = screen->w < 4096 ? screen->w : 4096;
		memset(col_a, 0, (size_t)w);
		for (int col = first; col <= last; col++) {
			int x = NX_DPF(GridLayout_columnX(&g, col, off));
			if (x >= 0 && x + tw <= screen->w)
				continue;
			for (int px = x < 0 ? 0 : x; px < x + tw && px < w; px++)
				col_a[px] = CUT_SHADE_ALPHA;
		}
		float hi = GridLayout_offsetFor(&g, GridLayout_columnCount(&g) - 1);
		int fw = (int)(screen->w * EDGE_FADE_SHARE + 0.5f);
		bool fade_l = off > 0.5f, fade_r = off < hi - 0.5f;
		for (int i = 0; i < fw && i < w; i++) {
			Uint32 a = (Uint32)((1.0f - (fw > 1 ? (float)i / (float)(fw - 1) : 1.0f)) * 255.0f + 0.5f);
			if (fade_l)
				col_a[i] = (Uint8)(255 - (255 - a) * (255 - col_a[i]) / 255);
			if (fade_r)
				col_a[w - 1 - i] = (Uint8)(255 - (255 - a) * (255 - col_a[w - 1 - i]) / 255);
		}
		// the selected tile (ring room included) stays out of the pass: the lit look is never dimmed by an edge
		int sx = NX_DPF(GridLayout_columnX(&g, sel_col, off)) - room;
		SDL_Rect lit_r = {sx, row_y[sel_row] - room, tw + 2 * room, th + 2 * room};
		darkenExcept(screen, w, bar, body_h, col_a, &lit_r);
	}

	SDL_SetClipRect(screen, &prev_clip);

	// what the next move needs is composed between frames (GridView_prefetchStep), for this layout and selection
	pf.armed = true;
	pf.g = g;
	pf.n = n;
	pf.sel = sel;
}

static bool pastDeadline(Uint32 deadline) {
	return (Sint32)(SDL_GetTicks() - deadline) >= 0; // wrap-safe
}

// Compose ahead what the next move needs, so it starts on cached tiles: the columns just off screen either side at
// the slide's target (plain) and the selection's neighbours (lit; the caption isn't part of the cached look). Until
// done or the deadline; true when it stopped with more.
static bool prefetchTiles(const GridLayout* g, int n, int sel, Uint32 deadline) {
	int tw = NX_DPF(g->tile_w), th = NX_DPF(g->tile_h);
	float scale = GridLayout_tileK(g);
	int room = ringRoom();
	bool more = false, asked = false, counted = false;
	int first, last;
	GridLayout_visibleColumns(g, off_to, &first, &last);
	for (int col = first; col <= last && !more; col++) {
		int x = NX_DPF(GridLayout_columnX(g, col, off_to));
		if (x + tw + room > 0 && x - room < screen->w)
			continue; // on screen at the target: drawn (cached) as the slide brings it in
		for (int row = 0; row < 2; row++) {
			int i = GridLayout_index(g, col, row);
			if (i < 0)
				continue;
			if (pastDeadline(deadline)) {
				more = true;
				break;
			}
			char logo[64], plain[32];
			TileSpec t;
			tileSpec(i, tw, th, scale, &t, logo);
			t.count = plainCount(i, plain, sizeof(plain), &asked);
			cachedTile(((Entry*)top->entries->items[i])->path, tw, th, &t, false);
		}
	}
	int step = g->sliding ? 2 : g->cols;
	int around[4] = {sel - 1, sel + 1, sel - step, sel + step};
	for (int k = 0; k < 4 && !more; k++) {
		int i = around[k];
		if (i < 0 || i >= n)
			continue;
		if (pastDeadline(deadline)) {
			more = true;
			break;
		}
		char logo[64], count[32];
		TileSpec t;
		tileSpec(i, tw, th, scale, &t, logo);
		// the lit look carries its count: the move then starts cached. A neighbour whose lit look already shows a
		// known count reuses it (no CollCount stat each pass); the selection always asks afresh.
		const char* path = ((Entry*)top->entries->items[i])->path;
		const char* known = litCount(path, tw, th);
		if (known) {
			snprintf(count, sizeof(count), "%s", known);
		} else {
			tileCount(i, count, sizeof(count));
			counted = true;
		}
		t.count = count;
		cachedTile(path, tw, th, &t, true);
	}
	if (counted || asked) {
		char sel_count[32];
		tileCount(sel, sel_count, sizeof(sel_count)); // the selection's count stays the latest request
	}
	return more;
}

bool GridView_prefetchStep(Uint32 deadline) {
	if (!pf.armed)
		return false;
	// the grid the layout was worked out for is gone (a new list, tab, size or scale not drawn yet, or a moved
	// selection): nothing until the next render re-arms it
	if (!GridView_active() || !top || !screen || top->serial != seen_top || MenuTabs_generation() != seen_gen ||
		top->entries->count != pf.n || screen->w != seen_screen_w || (int)FIXED_SCALE != seen_scale ||
		View_selectedIndex(pf.n) != pf.sel) {
		pf.armed = false;
		return false;
	}
	// not through the first part of a single move's slide or crossfade (a compose would stall their frames); past
	// PREFETCH_TWEEN_SHARE, or on a held D-pad (its repeats retarget both before they get that far), the target's
	// tiles are composed so the next moves land on cached ones
	if ((slide_tw.active && !slide_retargeted && tweenProgress(&slide_tw, SLIDE_MS) < PREFETCH_TWEEN_SHARE) ||
		(lit_tw.active && !lit_retargeted && tweenProgress(&lit_tw, LIT_MS) < PREFETCH_TWEEN_SHARE))
		return true;
	if (prefetchTiles(&pf.g, pf.n, pf.sel, deadline))
		return true;
	pf.armed = false;
	return false;
}

// Select `index` and keep the List window consistent for a later switch back to List: the selection at the top, clamped.
static void selectTile(int index, int n) {
	top->selected = index;
	ListWindow_selectAtTop(n, GameList_rowCount(), index, &top->start, &top->end);
}

void GridView_focusBottom(void) {
	int n = top ? top->entries->count : 0;
	if (!screen || n <= 0)
		return;
	GridLayout g;
	computeLayout(screen, n, &g);
	selectTile(GridLayout_bottomOf(&g, View_selectedIndex(n)), n);
}

bool GridView_handleInput(unsigned long now, bool* dirty, bool* switched_tab) {
	(void)now;
	if (switched_tab)
		*switched_tab = false;
	int btn;
	GridDir dir;
	if (PAD_justRepeated(BTN_UP)) {
		btn = BTN_UP;
		dir = GRID_DIR_UP;
	} else if (PAD_justRepeated(BTN_DOWN)) {
		btn = BTN_DOWN;
		dir = GRID_DIR_DOWN;
	} else if (PAD_justRepeated(BTN_LEFT)) {
		btn = BTN_LEFT;
		dir = GRID_DIR_LEFT;
	} else if (PAD_justRepeated(BTN_RIGHT)) {
		btn = BTN_RIGHT;
		dir = GRID_DIR_RIGHT;
	} else {
		return false;
	}

	int n = top->entries->count;
	GridLayout g;
	computeLayout(screen, n, &g);
	int index = n > 0 ? View_selectedIndex(n) : 0;
	if (dir == GRID_DIR_UP && stack->count == 1 && GridLayout_isTopRow(&g, index)) {
		// the main menu: UP from the top row focuses the tab row (a fresh press; a held one stops, no wrap)
		if (PAD_justPressed(btn)) {
			MenuTabs_setFocused(true);
			*dirty = true;
		}
		return true;
	}
	GridMove m = GridLayout_move(&g, &index, dir);
	if (m == GRID_MOVE_MOVED) {
		selectTile(index, n);
		*dirty = true;
	} else if ((m == GRID_MOVE_EDGE_PREV || m == GRID_MOVE_EDGE_NEXT) && stack->count == 1 && PAD_justPressed(btn)) {
		// the main menu: past an end, a fresh press switches tab; a held key stops (game lists always stop)
		unsigned gen = MenuTabs_generation();
		GameList_switchTab(m == GRID_MOVE_EDGE_PREV ? -1 : 1, dirty);
		if (switched_tab)
			*switched_tab = MenuTabs_generation() != gen;
	}
	return true;
}

bool GridView_animating(void) {
	if (!GridView_active()) {
		pf.armed = false;
		slide_tw.active = lit_tw.active = false;
		off_from = off_to;
		return false;
	}
	bool a = tweenTick(&slide_tw, SLIDE_MS);
	bool b = tweenTick(&lit_tw, LIT_MS);
	if (a && !slide_tw.active)
		off_from = off_to;
	return a || b;
}

void GridView_quit(void) {
	pf.armed = false;
	tileCacheClear(); // the tile kinds are rowview.c's: RowView_quit frees them
}
