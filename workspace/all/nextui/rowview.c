// The Carousel and Backdrop styles (§8b): render and D-pad input over the current Directory `top`. A main-menu tab
// (Consoles, Collections, Tools) in Carousel draws the frameless row on plain black: Consoles' logo slot with its "N
// games" under the logo as drawn, Collections' names with the selected one's count, Tools' slots (sub-project 8). A
// game list draws the Carousel's tile row, or the Backdrop's box art over the picture. Geometry and curves come from
// row_model.c (dp, host-tested), the Carousel tiles from tiles.c, the box art and the Backdrop picture from
// homeart.c. Everything else (A, B, X, Y, L1/R1, MENU, SELECT, START, the F keys) stays on
// GameList_handleInput's own paths, as on Grid.
//
// Items are painted once per size into cached surfaces at two fixed sizes, the centre's and a neighbour's at rest,
// and blitted: 1:1 at rest, scaled (nearest) only while an item changes size between the two. So no font, logo or
// shape mask is ever made at a tweened size (the font, MenuArt and mask caches key on the size), and a Carousel
// tile's darkening is a colour mod over the plain black background (the whole tile, border included).

#include "rowview.h"
#include "rowview_shared.h"
#include "stackview.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_ease.h"
#include "ui_message.h"
#include "utils.h"

#include "caption_fit.h"
#include "collcount.h"
#include "content.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gamelist.h"
#include "home.h"
#include "homeart.h"
#include "imgloader.h" // screen
#include "infoband.h"
#include "launcher.h"
#include "menuart.h"
#include "area_scale.h"
#include "menulogo.h"
#include "menu_transition.h"
#include "menutabs.h"
#include "ui_font.h"
#include "row_model.h"
#include "stack_model.h"
#include "shortcuts.h"
#include "stack_model.h"
#include "tiles.h"
#include "types.h"
#include "ui_accent.h"
#include "ui_fade.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLIDE_MS 300					// the row's slide (§8b.2)
#define FADE_MS MENU_TRANSITION_FADE_MS // the Backdrop picture's crossfade, and its fade in from black (§8b.4)
#define BAR_DP (28.0f * 42.0f / 30.0f)	// the header and the hint bar: 28 logical each
#define ROOM_DP 4.0f					// ring/shadow room above and below the row: the 3 dp ring + 1
#define RING_DP 3.0f
#define SCALE_EPS 0.004f // an item this close to a rest size is drawn 1:1 from that size's surface

#define CAROUSEL_CAPTION_GAP_DP 16.0f // × f, under the row
#define BACKDROP_CAPTION_GAP_DP 18.0f // × f, under the row
#define CAPTION_NAME_SP 18.0f
#define CAPTION_INFO_SP 14.0f
#define CAPTION_GUTTER_DP 24.0f

#define TILE_W_SPEC 140.0f // the Grid tile's width: tiles.c's insets scale by tile_w / 140 (never above 1)
#define BOX_SLOT_W_SPEC 170.0f
#define LOGO_SLOT_W_SPEC ROWVIEW_LOGO_SLOT_W_SPEC
#define TOOL_SLOT_W_SPEC ROWVIEW_TOOL_SLOT_W_SPEC

#define SLOT_PAD_DP 8.0f		// tool and collection names in a Backdrop slot
#define SLOT_TEXT_PAD_DP 12.0f	// a logo-less console's name in the logo slot
#define SLOT_TOOL_ICON_DP 60.0f // × the slot content scale
#define SLOT_TOOL_GAP_DP 12.0f
#define SLOT_TOOL_NAME_SP 17.0f
#define SLOT_LOGO_NAME_SP 24.0f
#define SLOT_LOGO_GREY 224 // Consoles' logos at #E0E0E0
#define TOOL_LONGEST_WORD "Achievements"

// the placeholder box (§8b.4)
#define PH_ASPECT 0.72f
#define PH_PLATE_ALPHA 217 // black at 85%
#define PH_BORDER_ALPHA 31 // white at 12%
#define PH_SPINE_ALPHA 26  // white at 10%
#define PH_SPINE_SHARE 0.06f
#define PH_TITLE_LINES 3
#define SHADOW_OFF_DP 4.0f // the box-art shadow (homeart.c bakes the same for real box art)
#define SHADOW_BLUR_DP 8.0f
#define SHADOW_ALPHA 128 // black at 50%

#define ITEM_SLOTS 32				 // a neighbour is its selected-size surface plus the scaled copy (sideCopy)
#define ITEM_KEY (2 * MAX_PATH + 96) // the path and the drawn name both fit
#define CAPTION_KEY 1024

// The row's position (items): eases from pos_from to pos_to.
static float pos_from = 0, pos_to = 0;
static Tween slide_tw;
// What the position belongs to: a change of any snaps it (keyed on `top` and the tab generation, not on the list).
static unsigned seen_top = 0; // the Directory serial (0 = none)
static unsigned seen_gen = 0;
static int seen_n = -1, seen_screen_w = 0, seen_scale = 0, seen_kind = -1;

// Tile kinds per index of the current list, worked out on first sight (a folder game stats the disk).
static signed char* kinds = NULL;
static int kinds_cap = 0;
static unsigned kinds_top = 0;
static unsigned kinds_gen = 0;
static int kinds_n = -1;

// The Backdrop picture: the shown "from" layer (a private copy: HomeArt's surfaces don't outlive a later call), the
// target game's path ("" = black), and the crossfade toward it, started once the target resolved.
static SDL_Surface* pic_from = NULL;
static bool from_valid = false; // false: the from layer is black
static char to_path[MAX_PATH];
static bool to_set = false, fade_started = false;
static Tween fade_tw;
static unsigned pic_top = 0;
static bool pic_on = false;
// B out of a Backdrop game list: the outgoing screen fades to black before the list closes (menu_transition.h).
static MenuTransitionExit exit_fade;
static unsigned exit_top = 0; // the list it belongs to (the Directory serial)

// Item surfaces at the two rest sizes, keyed by kind, size, state, path and the drawn name (a rename changes it).
typedef struct {
	char key[ITEM_KEY];
	SDL_Surface* s; // may be NULL (a remembered failed build)
	unsigned stamp;
	bool used;
} ItemSlot;
static ItemSlot items[ITEM_SLOTS];
static unsigned item_clock = 0;
static unsigned item_builds = 0;	  // items built so far (the settled prefetch's budget counts them)
static bool prefetch_pending = false; // the settled prefetch stopped at its budget: one more frame to continue

// The main-menu Carousel's "N games" line in the accent: Consoles' 8 dp under the selected logo as drawn, its y
// gliding as logo heights differ; Collections' on the selected item (its line is reserved in every item), fading in.
// The text surface is rebuilt only when its text, size or colour changes, the line's place on a new selection (and,
// for Consoles, when the selected logo's surface changes).
static struct {
	char key[96];
	SDL_Surface* s;
	int sel;				// the selection the place was worked out for (-1 = none)
	int y_from, y_to;		// Consoles: the line's top (px), gliding from → to
	const SDL_Surface* art; // Consoles: the logo surface y_to was worked out for (NULL = none, the name)
	int art_w, art_h;		// and its size
	int off;				// Collections: the line's centre below the item's centre, at the selected size (px)
	bool fade_pending;		// Collections: the fade starts when the selection's count is first known
	Tween glide, fade;
} cnt = {.sel = -1};

// The caption under the row (or beside a Vertical stack), rebuilt only when its text or look changes.
static struct {
	char key[CAPTION_KEY];
	SDL_Surface* s;
	SDL_Rect ink;  // the part with any alpha: only it is blitted (the rest of the block is clear)
	int content_h; // the lines' height as laid out (px; ≤ the surface's)
} caption;

///////////////////////////////////////
// Timing

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

static float currentPos(void) {
	if (!slide_tw.active)
		return pos_to;
	return pos_from + (pos_to - pos_from) * UI_easeStandard(tweenProgress(&slide_tw, SLIDE_MS));
}

///////////////////////////////////////
// Units

static float pxPerSp(void) {
	return FIXED_SCALE * 12.0f / 14.0f;
}

static int dpToPx(float dp) {
	return (int)floorf(dp * pxPerDp() + 0.5f);
}

static int textW(TTF_Font* f, const char* t) {
	int w = 0;
	if (f && t && t[0])
		GFX_measureText(f, t, &w, NULL);
	return w;
}

///////////////////////////////////////
// What the row shows

static RowKind currentKind(void) {
	// A main-menu tab is only ever Carousel here (CFG_getMenuStyle reads a stored Backdrop as Carousel; Home draws
	// itself): the frameless row on black, never the tile row.
	if (stack->count == 1) {
		switch (MenuTabs_current()) {
		case MENU_TAB_CONSOLES:
			return ROW_BACKDROP_LOGO;
		case MENU_TAB_COLLECTIONS:
			return ROW_BACKDROP_COLL;
		default:
			return ROW_BACKDROP_TOOL; // Tools
		}
	}
	if (GameList_currentStyle() == MENU_STYLE_CAROUSEL)
		return ROW_CAROUSEL; // a game list's tile row
	if (Shortcuts_isInToolsFolder(top->path))
		return ROW_BACKDROP_TOOL; // the Tools listing
	return ROW_BACKDROP_BOX;	  // a game list
}

// Only a game list's rows have a caption (the tile row's and the box row's). The main-menu rows and a Tools listing
// name themselves, and are centred alone (Consoles' count hangs under its logo, Collections' sits in the item).
static CapKind captionKind(RowKind k) {
	return k == ROW_CAROUSEL || k == ROW_BACKDROP_BOX ? CAP_GAME : CAP_NONE;
}

// The caption's reserved height (px): the Carousel's 2 name lines + the info line; the Backdrop's name's one line +
// two info rows.
static int captionReserve(RowKind k, CapKind c) {
	if (c == CAP_NONE)
		return 0;
	TTF_Font* fn = UIFont_get(CAPTION_NAME_SP, false);
	int nh = fn ? TTF_FontHeight(fn) : 0;
	TTF_Font* fi = UIFont_get(CAPTION_INFO_SP, false);
	int ih = fi ? TTF_FontHeight(fi) : 0;
	return k == ROW_CAROUSEL ? 2 * nh + ih : nh + 2 * ih;
}

static void computeGeo(SDL_Surface* screen, RowKind kind, RowGeo* g) {
	float pd = pxPerDp();
	float sw = screen->w / pd, sh = screen->h / pd;
	float body_h = sh - 2 * BAR_DP;
	g->kind = kind;
	g->vertical = false;
	g->cap = captionKind(kind);
	g->sz = Row_sizes(kind, sw, body_h);
	g->cap_h = captionReserve(kind, g->cap); // 0: the row alone is centred between the tab row and the hint bar
	float gap = (kind == ROW_CAROUSEL ? CAROUSEL_CAPTION_GAP_DP : BACKDROP_CAPTION_GAP_DP) * g->sz.f;
	// the box slot gives way to its caption (a px of slack for the rounding to px below), so the block fits the body
	float cap_dp = g->cap_h > 0 ? (g->cap_h + 1) / pd : 0;
	Row_fitBoxSlot(&g->sz, kind, body_h, ROOM_DP, gap, cap_dp);
	float row_h = g->sz.item_h;
	float top = Row_top(BAR_DP, body_h, row_h, ROOM_DP, gap, cap_dp);
	g->cx = screen->w / 2;
	g->cy = dpToPx(top + row_h / 2);
	g->cap_y = dpToPx(top + row_h + ROOM_DP + gap);
	// what's drawn of the caption: never under the hint bar (a block that couldn't fit clamps under the header and
	// leaves out the rows that don't fit)
	int body_bottom = screen->h - barPx();
	g->cap_draw_h = g->cap_y + g->cap_h > body_bottom ? body_bottom - g->cap_y : g->cap_h;
	if (g->cap_draw_h < 0)
		g->cap_draw_h = 0;
	g->full_w = dpToPx(g->sz.item_w);
	g->full_h = dpToPx(g->sz.item_h);
	g->side_w = dpToPx(g->sz.item_w * g->sz.scale);
	g->side_h = dpToPx(g->sz.item_h * g->sz.scale);
	float spec_w = kind == ROW_BACKDROP_LOGO								? LOGO_SLOT_W_SPEC
				   : kind == ROW_BACKDROP_TOOL || kind == ROW_BACKDROP_COLL ? TOOL_SLOT_W_SPEC
																			: BOX_SLOT_W_SPEC;
	g->k = fminf(1.0f, g->sz.item_w / spec_w);
}

static int selectedIndex(int n) {
	int s = top->selected;
	if (s >= n)
		s = n - 1;
	return s < 0 ? 0 : s;
}

///////////////////////////////////////
// Entries

static void syncKinds(int n) {
	if (top->serial == kinds_top && MenuTabs_generation() == kinds_gen && n == kinds_n)
		return;
	kinds_top = top->serial;
	kinds_gen = MenuTabs_generation();
	kinds_n = n;
	if (n > kinds_cap) {
		signed char* k = realloc(kinds, (size_t)n);
		if (!k) {
			free(kinds);
			kinds = NULL;
			kinds_cap = 0;
			return;
		}
		kinds = k;
		kinds_cap = n;
	}
	if (kinds && n > 0)
		memset(kinds, -1, (size_t)n);
}

static TileKind kindFor(int index, Entry* e) {
	if (kinds && index < kinds_cap && kinds[index] >= 0)
		return (TileKind)kinds[index];
	bool at_root = stack->count == 1;
	MenuTabId tab = MenuTabs_current();
	TileKind k;
	if ((at_root && tab == MENU_TAB_TOOLS) || e->type == ENTRY_PAK)
		k = TILE_TOOL;
	else if (at_root && tab == MENU_TAB_COLLECTIONS)
		k = TILE_COLLECTION;
	else if (at_root && tab == MENU_TAB_CONSOLES && e->type == ENTRY_DIR)
		k = TILE_LOGO;
	else if (e->type == ENTRY_ROM || (e->type == ENTRY_DIR && GameList_entryIsFolderGame(e)))
		k = TILE_GAME;
	else
		k = TILE_TITLE; // a subfolder: its name
	if (kinds && index < kinds_cap)
		kinds[index] = (signed char)k;
	return k;
}

static char* displayName(Entry* e) {
	char* name = e->unique ? e->unique : e->name;
	trimSortingMeta(&name);
	return name;
}

static const char* consoleLogoFile(const char* folder_name, char* out, size_t size) {
	const char* id = MenuLogo_idForFolder(folder_name);
	if (!id)
		return NULL;
	snprintf(out, size, "menu_logo_%s.png", id);
	return out;
}

// A console row's logo file (its folder's id), or NULL.
static const char* entryLogoFile(Entry* e, char* out, size_t size) {
	const char* slash = strrchr(e->path, '/');
	return consoleLogoFile(slash ? slash + 1 : e->path, out, size);
}

static const char* toolIconFile(Entry* e, const char* name) {
	const char* icon = Tiles_toolIcon(name);
	if (!icon) {
		const char* slash = strrchr(e->path, '/');
		icon = Tiles_toolIcon(slash ? slash + 1 : e->path);
	}
	return icon;
}

// "N games" for a Consoles or Collections row ("" while unknown).
static void countLabel(Entry* e, TileKind kind, char* out, size_t size) {
	out[0] = '\0';
	if (stack->count != 1)
		return;
	if (kind == TILE_LOGO)
		GameInfo_gamesLabel(Content_consoleGameCount(e), out, size);
	else if (kind == TILE_COLLECTION)
		GameInfo_gamesLabel(CollCount_get(e->path), out, size);
}

///////////////////////////////////////
// Surfaces

static SDL_Surface* newSurface(int w, int h, bool white_ground) {
	if (w <= 0 || h <= 0)
		return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	// a white ground for white content: its anti-aliased edges keep their white when blended onto this surface
	Uint8 c = white_ground ? 255 : 0;
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, c, c, c, 0));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	return s;
}

// Straight-alpha "over" of a grey level at an alpha onto an ARGB8888 surface's rect.
static void overRect(SDL_Surface* s, int x, int y, int w, int h, Uint8 grey, Uint8 alpha) {
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int x1 = x + w > s->w ? s->w : x + w, y1 = y + h > s->h ? s->h : y + h;
	if (x0 >= x1 || y0 >= y1 || alpha == 0)
		return;
	if (SDL_MUSTLOCK(s))
		SDL_LockSurface(s);
	Uint32 sa = alpha;
	for (int yy = y0; yy < y1; yy++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + yy * s->pitch);
		for (int xx = x0; xx < x1; xx++) {
			Uint32 d = row[xx];
			Uint32 da = d >> 24;
			Uint32 keep = da * (255 - sa) / 255; // the destination's share
			Uint32 oa = sa + keep;
			if (oa == 0) {
				row[xx] = 0;
				continue;
			}
			Uint32 ch[3] = {(d >> 16) & 0xff, (d >> 8) & 0xff, d & 0xff};
			Uint32 out = oa << 24;
			for (int c = 0; c < 3; c++)
				out |= ((grey * sa + ch[c] * keep) / oa) << (16 - 8 * c);
			row[xx] = out;
		}
	}
	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
}

// A stretch target for an item changing size (grown to the largest asked; blend NONE).
static SDL_Surface* stretch_scratch = NULL;

static SDL_Surface* stretchScratch(int w, int h) {
	if (stretch_scratch && stretch_scratch->w >= w && stretch_scratch->h >= h)
		return stretch_scratch;
	if (stretch_scratch) {
		if (stretch_scratch->w > w)
			w = stretch_scratch->w;
		if (stretch_scratch->h > h)
			h = stretch_scratch->h;
		SDL_FreeSurface(stretch_scratch);
	}
	stretch_scratch = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (stretch_scratch)
		SDL_SetSurfaceBlendMode(stretch_scratch, SDL_BLENDMODE_NONE);
	return stretch_scratch;
}

// Blit s centred on (cx, cy), scaled by factor (1:1 when it is 1), at alpha a and grey level c, onto an opaque dst
// (every caller's is: the screen's body is the picture or plain black, and the from picture is opaque). At grey 255
// it is UI_blitBlendOpaque (scaled: SDL's fast nearest stretch into a scratch first, no blending); a grey level goes
// through SDL. The surface's mods and blend mode are restored (HomeArt's and the cache's surfaces are shared).
static void blitCentred(SDL_Surface* dst, SDL_Surface* s, int cx, int cy, float factor, Uint8 a, Uint8 c) {
	if (!s || a == 0 || !(factor > 0.0f))
		return;
	bool exact = fabsf(factor - 1.0f) < SCALE_EPS;
	int w = exact ? s->w : (int)(s->w * factor + 0.5f);
	int h = exact ? s->h : (int)(s->h * factor + 0.5f);
	if (w <= 0 || h <= 0)
		return;
	SDL_Rect r = {cx - w / 2, cy - h / 2, w, h};
	Uint8 oa, orr, og, ob;
	SDL_BlendMode bm;
	SDL_GetSurfaceAlphaMod(s, &oa);
	SDL_GetSurfaceColorMod(s, &orr, &og, &ob);
	SDL_GetSurfaceBlendMode(s, &bm);
	if (c == 255) {
		if (exact) {
			UI_blitBlendOpaque(s, NULL, dst, r.x, r.y, a);
			return;
		}
		SDL_Surface* tmp = stretchScratch(w, h);
		if (tmp) {
			SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
			SDL_SetSurfaceAlphaMod(s, 255);
			SDL_SetSurfaceColorMod(s, 255, 255, 255);
			SDL_BlitScaled(s, NULL, tmp, &(SDL_Rect){0, 0, w, h});
			SDL_SetSurfaceAlphaMod(s, oa);
			SDL_SetSurfaceColorMod(s, orr, og, ob);
			SDL_SetSurfaceBlendMode(s, bm);
			UI_blitBlendOpaque(tmp, &(SDL_Rect){0, 0, w, h}, dst, r.x, r.y, a);
			return;
		}
	}
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	SDL_SetSurfaceAlphaMod(s, a);
	SDL_SetSurfaceColorMod(s, c, c, c);
	if (exact)
		SDL_BlitSurface(s, NULL, dst, &r);
	else
		SDL_BlitScaled(s, NULL, dst, &r);
	SDL_SetSurfaceAlphaMod(s, oa);
	SDL_SetSurfaceColorMod(s, orr, og, ob);
	SDL_SetSurfaceBlendMode(s, bm);
}

// Black over the whole of dst at alpha a.
static void blackOver(SDL_Surface* dst, Uint8 a) {
	UI_dimRect(dst, NULL, a);
}

///////////////////////////////////////
// The item cache

static bool itemFind(const char* key, SDL_Surface** out) {
	for (int i = 0; i < ITEM_SLOTS; i++) {
		if (items[i].used && strcmp(items[i].key, key) == 0) {
			items[i].stamp = ++item_clock;
			*out = items[i].s;
			return true;
		}
	}
	return false;
}

// Takes ownership of s (NULL remembers a failed build). Evicts the least recently used slot.
static SDL_Surface* itemStore(const char* key, SDL_Surface* s) {
	item_builds++;
	ItemSlot* victim = &items[0];
	for (int i = 0; i < ITEM_SLOTS; i++) {
		if (!items[i].used) {
			victim = &items[i];
			break;
		}
		if (items[i].stamp < victim->stamp)
			victim = &items[i];
	}
	if (victim->s)
		SDL_FreeSurface(victim->s);
	snprintf(victim->key, sizeof(victim->key), "%s", key);
	victim->s = s;
	victim->used = true;
	victim->stamp = ++item_clock;
	return s;
}

static void itemsClear(void) {
	for (int i = 0; i < ITEM_SLOTS; i++) {
		if (items[i].s)
			SDL_FreeSurface(items[i].s);
	}
	memset(items, 0, sizeof(items));
	item_clock = 0;
}

///////////////////////////////////////
// Carousel tiles

// The tile for item `e` at w×h px, plain (lit false) or lit, with its ring room around it. A game's screenshot is
// requested at the centre size for both sizes (one cache entry per game; the side tile scales it down once).
static SDL_Surface* carouselTile(const RowGeo* g, Entry* e, TileKind kind, int w, int h, bool lit) {
	SDL_Surface* pic = NULL;
	int state = 0;
	if (kind == TILE_GAME) {
		HomeArtState st = HomeArt_pin(e->path, g->full_w, g->full_h, 0, &pic); // valid until the next HomeArt_*
		state = st == HOMEART_READY && pic ? 2 : (st == HOMEART_LOADING ? 1 : 0);
	}
	char key[ITEM_KEY];
	// the lit look's ring (a game) or fill (a tool) is in the accent, a tool's content in its ink
	SDL_Color ac = lit ? UI_accent() : (SDL_Color){0, 0, 0, 0};
	SDL_Color ink = lit ? UI_onAccent() : (SDL_Color){0, 0, 0, 0};
	snprintf(key, sizeof(key), "C|%d|%d|%d|%d|%02x%02x%02x%02x%02x%02x|%d|%s|%s", w, h, (int)kind, lit, ac.r, ac.g,
			 ac.b, ink.r, ink.g, ink.b, state, e->path, displayName(e));
	SDL_Surface* s;
	if (itemFind(key, &s))
		return s;

	int ring = NX_DPF(RING_DP) + 1;
	s = newSurface(w + 2 * ring, h + 2 * ring, false);
	if (s) {
		// composed on the Carousel's plain black ground, so the whole surface is opaque: drawn by a copy (or the
		// darkening's lerp toward the black under it), and stretched without blending while it changes size
		SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 255));
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
		char logo[64];
		float tile_dp = w / pxPerDp();
		TileSpec t = {.kind = kind, .name = displayName(e), .carousel = true};
		t.scale = tile_dp / TILE_W_SPEC > 1.0f ? 1.0f : tile_dp / TILE_W_SPEC;
		if (kind == TILE_LOGO)
			t.logo_file = entryLogoFile(e, logo, sizeof(logo));
		else if (kind == TILE_TOOL)
			t.icon_file = toolIconFile(e, t.name);
		else if (kind == TILE_GAME) {
			if (state == 2)
				t.picture = pic;
			else
				t.kind = TILE_TITLE; // no screenshot: a title tile
			if (state == 1)
				t.name = NULL; // still loading: the black base only
		}
		Tiles_draw(s, (SDL_Rect){ring, ring, w, h}, &t, lit ? 1.0f : 0.0f);
	}
	return itemStore(key, s);
}

///////////////////////////////////////
// Backdrop slots

// A font at `sp` shrunk (never grown) until `word` fits avail px, never below min_sp.
static TTF_Font* fitFont(float sp, float min_sp, const char* word, int avail) {
	TTF_Font* f = UIFont_get(sp, false);
	int w = textW(f, word);
	if (!f || avail <= 0 || w <= avail)
		return f;
	float fit = sp * (float)avail / (float)w;
	if (fit < min_sp)
		fit = min_sp;
	f = UIFont_get(fit, false);
	// NX_SP rounds to the nearest px: one px smaller when that rounding still overflows
	float one_px = 1.0f / pxPerSp();
	if (f && textW(f, word) > avail && fit - one_px >= min_sp)
		f = UIFont_get(fit - one_px, false);
	return f;
}

// The widest whitespace-separated word of name (measured in f) into out.
static void longestWord(TTF_Font* f, const char* name, char* out, size_t size) {
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
			int w = textW(f, word);
			if (w > best) {
				best = w;
				snprintf(out, size, "%s", word);
			}
		}
		p = q;
	}
}

// A collection item's text at w×h px, a side item at lvl of the selected size (k_full: the selected size's slot
// content scale). The name size is worked out at the selected size, so a side item is the same item smaller: 30 sp ×
// k_full, shrunk in whole sp until its longest word fits the slot less 8 dp each side, never below max(0.75 × that,
// 1.25 × the count); then the count line (max(10, 14 × k_full) sp, 6 dp unscaled under the name; × lvl on a side
// item) reserved under it. In a Vertical stack (fit) the slot holds it all: the name then shrinks on in whole sp, below
// that floor if needed, until its block and the count line fit the slot's height (Row_collFitSlotSp).
typedef struct {
	RowCollText t;
	float sp;  // the name's size
	int step;  // its line height (1.15)
	int avail; // its width
} CollLayout;

// Row_collFitSlotSp's measure: the lines `name` wraps to at sp (× lvl) in avail px, with its 1.15 step.
typedef struct {
	const char* name;
	int avail;
	float lvl;
} CollWrap;

static int collLinesAt(float sp, void* ctx) {
	const CollWrap* cw = ctx;
	int step = Row_lineStep(NX_SP(sp * cw->lvl), ROW_COLL_LINE);
	TTF_Font* f = UIFont_get(sp * cw->lvl, false);
	int nh = Tiles_textBlockStep(NULL, f, cw->name, 0, 0, cw->avail, ROW_COLL_LINES, false, 255, 255, false, step);
	return step > 0 && nh > 0 ? nh / step : 1;
}

static CollLayout collLayout(const char* name, int w, int h, float k_full, float lvl, bool fit) {
	CollLayout c;
	int full_avail = (int)(w / lvl + 0.5f) - 2 * NX_DPF(SLOT_PAD_DP);
	float start = ROW_COLL_NAME_SP * k_full;
	char word[256];
	TTF_Font* f = UIFont_get(start, false);
	longestWord(f, name, word, sizeof(word));
	float count_sp = Row_countSp(k_full);
	float sp = Row_collNameSp(start, (float)textW(f, word), (float)full_avail, count_sp);
	// widths don't scale exactly with the size (hinting, NX_SP's rounding): a whole sp more while it still overflows
	float floor_sp = Row_collNameFloor(start, count_sp);
	while (sp > floor_sp && textW(UIFont_get(sp, false), word) > full_avail) {
		float next = sp == start ? ceilf(start) - 1.0f : sp - 1.0f;
		sp = next > floor_sp ? next : floor_sp;
	}
	c.avail = w - 2 * NX_DPF(SLOT_PAD_DP * lvl);
	TTF_Font* fc = UIFont_get(count_sp * lvl, false);
	int count_h = fc ? TTF_FontHeight(fc) : 0;
	if (fit) {
		CollWrap cw = {name, c.avail, lvl};
		sp = Row_collFitSlotSp(sp, ROW_COUNT_MIN_SP, pxPerSp() * lvl, h, NX_DPF(ROW_COLL_COUNT_GAP_DP * lvl), count_h,
							   collLinesAt, &cw);
	}
	c.sp = sp * lvl;
	c.step = Row_lineStep(NX_SP(c.sp), ROW_COLL_LINE);
	f = UIFont_get(c.sp, false);
	int nh = Tiles_textBlockStep(NULL, f, name, 0, 0, c.avail, ROW_COLL_LINES, false, 255, 255, false, c.step);
	int lines = c.step > 0 && nh > 0 ? nh / c.step : 1;
	c.t = Row_collText(h, lines, c.step, NX_DPF(ROW_COLL_COUNT_GAP_DP * lvl), count_h);
	return c;
}

// A logo-less console's name in the logo slot (24 sp × k, two lines): its height measured, or drawn at y.
static int logoName(SDL_Surface* s, const char* name, int w, int y, float k, float lvl) {
	TTF_Font* f = UIFont_get(SLOT_LOGO_NAME_SP * k, false);
	return Tiles_textBlock(s, f, name, w / 2, y, w - 2 * NX_DPF(SLOT_TEXT_PAD_DP * lvl), 2, false, 255, 255, false);
}

// A Backdrop slot item at w×h px (pad none, transparent white ground): content scale k already includes the size's
// own scale (lvl, 1 or the neighbour scale), and px paddings scale by lvl.
static SDL_Surface* buildSlot(Entry* e, TileKind kind, int w, int h, float k, float lvl, bool fit) {
	SDL_Surface* s = newSurface(w, h, true);
	if (!s)
		return NULL;
	const char* name = displayName(e);
	char logo[64];
	switch (kind) {
	case TILE_LOGO: {
		const char* file = entryLogoFile(e, logo, sizeof(logo));
		SDL_Surface* art = file ? MenuArt_get(file, w, h) : NULL;
		if (art) {
			// off-white, not the art's pure white (too harsh on black); the ground matches so the edges stay that grey
			SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, SLOT_LOGO_GREY, SLOT_LOGO_GREY, SLOT_LOGO_GREY, 0));
			SDL_SetSurfaceColorMod(art, SLOT_LOGO_GREY, SLOT_LOGO_GREY, SLOT_LOGO_GREY);
			SDL_BlitSurface(art, NULL, s, &(SDL_Rect){(w - art->w) / 2, (h - art->h) / 2, art->w, art->h});
			SDL_SetSurfaceColorMod(art, 255, 255, 255); // MenuArt's copy is shared
			break;
		}
		// no logo: the name is the item (24 sp, two lines)
		logoName(s, name, w, (h - logoName(NULL, name, w, 0, k, lvl)) / 2, k, lvl);
		break;
	}
	case TILE_TOOL: {
		int icon_px = NX_DPF(SLOT_TOOL_ICON_DP * k);
		const char* file = toolIconFile(e, name);
		SDL_Surface* icon = file ? MenuArt_get(file, icon_px, icon_px) : NULL;
		int avail = w - 2 * NX_DPF(SLOT_PAD_DP * lvl);
		TTF_Font* f = fitFont(SLOT_TOOL_NAME_SP * k, 1.0f, TOOL_LONGEST_WORD, avail);
		int th = Tiles_textBlock(NULL, f, name, w / 2, 0, avail, 2, true, 255, 255, false);
		int gap = icon && th > 0 ? NX_DPF(SLOT_TOOL_GAP_DP * k) : 0;
		int y = (h - ((icon ? icon->h : 0) + gap + th)) / 2;
		if (icon) {
			SDL_BlitSurface(icon, NULL, s, &(SDL_Rect){(w - icon->w) / 2, y, icon->w, icon->h});
			y += icon->h + gap;
		}
		Tiles_textBlock(s, f, name, w / 2, y, avail, 2, true, 255, 255, false);
		break;
	}
	case TILE_COLLECTION: {
		// the name (from 30 sp, line height 1.15, ≤ 2 lines with "…") over the count line every item reserves, so
		// names don't move; the count itself is drawn per frame on the selected item only (drawCount)
		// (fit: a Vertical stack's slot holds the whole block)
		CollLayout c = collLayout(name, w, h, k / lvl, lvl, fit);
		TTF_Font* f = UIFont_get(c.sp, false); // a font is only good until a later UIFont_get
		Tiles_textBlockStep(s, f, name, w / 2, c.t.name_y, c.avail, ROW_COLL_LINES, false, 255, 255, false, c.step);
		break;
	}
	default: { // a game or a folder in a slot row (unusual): its name
		TTF_Font* f = UIFont_get(SLOT_TOOL_NAME_SP * k, false);
		int max_w = w - 2 * NX_DPF(SLOT_PAD_DP * lvl);
		int th = Tiles_textBlock(NULL, f, name, w / 2, 0, max_w, 3, false, 255, 255, false);
		Tiles_textBlock(s, f, name, w / 2, (h - th) / 2, max_w, 3, false, 255, 255, false);
		break;
	}
	}
	return s;
}

// A neighbour at rest: the selected-size surface `full` (cached under full_key) scaled by `scale`, kept, keyed on that
// surface's own key. The same nearest stretch blitCentred does while an item changes size, at the same rounded size,
// so an item settling into (or leaving) the neighbour size never swaps to another fit of its text: one composition,
// scaled (sub-project 9 device fix).
static SDL_Surface* sideCopy(const char* full_key, SDL_Surface* full, float scale) {
	if (!full)
		return NULL;
	int w = (int)(full->w * scale + 0.5f), h = (int)(full->h * scale + 0.5f);
	if (w <= 0 || h <= 0)
		return NULL;
	char key[ITEM_KEY];
	snprintf(key, sizeof(key), "n|%d|%d|%s", w, h, full_key);
	SDL_Surface* s;
	if (itemFind(key, &s))
		return s;
	s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s) {
		Uint8 oa, orr, og, ob;
		SDL_BlendMode bm;
		SDL_GetSurfaceAlphaMod(full, &oa);
		SDL_GetSurfaceColorMod(full, &orr, &og, &ob);
		SDL_GetSurfaceBlendMode(full, &bm);
		SDL_SetSurfaceBlendMode(full, SDL_BLENDMODE_NONE);
		SDL_SetSurfaceAlphaMod(full, 255);
		SDL_SetSurfaceColorMod(full, 255, 255, 255);
		// area-averaged (a neighbour is 0.4-0.62 of the selected size: a nearest pick leaves jagged edges)
		if (full->format->format != SDL_PIXELFORMAT_ARGB8888 ||
			AreaScale_argb(full->pixels, full->w, full->h, full->pitch / 4, s->pixels, w, h, s->pitch / 4) != 0)
			SDL_BlitScaled(full, NULL, s, NULL);
		SDL_SetSurfaceAlphaMod(full, oa);
		SDL_SetSurfaceColorMod(full, orr, og, ob);
		SDL_SetSurfaceBlendMode(full, bm);
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	}
	return itemStore(key, s);
}

// A frameless slot: composed once, at the selected size (its text fitted there); a neighbour is that surface scaled.
static SDL_Surface* slotItem(const RowGeo* g, Entry* e, TileKind kind, bool side) {
	char key[ITEM_KEY];
	// a Vertical stack's slots have their own sizes and content scale: their own keys, never a horizontal one's
	snprintf(key, sizeof(key), "%s|%d|%d|%d|%s|%s", g->vertical ? "V" : "S", g->full_w, g->full_h, (int)kind, e->path,
			 displayName(e));
	SDL_Surface* full;
	if (!itemFind(key, &full))
		full = itemStore(key, buildSlot(e, kind, g->full_w, g->full_h, g->k, 1.0f, g->vertical));
	return side ? sideCopy(key, full, g->sz.scale) : full;
}

// The placeholder box (§8b.4) fitted in a slot_w×slot_h slot, over the soft shadow, padded for it on every side.
static SDL_Surface* buildPlaceholder(int slot_w, int slot_h, float lvl, const char* title) {
	int bw = slot_w, bh;
	if ((float)slot_h * PH_ASPECT < (float)slot_w)
		bw = (int)(slot_h * PH_ASPECT + 0.5f);
	bh = (int)(bw / PH_ASPECT + 0.5f);
	if (bh > slot_h)
		bh = slot_h;
	if (bw <= 0 || bh <= 0)
		return NULL;
	int off = NX_DPF(SHADOW_OFF_DP * lvl);
	int r = NX_DPF(SHADOW_BLUR_DP * lvl) / 3;
	if (r < 1)
		r = 1;
	int pad = off + r * 3 + 1;
	int W = bw + 2 * pad, H = bh + 2 * pad;
	SDL_Surface* s = newSurface(W, H, false);
	if (!s)
		return NULL;

	// the shadow: the box's shape at 50%, 4 dp down, box-blurred 3 times (≈ the 8 dp blur)
	unsigned char* a = calloc((size_t)W * H, 1);
	unsigned char* tmp = malloc((size_t)W * H);
	if (a && tmp) {
		for (int y = pad + off; y < pad + off + bh && y < H; y++)
			memset(a + (size_t)y * W + pad, SHADOW_ALPHA, (size_t)bw);
		Row_boxBlurAlpha(a, tmp, W, H, r, 3);
		if (SDL_MUSTLOCK(s))
			SDL_LockSurface(s);
		for (int y = 0; y < H; y++) {
			Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
			for (int x = 0; x < W; x++)
				row[x] = (Uint32)a[(size_t)y * W + x] << 24; // black
		}
		if (SDL_MUSTLOCK(s))
			SDL_UnlockSurface(s);
	}
	free(a);
	free(tmp);

	// the plate, its 1 dp border and the spine
	int b = NX_DPF(1.0f * lvl);
	if (b < 1)
		b = 1;
	overRect(s, pad, pad, bw, bh, 0, PH_PLATE_ALPHA);
	overRect(s, pad, pad, bw, b, 255, PH_BORDER_ALPHA);
	overRect(s, pad, pad + bh - b, bw, b, 255, PH_BORDER_ALPHA);
	overRect(s, pad, pad + b, b, bh - 2 * b, 255, PH_BORDER_ALPHA);
	overRect(s, pad + bw - b, pad + b, b, bh - 2 * b, 255, PH_BORDER_ALPHA);
	overRect(s, pad + b, pad + b, (int)(bw * PH_SPINE_SHARE + 0.5f), bh - 2 * b, 255, PH_SPINE_ALPHA);

	// the content box: past the spine, 6% side and 7% top/bottom margins
	int x0 = pad + (int)(bw * (PH_SPINE_SHARE + 0.06f) + 0.5f), x1 = pad + bw - (int)(bw * 0.06f + 0.5f);
	int y0 = pad + (int)(bh * 0.07f + 0.5f), y1 = pad + bh - (int)(bh * 0.07f + 0.5f);
	// only the title (no console logo, 2026-10-02): 13% of the box's width, 10 to 20 sp, up to three lines, centred
	float sp = 0.13f * (bw / pxPerDp());
	sp = sp < 10.0f ? 10.0f : (sp > 20.0f ? 20.0f : sp);
	TTF_Font* f = UIFont_get(sp, false);
	if (f && title && x1 > x0) {
		int th = Tiles_textBlock(NULL, f, title, (x0 + x1) / 2, 0, x1 - x0, PH_TITLE_LINES, false, 255, 255, false);
		Tiles_textBlock(s, f, title, (x0 + x1) / 2, y0 + (y1 - y0 - th) / 2, x1 - x0, PH_TITLE_LINES,
						false, 255, 255, false);
	}
	return s;
}

// The placeholder box: composed once at the selected size (its title fitted there); a neighbour is that scaled.
static SDL_Surface* placeholderItem(const RowGeo* g, Entry* e, TileKind kind, bool side) {
	char key[ITEM_KEY];
	snprintf(key, sizeof(key), "P|%d|%d|%s|%s", g->full_w, g->full_h, e->path, displayName(e));
	SDL_Surface* full;
	if (!itemFind(key, &full))
		full = itemStore(key, buildPlaceholder(g->full_w, g->full_h, 1.0f, displayName(e)));
	return side ? sideCopy(key, full, g->sz.scale) : full;
}

///////////////////////////////////////
// Drawing the row

// An item drawn from its two rest-size surfaces: 1:1 at a rest size, else the centre one scaled.
static void drawRested(SDL_Surface* screen, SDL_Surface* (*get)(const RowGeo*, Entry*, TileKind, bool),
					   const RowGeo* g, Entry* e, TileKind kind, float s, int cx, int cy, Uint8 a) {
	if (fabsf(s - g->sz.scale) < SCALE_EPS)
		blitCentred(screen, get(g, e, kind, true), cx, cy, 1.0f, a, 255);
	else
		blitCentred(screen, get(g, e, kind, false), cx, cy, s, a, 255);
}

// An opaque Carousel tile surface into dst's rect r at alpha a: 1:1 (UI_blitOpaque: a copy, or the lerp toward
// what's under it), or stretched (nearest, no blending: SDL's fast stretch) straight into dst at full alpha, or via
// the scratch for a partial alpha.
static void drawTileSurface(SDL_Surface* dst, SDL_Surface* s, SDL_Rect r, int a) {
	if (!s || a <= 0 || r.w <= 0 || r.h <= 0)
		return;
	if (r.w == s->w && r.h == s->h) {
		UI_blitOpaque(s, NULL, 0, 0, s->w, s->h, dst, r.x, r.y, a);
		return;
	}
	if (a >= 255) {
		SDL_BlitScaled(s, NULL, dst, &r);
		return;
	}
	SDL_Surface* tmp = stretchScratch(r.w, r.h);
	if (!tmp)
		return;
	SDL_BlitScaled(s, NULL, tmp, &(SDL_Rect){0, 0, r.w, r.h});
	UI_blitOpaque(tmp, NULL, 0, 0, r.w, r.h, dst, r.x, r.y, a);
}

// A Carousel tile centred on (cx, cy): the side one 1:1 at rest; while it changes size, the plain look with the lit
// one fading in over it, both from the centre size. The darkening is the lerp toward the black ground under the tile
// (the tiles are opaque and never overlap: the gap between them stays `gap` through the slide, on X or on Y).
static void drawCarouselItem(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy,
							 float scale, float darken, float d) {
	Uint8 c = (Uint8)(255.0f * (1.0f - darken) + 0.5f);
	if (c == 0)
		return; // black on black
	float lit = 1.0f - (d < 1.0f ? d : 1.0f);
	if (fabsf(scale - g->sz.scale) < SCALE_EPS) {
		SDL_Surface* s = carouselTile(g, e, kind, g->side_w, g->side_h, false);
		if (s)
			drawTileSurface(screen, s, (SDL_Rect){cx - s->w / 2, cy - s->h / 2, s->w, s->h}, c);
		return;
	}
	SDL_Surface* plain = lit < 1.0f - SCALE_EPS ? carouselTile(g, e, kind, g->full_w, g->full_h, false) : NULL;
	SDL_Surface* lit_s = lit > SCALE_EPS ? carouselTile(g, e, kind, g->full_w, g->full_h, true) : NULL;
	SDL_Surface* any = lit_s ? lit_s : plain;
	if (!any)
		return;
	bool exact = fabsf(scale - 1.0f) < SCALE_EPS;
	int w = exact ? any->w : (int)(any->w * scale + 0.5f);
	int h = exact ? any->h : (int)(any->h * scale + 0.5f);
	SDL_Rect r = {cx - w / 2, cy - h / 2, w, h};
	drawTileSurface(screen, plain, r, 255);
	drawTileSurface(screen, lit_s, r, plain ? (int)(lit * 255.0f + 0.5f) : 255);
	if (c < 255)
		UI_dimRect(screen, &r, (Uint8)(255 - c));
}

// A box art at a neighbour's rest size: stretched once and kept, so a rested neighbour isn't stretched every frame.
// Keyed by the art surface itself (HomeArt keeps a READY surface until it evicts it).
static SDL_Surface* sideArt(const RowGeo* g, Entry* e, SDL_Surface* art) {
	int w = (int)(art->w * g->sz.scale + 0.5f), h = (int)(art->h * g->sz.scale + 0.5f);
	if (w <= 0 || h <= 0)
		return NULL;
	char key[ITEM_KEY];
	snprintf(key, sizeof(key), "A|%d|%d|%p|%s", w, h, (void*)art, e->path);
	SDL_Surface* s;
	if (itemFind(key, &s))
		return s;
	s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s) {
		Uint8 oa, orr, og, ob;
		SDL_BlendMode bm;
		SDL_GetSurfaceAlphaMod(art, &oa);
		SDL_GetSurfaceColorMod(art, &orr, &og, &ob);
		SDL_GetSurfaceBlendMode(art, &bm);
		SDL_SetSurfaceBlendMode(art, SDL_BLENDMODE_NONE);
		SDL_SetSurfaceAlphaMod(art, 255);
		SDL_SetSurfaceColorMod(art, 255, 255, 255);
		SDL_BlitScaled(art, NULL, s, NULL); // the same nearest stretch blitCentred does per frame
		SDL_SetSurfaceAlphaMod(art, oa);
		SDL_SetSurfaceColorMod(art, orr, og, ob);
		SDL_SetSurfaceBlendMode(art, bm);
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	}
	return itemStore(key, s);
}

static void drawBackdropItem(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy,
							 float scale, float alpha) {
	if (g->kind == ROW_BACKDROP_BOX) {
		Uint8 a = (Uint8)(alpha * 255.0f + 0.5f);
		if (kind == TILE_GAME) {
			SDL_Surface* art = NULL;
			HomeArtState st = HomeArt_boxart(e->path, g->full_w, g->full_h, &art, NULL, NULL);
			if (st == HOMEART_READY && art) {
				// the shadow padding is the same on every side: the surface's centre is the art's
				SDL_Surface* side = fabsf(scale - g->sz.scale) < SCALE_EPS ? sideArt(g, e, art) : NULL;
				if (side)
					blitCentred(screen, side, cx, cy, 1.0f, a, 255);
				else
					blitCentred(screen, art, cx, cy, scale, a, 255);
				return;
			}
			if (st == HOMEART_LOADING)
				return; // blank while loading: no placeholder flashing in and out
		}
		drawRested(screen, placeholderItem, g, e, kind, scale, cx, cy, a);
		return;
	}
	drawRested(screen, slotItem, g, e, kind, scale, cx, cy, (Uint8)(alpha * 255.0f + 0.5f));
}

///////////////////////////////////////
// The caption

typedef struct {
	InfoSeg segs[3];
	int n;
} SegRow;

static int gameSegments(Entry* e, InfoSeg segs[3]) {
	GameInfo info;
	if (!GameInfo_get(e->path, &info) || !(info.has_time || info.has_ra))
		return 0;
	return GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0, info.has_time ? info.seconds : -1,
							 info.has_ra ? info.unlocked : 0, info.has_ra ? info.total : 0,
							 info.has_ra ? info.next : NULL, true, segs);
}

// The caption's look: under the row (centred, in its reserve) or beside a Vertical stack (left-aligned, §8f.4).
typedef struct {
	int w;			// the text column (px)
	int max_h;		// the most it may take (px): the reserve and the whole free lines under it, or the body
	int reserve_h;	// the reserve (the row's caption room: its rows never move); 0 beside a stack
	int name_lines; // the name's lines at most
	int gap;		// between rows (px): 0 under the row, 3 dp beside a stack
	bool left;		// left-aligned from the column's left edge, else centred
	bool shadow;	// the name's dark shadow
} CapStyle;

#define CAP_LINES (2 + CAPTION_FIT_NEXT_LINES) // info lines: time, trophy and Next's own lines

static int captionMeasure(void* ctx, const char* text) {
	return textW((TTF_Font*)ctx, text);
}

// The info rows as drawn lines: a row ending in a Next too long for the column gives Next its own lines, out of the
// whole free lines past the reserve (caption_fit.h). Returns the line count; *h the block's height with the name's.
static int captionLines(const CapStyle* cs, int name_h, const SegRow* rows, int nrows, SegRow* out, int* h) {
	TTF_Font* fi = UIFont_get(CAPTION_INFO_SP, false);
	int ih = fi ? TTF_FontHeight(fi) : 0;
	int n = 0, base = name_h;
	for (int r = 0; r < nrows; r++) {
		if (rows[r].n > 0)
			base += (base > 0 ? cs->gap : 0) + ih;
	}
	int used = base > cs->reserve_h ? base : cs->reserve_h;
	int free_lines = ih > 0 && cs->max_h > used ? (cs->max_h - used) / (ih + cs->gap) : 0;
	int sep_w = InfoBand_separatorWidth(fi), trophy_w = InfoBand_trophyWidth(fi);
	*h = name_h;
	for (int r = 0; r < nrows && fi; r++) {
		if (rows[r].n <= 0)
			continue; // a row without data is left out
		CaptionFitRow fit;
		bool split = CaptionFit_split(rows[r].segs, rows[r].n, cs->w, sep_w, trophy_w, free_lines, captionMeasure, fi,
									  &fit);
		if (n < CAP_LINES) {
			out[n] = rows[r];
			out[n].n = split ? fit.head_n : rows[r].n;
			n++;
			*h += (*h > 0 ? cs->gap : 0) + ih;
		}
		for (int k = 0; split && k < fit.next_lines && n < CAP_LINES; k++) {
			out[n].n = 1;
			out[n].segs[0].kind = INFO_SEG_NEXT; // white
			snprintf(out[n].segs[0].text, sizeof(out[n].segs[0].text), "%s", fit.next[k]);
			n++;
			*h += cs->gap + ih;
		}
		if (split)
			free_lines -= fit.next_lines;
	}
	return n;
}

static SDL_Surface* captionSurface(const CapStyle* cs, const char* name, const SegRow* rows, int nrows) {
	char key[CAPTION_KEY];
	int n = snprintf(key, sizeof(key), "%d|%d|%d|%d|%d|%d|%d|%d|%s|", (int)FIXED_SCALE, cs->w, cs->max_h,
					 cs->reserve_h, cs->name_lines, cs->gap, cs->left, cs->shadow, name ? name : "");
	for (int r = 0; r < nrows && n > 0 && (size_t)n < sizeof(key); r++) {
		for (int i = 0; i < rows[r].n && n > 0 && (size_t)n < sizeof(key); i++)
			n += snprintf(key + n, sizeof(key) - n, "%d:%s|", (int)rows[r].segs[i].kind, rows[r].segs[i].text);
		if (n > 0 && (size_t)n < sizeof(key))
			n += snprintf(key + n, sizeof(key) - n, "/");
	}
	if (caption.s && strcmp(caption.key, key) == 0)
		return caption.s;
	if (caption.s)
		SDL_FreeSurface(caption.s);
	caption.s = NULL;
	caption.content_h = 0;
	caption.ink = (SDL_Rect){0, 0, 0, 0};
	snprintf(caption.key, sizeof(caption.key), "%s", key);

	// the layout first (a font is only good until a later UIFont_get: each is fetched where it's used)
	int w = cs->w;
	bool has_name = name && name[0];
	TTF_Font* fn = UIFont_get(CAPTION_NAME_SP, false);
	int name_h = fn && has_name ? (cs->left ? Tiles_textBlockLeft(NULL, fn, name, 0, 0, w, cs->name_lines, 255, false)
											: Tiles_textBlock(NULL, fn, name, w / 2, 0, w, cs->name_lines, false,
															  255, 255, false))
								: 0;
	SegRow lines[CAP_LINES];
	int content_h = 0;
	int nlines = captionLines(cs, name_h, rows, nrows, lines, &content_h);
	int h = content_h < cs->max_h ? content_h : cs->max_h;
	caption.content_h = h;
	caption.s = newSurface(w, h, false);
	if (!caption.s)
		return NULL;

	int y = 0;
	fn = UIFont_get(CAPTION_NAME_SP, false);
	if (fn && has_name) {
		y += cs->left ? Tiles_textBlockLeft(caption.s, fn, name, 0, 0, w, cs->name_lines, 255, cs->shadow)
					  : Tiles_textBlock(caption.s, fn, name, w / 2, 0, w, cs->name_lines, false, 255, 255, cs->shadow);
	}
	TTF_Font* fi = UIFont_get(CAPTION_INFO_SP, false);
	for (int l = 0; fi && l < nlines; l++) {
		int ly = y + (y > 0 ? cs->gap : 0);
		if (ly + TTF_FontHeight(fi) > h)
			break; // past the room it may take
		int tw = InfoBand_segmentsWidth(lines[l].segs, lines[l].n, w, fi);
		if (tw <= 0)
			continue;
		// under the row the info keeps its shadow; beside the stack it follows the name's (Backdrop-Vertical only)
		InfoBand_drawSegmentsEx(caption.s, lines[l].segs, lines[l].n, cs->left ? 0 : (w - tw) / 2, false, ly, w, fi,
								!cs->left || cs->shadow);
		y = ly + TTF_FontHeight(fi);
	}
	// the ink's bounding box: a frame blends only that (SDL's per-pixel blend costs per pixel, clear ones too)
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for (int yy = 0; yy < h; yy++) {
		const Uint32* row = (const Uint32*)((const Uint8*)caption.s->pixels + yy * caption.s->pitch);
		for (int xx = 0; xx < w; xx++) {
			if (!(row[xx] >> 24))
				continue;
			if (xx < x0)
				x0 = xx;
			if (xx > x1)
				x1 = xx;
			if (yy < y0)
				y0 = yy;
			y1 = yy;
		}
	}
	caption.ink = x1 < 0 ? (SDL_Rect){0, 0, 0, 0} : (SDL_Rect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
	return caption.s;
}

// A game's info rows: the Carousel's one full line (time · n of m · Next), or Backdrop's two, the time then "n of m ·
// Next: …" (beside a stack too, §8f.4). None for a folder or a tool: the name alone.
static int captionRows(Entry* e, TileKind kind, bool one_line, SegRow rows[2]) {
	memset(rows, 0, sizeof(SegRow) * 2);
	if (kind != TILE_GAME)
		return 0;
	InfoSeg segs[3];
	int n = gameSegments(e, segs);
	if (one_line) {
		memcpy(rows[0].segs, segs, sizeof(InfoSeg) * n);
		rows[0].n = n;
		return 1;
	}
	for (int i = 0; i < n; i++) {
		SegRow* row = (segs[i].kind == INFO_SEG_ACH || segs[i].kind == INFO_SEG_NEXT) ? &rows[1] : &rows[0];
		row->segs[row->n++] = segs[i];
	}
	return 2;
}

static void blitCaption(SDL_Surface* screen, SDL_Surface* s, int x, int y) {
	if (s && caption.ink.w > 0)
		SDL_BlitSurface(s, &caption.ink, screen,
						&(SDL_Rect){x + caption.ink.x, y + caption.ink.y, caption.ink.w, caption.ink.h});
}

static void drawCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind) {
	if (g->cap == CAP_NONE || g->cap_draw_h <= 0)
		return;
	bool carousel = g->kind == ROW_CAROUSEL;
	SegRow rows[2];
	int nrows = captionRows(e, kind, carousel, rows);
	// a long Next may take the whole free lines under the reserve (none when the block was clamped)
	TTF_Font* fi = UIFont_get(CAPTION_INFO_SP, false);
	int ih = fi ? TTF_FontHeight(fi) : 0;
	int below = screen->h - barPx() - (g->cap_y + g->cap_h);
	int free_lines = ih > 0 && g->cap_draw_h == g->cap_h && below > 0 ? below / ih : 0;
	if (free_lines > CAPTION_FIT_NEXT_LINES)
		free_lines = CAPTION_FIT_NEXT_LINES;
	CapStyle cs = {g->cx * 2 - 2 * NX_DPF(CAPTION_GUTTER_DP),
				   g->cap_draw_h + free_lines * ih,
				   g->cap_h,
				   carousel ? 2 : 1,
				   0,
				   false,
				   true};
	SDL_Surface* s = captionSurface(&cs, displayName(e), rows, nrows);
	blitCaption(screen, s, g->cx - cs.w / 2, g->cap_y);
}

// Beside a Vertical stack (§8f.4): Backdrop's three rows for both renderings, 3 dp apart, the name on up to two lines;
// it grows with a long Next (up to the body) and stays centred on the selection.
static void drawSideCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int x, int w, int cy,
							int body_top, int body_h) {
	if (w <= 0 || body_h <= 0)
		return;
	SegRow rows[2];
	int nrows = captionRows(e, kind, false, rows);
	CapStyle cs = {w, body_h, 0, 2, NX_DPF(STACK_CAPTION_LINE_GAP_DP), true, g->kind == ROW_BACKDROP_BOX};
	SDL_Surface* s = captionSurface(&cs, displayName(e), rows, nrows);
	if (!s)
		return;
	int top = cy - caption.content_h / 2;
	if (top + caption.content_h > body_top + body_h)
		top = body_top + body_h - caption.content_h;
	if (top < body_top)
		top = body_top;
	blitCaption(screen, s, x, top);
}

///////////////////////////////////////
// The main-menu count line

// The "N games" text in the accent (opaque) at sp: the one cached surface, rebuilt when the text, size or colour
// changes. NULL for an empty text.
static SDL_Surface* countSurface(const char* text, float sp) {
	if (!text || !text[0])
		return NULL;
	SDL_Color ac = UI_accent();
	char key[sizeof(cnt.key)];
	snprintf(key, sizeof(key), "%d|%02x%02x%02x|%s", NX_SP(sp), ac.r, ac.g, ac.b, text);
	if (cnt.s && strcmp(cnt.key, key) == 0)
		return cnt.s;
	if (cnt.s)
		SDL_FreeSurface(cnt.s);
	snprintf(cnt.key, sizeof(cnt.key), "%s", key);
	TTF_Font* f = UIFont_get(sp, false);
	cnt.s = f ? GFX_renderText(f, text, (SDL_Color){ac.r, ac.g, ac.b, 255}) : NULL;
	if (cnt.s)
		SDL_SetSurfaceBlendMode(cnt.s, SDL_BLENDMODE_BLEND);
	return cnt.s;
}

// Consoles: the selected console's logo as buildSlot draws it in its slot (MenuArt's fit), or NULL (no logo).
static SDL_Surface* logoArt(const RowGeo* g, Entry* e, TileKind kind) {
	char logo[64];
	const char* file = kind == TILE_LOGO ? entryLogoFile(e, logo, sizeof(logo)) : NULL;
	return file ? MenuArt_get(file, g->full_w, g->full_h) : NULL;
}

// Consoles: the count line's top (px), 8 dp under what the selected item draws in its slot: the logo `art` (its own
// aspect in the slot), or a logo-less console's name.
static int logoCountTop(const RowGeo* g, Entry* e, const SDL_Surface* art) {
	int drawn = art ? art->h : logoName(NULL, displayName(e), g->full_w, 0, g->k, 1.0f);
	return (int)floorf(Row_logoCountY((float)g->cy, (float)drawn, (float)NX_DPF(ROW_LOGO_COUNT_GAP_DP)) + 0.5f);
}

// A Vertical stack's Consoles count belongs to its item (stackview.c, device fix round 2): up to two items show one
// at once (the outgoing and incoming selection), so their texts are kept apart, a few at a time, never re-rendered per
// frame while the stack slides.
#define ITEM_COUNT_SLOTS 4
static struct {
	char key[96];
	SDL_Surface* s;
	unsigned stamp;
} item_counts[ITEM_COUNT_SLOTS];
static unsigned item_count_clock = 0;

static SDL_Surface* itemCountSurface(const char* text, float sp) {
	if (!text || !text[0])
		return NULL;
	SDL_Color ac = UI_accent();
	char key[sizeof(item_counts[0].key)];
	snprintf(key, sizeof(key), "%d|%02x%02x%02x|%s", NX_SP(sp), ac.r, ac.g, ac.b, text);
	int victim = 0;
	for (int i = 0; i < ITEM_COUNT_SLOTS; i++) {
		if (item_counts[i].s && strcmp(item_counts[i].key, key) == 0) {
			item_counts[i].stamp = ++item_count_clock;
			return item_counts[i].s;
		}
		if (item_counts[i].stamp < item_counts[victim].stamp)
			victim = i;
	}
	if (item_counts[victim].s)
		SDL_FreeSurface(item_counts[victim].s);
	snprintf(item_counts[victim].key, sizeof(item_counts[victim].key), "%s", key);
	TTF_Font* f = UIFont_get(sp, false);
	SDL_Surface* t = f ? GFX_renderText(f, text, (SDL_Color){ac.r, ac.g, ac.b, 255}) : NULL;
	if (t)
		SDL_SetSurfaceBlendMode(t, SDL_BLENDMODE_BLEND);
	item_counts[victim].s = t;
	item_counts[victim].stamp = ++item_count_clock;
	return t;
}

static void itemCountsClear(void) {
	for (int i = 0; i < ITEM_COUNT_SLOTS; i++) {
		if (item_counts[i].s)
			SDL_FreeSurface(item_counts[i].s);
	}
	memset(item_counts, 0, sizeof(item_counts));
	item_count_clock = 0;
}

// A Vertical stack's Consoles "N games" on item e, centred at (cx, cy) at its live scale, d steps from the position
// (Stack_countOn): 8 dp under its logo as drawn (or a logo-less console's name, at least a half-slot "logo"), scaled
// with the item and fading 1 − d, so it rides with its own logo instead of sitting in the selection's slot.
static void drawItemCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
						  float d) {
	if (g->kind != ROW_BACKDROP_LOGO)
		return;
	char text[32];
	countLabel(e, kind, text, sizeof(text));
	if (!text[0])
		return;
	SDL_Surface* art = logoArt(g, e, kind);
	int drawn = art ? art->h : logoName(NULL, displayName(e), g->full_w, 0, g->k, 1.0f);
	if (!art && g->vertical && drawn < g->full_h / 2)
		drawn = g->full_h / 2;
	StackCount c = Stack_countOn((float)cy, scale, (float)drawn, (float)NX_DPF(ROW_LOGO_COUNT_GAP_DP), d);
	Uint8 a = (Uint8)(255.0f * c.alpha + 0.5f);
	SDL_Surface* s = a ? itemCountSurface(text, Row_countSp(g->k)) : NULL;
	if (!s)
		return;
	bool exact = fabsf(scale - 1.0f) < SCALE_EPS;
	int h = exact ? s->h : (int)(s->h * scale + 0.5f);
	// blitCentred puts the top at centre − h/2: the line's top lands exactly on c.top (settled: logoCountTop's value)
	blitCentred(screen, s, cx, Stack_round(c.top) + h / 2, scale, a, 255);
}

// Consoles: send the line's top to y, gliding from where it is now (at once when `at_once`).
static void retargetCount(int y, bool at_once) {
	if (at_once || !animationsOn()) {
		cnt.y_from = cnt.y_to = y;
		cnt.glide.active = false;
		return;
	}
	if (y == cnt.y_to)
		return;
	float p = cnt.glide.active ? UI_easeStandard(tweenProgress(&cnt.glide, ROW_COUNT_GLIDE_MS)) : 1.0f;
	cnt.y_from = (int)floorf(cnt.y_from + (cnt.y_to - cnt.y_from) * p + 0.5f);
	cnt.y_to = y;
	tweenStart(&cnt.glide);
}

// The line's place for a new selection: Consoles' top glides there from where it is (snapped on a new list, tab or
// screen), and again whenever the selected logo's surface changes (it loads, or reloads at another size);
// Collections' fades in on the selected item once its count is known (`known`: it is this frame).
static void placeCount(const RowGeo* g, Entry* e, TileKind kind, int sel, bool snap, bool known) {
	if (g->kind == ROW_BACKDROP_LOGO) {
		SDL_Surface* art = logoArt(g, e, kind);
		bool art_changed = art != cnt.art || (art && (art->w != cnt.art_w || art->h != cnt.art_h));
		if (!snap && sel == cnt.sel && !art_changed)
			return;
		retargetCount(logoCountTop(g, e, art), snap || cnt.sel < 0);
		cnt.art = art;
		cnt.art_w = art ? art->w : 0;
		cnt.art_h = art ? art->h : 0;
	} else {
		if (!snap && sel == cnt.sel)
			return;
		CollLayout c = collLayout(displayName(e), g->full_w, g->full_h, g->k, 1.0f, g->vertical);
		TTF_Font* fc = UIFont_get(Row_countSp(g->k), false);
		int ch = fc ? TTF_FontHeight(fc) : 0;
		cnt.off = c.t.count_y + ch / 2 - g->full_h / 2;
		cnt.fade.active = false;
		// a new list or tab shows a known count at once; a new selection, or a count still unknown, fades in from
		// the frame it is first known (drawCount), so a count that arrives late never pops in
		cnt.fade_pending = !(snap && known);
	}
	cnt.sel = sel;
}

// The main-menu Carousel's "N games" for the selection: none while the count is unknown. It dims with the rest of
// the content (contentdim) while the tab row has focus.
static void drawCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int sel, const RowPlace* at,
					  bool snap) {
	if (g->kind != ROW_BACKDROP_LOGO && g->kind != ROW_BACKDROP_COLL) {
		cnt.sel = -1;
		return;
	}
	char text[32];
	countLabel(e, kind, text, sizeof(text));
	placeCount(g, e, kind, sel, snap, text[0] != '\0');
	if (!text[0])
		return;
	if (g->kind == ROW_BACKDROP_COLL && cnt.fade_pending) { // the selection's count is known from this frame: fade
		cnt.fade_pending = false;
		tweenStart(&cnt.fade); // animations off: inactive, so it shows at once
	}
	if (g->kind == ROW_BACKDROP_LOGO) {
		SDL_Surface* s = countSurface(text, Row_countSp(g->k));
		if (!s)
			return;
		float p = cnt.glide.active ? UI_easeStandard(tweenProgress(&cnt.glide, ROW_COUNT_GLIDE_MS)) : 1.0f;
		int y = (int)floorf(cnt.y_from + (cnt.y_to - cnt.y_from) * p + 0.5f);
		blitCentred(screen, s, g->cx, y + s->h / 2, 1.0f, 255, 255);
		return;
	}
	// Collections: on the selected item, wherever the slide has it, at its scale and alpha
	SDL_Surface* s = countSurface(text, Row_countSp(g->k));
	if (!s || !at->visible)
		return;
	float fade = cnt.fade.active ? UI_easeStandard(tweenProgress(&cnt.fade, ROW_COUNT_FADE_MS)) : 1.0f;
	float a = fade * at->alpha;
	int y = g->cy + at->dy + (int)floorf(cnt.off * at->scale + 0.5f);
	blitCentred(screen, s, g->cx + at->dx, y, at->scale, (Uint8)(255.0f * a + 0.5f), 255);
}

///////////////////////////////////////

bool RowView_active(void) {
	int style = GameList_currentStyle();
	return (style == MENU_STYLE_CAROUSEL || style == MENU_STYLE_BACKDROP) && !Home_active();
}

bool RowView_backdropPicture(void) {
	return pic_on;
}

static void resetPicture(void) {
	pic_on = false;
	from_valid = false;
	to_set = false;
	fade_started = false;
	fade_tw.active = false;
	to_path[0] = '\0';
	pic_top = 0;
	if (pic_from) { // 3-4 MB: only held on a Backdrop game row
		SDL_FreeSurface(pic_from);
		pic_from = NULL;
	}
}

static float fadeProgress(void) {
	return fade_started ? UI_easeStandard(tweenProgress(&fade_tw, FADE_MS)) : 0.0f;
}

// The target is about to change: bake what is shown now (from, and the old target at the fade's progress) into
// the from layer, so the next crossfade starts from exactly this picture.
static void settlePicture(SDL_Surface* screen) {
	if (!fade_started)
		return; // the old target never showed: the from layer is still what's on screen
	float p = fadeProgress();
	SDL_Surface* prev = NULL;
	HomeArtState st = to_path[0] ? HomeArt_backdrop(to_path, screen->w, screen->h, &prev) : HOMEART_NONE;
	bool prev_ok = st == HOMEART_READY && prev;
	if (p >= 1.0f && !prev_ok) {
		from_valid = false; // fully faded to black
		return;
	}
	if (!pic_from || pic_from->w != screen->w || pic_from->h != screen->h) {
		if (pic_from)
			SDL_FreeSurface(pic_from);
		pic_from = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
		from_valid = false;
		if (!pic_from)
			return;
		SDL_SetSurfaceBlendMode(pic_from, SDL_BLENDMODE_NONE);
	}
	if (p >= 1.0f) {
		SDL_BlendMode bm;
		SDL_GetSurfaceBlendMode(prev, &bm);
		SDL_SetSurfaceBlendMode(prev, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(prev, NULL, pic_from, NULL);
		SDL_SetSurfaceBlendMode(prev, bm);
		from_valid = true;
		return;
	}
	if (!from_valid)
		SDL_FillRect(pic_from, NULL, SDL_MapRGBA(pic_from->format, 0, 0, 0, 255));
	Uint8 a = (Uint8)(p * 255.0f + 0.5f);
	if (prev_ok && prev->w == pic_from->w && prev->h == pic_from->h)
		UI_blitOpaque(prev, NULL, 0, 0, prev->w, prev->h, pic_from, 0, 0, a); // both opaque
	else if (prev_ok)
		blitCentred(pic_from, prev, pic_from->w / 2, pic_from->h / 2, 1.0f, a, 255);
	else
		blackOver(pic_from, a);
	from_valid = true;
}

// The picture is a game list's (Backdrop box row) only: a main-menu tab never has one.
static bool pictureRow(void) {
	return top && stack->count > 1 && RowView_active() && currentKind() == ROW_BACKDROP_BOX;
}

bool RowView_paintsScreen(void) {
	return pictureRow();
}

bool RowView_renderPicture(SDL_Surface* screen) {
	pic_on = false;
	if (!screen || !pictureRow()) {
		if (to_set || pic_from)
			resetPicture();
		return false;
	}
	int n = top->entries->count;
	syncKinds(n);
	// a new list: the picture starts from black (no crossfade from another list's game). Keyed on the Directory
	// alone: only a game list has a picture, and the tab generation moves only at the root (a reload under an open
	// game list keeps the same list, and its picture).
	if (top->serial != pic_top) {
		resetPicture();
		pic_top = top->serial;
	}
	const char* want = "";
	if (n > 0) {
		int sel = selectedIndex(n);
		Entry* e = top->entries->items[sel];
		if (kindFor(sel, e) == TILE_GAME)
			want = e->path;
	}
	if (!to_set || strcmp(want, to_path) != 0) {
		if (to_set)
			settlePicture(screen);
		snprintf(to_path, sizeof(to_path), "%s", want);
		to_set = true;
		fade_started = false;
		fade_tw.active = false;
	}

	SDL_Surface* to = NULL;
	HomeArtState st = to_path[0] ? HomeArt_backdrop(to_path, screen->w, screen->h, &to) : HOMEART_NONE;
	if (st != HOMEART_LOADING && !fade_started) {
		fade_started = true;
		tweenStart(&fade_tw); // animations off: inactive, so the progress is 1 at once
	}
	float p = fadeProgress();
	bool to_ok = st == HOMEART_READY && to;
	bool from_ok = from_valid && pic_from && pic_from->w == screen->w && pic_from->h == screen->h;
	Uint8 a = (Uint8)(p * 255.0f + 0.5f);
	// Both layers are prepared opaque full-screen surfaces: the crossfade is one exact lerp pass of the two straight
	// into the screen (UI_blitOpaque), not a copy and an SDL alpha blit over it.
	if (to_ok && p > 0.0f && to->w == screen->w && to->h == screen->h) {
		if (p < 1.0f && from_ok) {
			UI_blitOpaque(to, pic_from, 0, 0, to->w, to->h, screen, 0, 0, a);
		} else {
			if (p < 1.0f)
				SDL_FillRect(screen, NULL, SDL_MapRGBA(screen->format, 0, 0, 0, 255));
			UI_blitOpaque(to, NULL, 0, 0, to->w, to->h, screen, 0, 0, p >= 1.0f ? 255 : a);
		}
	} else {
		if (p < 1.0f) {
			if (from_ok)
				UI_blitOpaque(pic_from, NULL, 0, 0, pic_from->w, pic_from->h, screen, 0, 0, 255);
			else
				SDL_FillRect(screen, NULL, SDL_MapRGBA(screen->format, 0, 0, 0, 255));
		} else if (to_ok) {
			SDL_FillRect(screen, NULL, SDL_MapRGBA(screen->format, 0, 0, 0, 255)); // every pixel painted
		}
		if (p > 0.0f) {
			if (to_ok)
				blitCentred(screen, to, screen->w / 2, screen->h / 2, 1.0f, a, 255); // another size (not expected)
			else
				blackOver(screen, a);
		}
	}
	pic_on = (p < 1.0f && from_valid && pic_from) || (to_ok && p > 0.0f);
	return pic_on;
}

///////////////////////////////////////
// Leaving a Backdrop game list (B)

bool RowView_beginExit(void) {
	MenuTransition_exitCancel(&exit_fade);
	// a picture on screen now (the last frame's) that is this list's own: with none the screen is already black and
	// the list just closes. A second B mid-fade lands here on the parent before it ever drew: pic_on still tells of
	// the child's picture, so pic_top must match too, or the parent would fade from a black frame (a row flash).
	if (!pictureRow() || !pic_on || pic_top != top->serial)
		return false;
	exit_top = top->serial;
	return MenuTransition_exitBegin(&exit_fade, SDL_GetTicks(), true, animationsOn());
}

bool RowView_exiting(void) {
	// the list went away under the fade (nothing expected: input waits for it): drop it
	if (exit_fade.active && (!pictureRow() || top->serial != exit_top))
		MenuTransition_exitCancel(&exit_fade);
	return exit_fade.active;
}

bool RowView_pictureBusy(void) {
	return fade_tw.active || exit_fade.active;
}

bool RowView_exitStep(bool key_pressed) {
	return RowView_exiting() && MenuTransition_exitStep(&exit_fade, SDL_GetTicks(), key_pressed);
}

void RowView_renderExit(SDL_Surface* screen) {
	if (!screen || !RowView_exiting())
		return;
	float d = MenuTransition_exitDarkness(&exit_fade, SDL_GetTicks());
	if (d > 0.0f)
		UI_dimRect(screen, NULL, (Uint8)(d * 255.0f + 0.5f)); // one pass, only while the fade runs
}

// A list, tab, screen size, scale or kind change: start over with the row snapped to the selection.
static bool syncList(SDL_Surface* screen, int n, int lastScreen, RowKind kind) {
	bool changed = top->serial != seen_top || MenuTabs_generation() != seen_gen || n != seen_n ||
				   screen->w != seen_screen_w || (int)FIXED_SCALE != seen_scale || (int)kind != seen_kind ||
				   lastScreen != SCREEN_GAMELIST;
	if (!changed)
		return false;
	seen_top = top->serial;
	seen_gen = MenuTabs_generation();
	seen_n = n;
	seen_screen_w = screen->w;
	seen_scale = (int)FIXED_SCALE;
	seen_kind = (int)kind;
	return true;
}

static bool prefetchItems(const RowGeo* g, int n, int sel);

// One item ahead: a Carousel tile (plain or lit), a box art's neighbour-size copy or the placeholder box, a slot.
static void prefetchOne(const RowGeo* g, Entry* e, TileKind k, bool side, bool lit) {
	if (g->kind == ROW_CAROUSEL) {
		carouselTile(g, e, k, side ? g->side_w : g->full_w, side ? g->side_h : g->full_h, lit);
	} else if (lit) {
		return; // a Carousel look only
	} else if (g->kind == ROW_BACKDROP_BOX) {
		SDL_Surface* art = NULL;
		HomeArtState st =
			k == TILE_GAME ? HomeArt_boxart(e->path, g->full_w, g->full_h, &art, NULL, NULL) : HOMEART_NONE;
		if (st == HOMEART_READY && art) {
			if (side)
				sideArt(g, e, art);
		} else if (st != HOMEART_LOADING) {
			placeholderItem(g, e, k, side);
		}
	} else {
		slotItem(g, e, k, side);
	}
}

void RowView_render(SDL_Surface* screen, int lastScreen) {
	prefetch_pending = false;
	if (!screen || !top)
		return;
	int bar = barPx();
	int body_h = screen->h - 2 * bar;
	RowKind kind = currentKind();
	// plain black under the row (and under the hint bar's scrim); a Backdrop game list's picture is already drawn
	if (!pictureRow())
		SDL_FillRect(screen, &(SDL_Rect){0, bar, screen->w, screen->h - bar},
					 SDL_MapRGBA(screen->format, 0, 0, 0, 255));

	// the Vertical orientation (§8f) draws its own stack over the same black; the row snaps when it shows again
	if (StackView_active()) {
		seen_top = 0;
		slide_tw.active = false;
		pos_from = pos_to;
		StackView_render(screen, lastScreen);
		return;
	}
	StackView_forget();

	int n = top->entries->count;
	syncKinds(n);
	bool snap = syncList(screen, n, lastScreen, kind);
	if (n <= 0 || body_h <= 0) {
		slide_tw.active = false;
		pos_from = pos_to = 0;
		UI_renderCenteredMessage(screen, "Empty folder");
		return;
	}

	int sel = selectedIndex(n);
	// the slide toward the selection, retargeted from where the row is now
	if (snap || !animationsOn()) {
		pos_from = pos_to = (float)sel;
		slide_tw.active = false;
	} else if ((float)sel != pos_to) {
		pos_from = currentPos();
		pos_to = (float)sel;
		tweenStart(&slide_tw);
	}
	float pos = currentPos();

	RowGeo g;
	computeGeo(screen, kind, &g);

	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &(SDL_Rect){0, bar, screen->w, body_h});

	// far to near (the largest d first), so the centre lands on top
	int first, last;
	Row_visibleRange(n, pos, &first, &last);
	int order[16], count = 0;
	for (int i = first; i <= last && count < 16; i++)
		order[count++] = i;
	for (int a = 0; a < count; a++) {
		for (int b = a + 1; b < count; b++) {
			if (fabsf(order[b] - pos) > fabsf(order[a] - pos)) {
				int t = order[a];
				order[a] = order[b];
				order[b] = t;
			}
		}
	}
	for (int j = 0; j < count; j++) {
		int i = order[j];
		RowItem it = Row_item(&g.sz, kind, (float)i, pos);
		if (!it.visible)
			continue;
		Entry* e = top->entries->items[i];
		TileKind k = kindFor(i, e);
		float d = fabsf((float)i - pos);
		int cx = g.cx + dpToPx(it.dx);
		if (kind == ROW_CAROUSEL)
			drawCarouselItem(screen, &g, e, k, cx, g.cy, it.scale, it.darken, d);
		else
			drawBackdropItem(screen, &g, e, k, cx, g.cy, it.scale, it.alpha);
	}

	// the selection's caption (its game info requested last: GameInfo's queue keeps the latest request)
	Entry* e = top->entries->items[sel];
	drawCaption(screen, &g, e, kindFor(sel, e));
	RowItem sel_it = Row_item(&g.sz, kind, (float)sel, pos);
	RowPlace at = {dpToPx(sel_it.dx), 0, sel_it.scale, sel_it.alpha, sel_it.visible};
	drawCount(screen, &g, e, kindFor(sel, e), sel, &at, snap);

	SDL_SetClipRect(screen, &prev_clip);

	// settled: build ahead what the next step needs (one item a frame; GameList keeps asking while there's more)
	prefetch_pending = !slide_tw.active && !RowView_pictureBusy() && prefetchItems(&g, n, sel);
}

// The items the next step in either direction draws first: the neighbours at the centre size (they grow into the
// centre; plain and lit for a Carousel tile) and the items that come into view at d = 4 (a neighbour's size). Builds
// at most one; true when it stopped with more to build.
static bool prefetchItems(const RowGeo* g, int n, int sel) {
	unsigned start = item_builds;
	const struct {
		int di;
		bool side, lit;
	} jobs[] = {{1, false, false}, {1, false, true}, {-1, false, false}, {-1, false, true}, {4, true, false}, {-4, true, false}};
	for (size_t j = 0; j < sizeof(jobs) / sizeof(jobs[0]); j++) {
		int i = sel + jobs[j].di;
		if (i < 0 || i >= n)
			continue;
		if (item_builds != start)
			return true;
		Entry* e = top->entries->items[i];
		prefetchOne(g, e, kindFor(i, e), jobs[j].side, jobs[j].lit);
	}
	return false;
}

bool RowView_handleInput(unsigned long now, bool* dirty) {
	if (StackView_active())
		return StackView_handleInput(dirty); // the Vertical orientation: UP/DOWN step (LEFT/RIGHT: tab on the main menu)
	// UP/DOWN don't move the selection (handled, so the List code below doesn't either); in the main menu a fresh UP
	// focuses the tab row (the row is one row: any item is its top)
	if (PAD_justRepeated(BTN_UP) || PAD_justRepeated(BTN_DOWN)) {
		if (stack->count == 1 && PAD_justPressed(BTN_UP)) {
			MenuTabs_setFocused(true);
			*dirty = true;
		}
		return true;
	}
	int btn, dir;
	if (PAD_justRepeated(BTN_LEFT)) {
		btn = BTN_LEFT;
		dir = -1;
	} else if (PAD_justRepeated(BTN_RIGHT)) {
		btn = BTN_RIGHT;
		dir = 1;
	} else {
		return false;
	}

	int n = top->entries->count;
	int sel = n > 0 ? selectedIndex(n) : 0;
	int next = sel + dir;
	if (n > 0 && next >= 0 && next < n) {
		top->selected = next;
		// keep the List window consistent for a later switch back to List: the selection at the top, clamped
		int rows = GameList_rowCount();
		top->start = next;
		top->end = top->start + rows < n ? top->start + rows : n;
		if (top->end - top->start < rows) {
			top->start = top->end - rows;
			if (top->start < 0)
				top->start = 0;
		}
		*dirty = true;
	} else if (stack->count == 1 && PAD_justPressed(btn)) {
		// the main menu: past an end, a fresh press switches tab; a held key stops (game lists always stop)
		GameList_switchTab(dir, dirty);
	}
	return true;
}

bool RowView_animating(void) {
	if (!RowView_active()) {
		prefetch_pending = false;
		slide_tw.active = false;
		pos_from = pos_to;
		fade_tw.active = false;
		cnt.glide.active = cnt.fade.active = false;
		cnt.fade_pending = false;
		cnt.sel = -1;
		StackView_forget();
		return false;
	}
	if (StackView_active()) {
		// the stack's own slide and prefetch, the count line's glide and fade (shared), and a Backdrop-Vertical's
		// picture: its crossfade and B's fade out (the picture is the row's, whichever orientation draws over it)
		slide_tw.active = false;
		pos_from = pos_to;
		prefetch_pending = false;
		bool b = tweenTick(&fade_tw, FADE_MS);
		bool c = tweenTick(&cnt.glide, ROW_COUNT_GLIDE_MS);
		bool d = tweenTick(&cnt.fade, ROW_COUNT_FADE_MS);
		bool s = StackView_animating();
		return b || c || d || s || RowView_exiting();
	}
	bool a = tweenTick(&slide_tw, SLIDE_MS);
	bool b = tweenTick(&fade_tw, FADE_MS);
	bool c = tweenTick(&cnt.glide, ROW_COUNT_GLIDE_MS);
	bool d = tweenTick(&cnt.fade, ROW_COUNT_FADE_MS);
	if (a && !slide_tw.active)
		pos_from = pos_to;
	return a || b || c || d || prefetch_pending || RowView_exiting();
}

///////////////////////////////////////
// Shared with the Vertical orientation (rowview_shared.h)

void RowView_syncKinds(int n) {
	syncKinds(n);
}

TileKind RowView_kindFor(int index, Entry* e) {
	return kindFor(index, e);
}

SDL_Surface* RowView_slotItem(const RowGeo* g, Entry* e, TileKind kind, bool side) {
	return slotItem(g, e, kind, side);
}

void RowView_blitItem(SDL_Surface* dst, SDL_Surface* s, int cx, int cy, float factor, Uint8 a) {
	blitCentred(dst, s, cx, cy, factor, a, 255);
}

unsigned RowView_itemBuilds(void) {
	return item_builds;
}

void RowView_drawCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int sel, const RowPlace* at,
					   bool snap) {
	drawCount(screen, g, e, kind, sel, at, snap);
}

void RowView_drawGameItem(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
						  float alpha, float darken, float d) {
	if (g->kind == ROW_CAROUSEL)
		drawCarouselItem(screen, g, e, kind, cx, cy, scale, darken, d);
	else
		drawBackdropItem(screen, g, e, kind, cx, cy, scale, alpha);
}

void RowView_prefetchItem(const RowGeo* g, Entry* e, TileKind kind, bool side, bool lit) {
	prefetchOne(g, e, kind, side, lit);
}

void RowView_drawSideCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int x, int w, int cy,
							 int body_top, int body_h) {
	drawSideCaption(screen, g, e, kind, x, w, cy, body_top, body_h);
}

void RowView_drawItemCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
						   float d) {
	drawItemCount(screen, g, e, kind, cx, cy, scale, d);
}

void RowView_quit(void) {
	free(kinds);
	kinds = NULL;
	kinds_cap = 0;
	kinds_top = 0;
	kinds_n = -1;
	itemsClear();
	if (caption.s)
		SDL_FreeSurface(caption.s);
	memset(&caption, 0, sizeof(caption));
	if (cnt.s)
		SDL_FreeSurface(cnt.s);
	memset(&cnt, 0, sizeof(cnt));
	cnt.sel = -1;
	resetPicture();
	MenuTransition_exitCancel(&exit_fade);
	if (stretch_scratch)
		SDL_FreeSurface(stretch_scratch);
	stretch_scratch = NULL;
	StackView_forget();
	itemCountsClear();
}
