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
//
// GPU sprites (RowView_beginSprites): the Consoles row and the game lists' Carousel and Backdrop rows (both
// orientations) hand those same cached surfaces to the GPU instead of blending them into the screen. Each surface gets
// its texture once (PLAT_textureForSurface, freed with it: freeSurf), the GPU scales it while it changes size (linear)
// and fades it (a Carousel tile's darkening is its alpha over the black ground, and the black ground over it while it
// crossfades its lit look), and the caption is a sprite too. A Backdrop game list's picture leaves the CPU as well: its
// layers are textures of their own under the screen (PLAT_spriteAddUnder), seen through the screen's transparent body,
// and B's fade out is a black sprite over everything. So a frame in steady motion draws nothing into the screen's body,
// and the host uploads only the bars' rows (nextui.c). Under a context menu (it draws over the body on the screen) a
// frame is drawn the software way, as before.

#include "ui_contextmenu.h"
#include "artloader.h"
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
#include "list_window.h"
#include "menuart.h"
#include "area_scale.h"
#include "menulogo.h"
#include "controller_art.h"
#include "controller_art_model.h"
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
#define PIC_UPLOAD_BANDS 4				// the Backdrop picture's texture, uploaded over this many frames (sprite mode)
#define FADE_MS MENU_TRANSITION_FADE_MS // the Backdrop picture's crossfade, and its fade in from black (§8b.4)
#define ROOM_DP 4.0f					// ring/shadow room above and below the row: the 3 dp ring + 1
#define RING_DP 3.0f
#define SCALE_EPS 0.004f // an item this close to a rest size is drawn 1:1 from that size's surface

#define CAROUSEL_CAPTION_GAP_DP 16.0f // × f, under the row
#define BACKDROP_CAPTION_GAP_DP 18.0f // × f, under the row
#define CAPTION_NAME_SP 18.0f
#define CAPTION_SIDE_NAME_SP 16.0f // beside a Vertical stack: a column, not the row's full width under it
#define CAPTION_INFO_SP 14.0f
// ...and at the Small UI scale (2x), where those read small: the name back at the row's 18, the info a step up
#define CAPTION_SIDE_NAME_SP_SMALL 18.0f
#define CAPTION_SIDE_INFO_SP_SMALL 15.0f
#define CAPTION_GUTTER_DP 24.0f

#define TILE_W_SPEC 140.0f // the Grid tile's width: tiles.c's insets scale by tile_w / 140 (never above 1)
#define BOX_SLOT_W_SPEC 170.0f
#define LOGO_SLOT_W_SPEC ROWVIEW_LOGO_SLOT_W_SPEC

#define SLOT_PAD_DP 8.0f		// tool and collection names in a Backdrop slot
#define SLOT_TEXT_PAD_DP 12.0f	// a logo-less console's name in the logo slot
#define SLOT_TOOL_ICON_DP 60.0f // × the slot content scale
#define SLOT_TOOL_GAP_DP 12.0f
#define SLOT_TOOL_NAME_SP 17.0f
#define SLOT_LOGO_NAME_SP 24.0f
#define ROW_BIG_UI_SHRINK 0.85f	   // the Collections slots at a UI scale above the default
#define SLOT_UNKNOWN_ICON_DP 44.0f // a logo-less console's emblem, × the slot content scale
#define SLOT_UNKNOWN_GAP_DP 8.0f

#define TOOL_LONGEST_WORD "Achievements"

// the placeholder box (§8b.4)
#define PH_ASPECT 0.72f
#define PH_PLATE_ALPHA 217	// black at 85%
#define PH_ART_DIM_ALPHA 77 // black at 30% over the abstract plate
#define PH_BORDER_ALPHA 31	// white at 12%
#define PH_SPINE_ALPHA 26	// white at 10%
#define PH_SPINE_SHARE 0.06f
#define SHADOW_OFF_DP 4.0f // the box-art shadow (homeart.c bakes the same for real box art)
#define SHADOW_BLUR_DP 8.0f
#define SHADOW_ALPHA 128 // black at 50%

#define ITEM_SLOTS 32				 // a neighbour is its selected-size surface plus the scaled copy (sideCopy)
#define ITEM_KEY (2 * MAX_PATH + 96) // the path and the drawn name both fit
#define CAPTION_KEY 1024

// The row's position (items): eases from pos_from to pos_to.
static float pos_from = 0, pos_to = 0;
static Tween slide_tw;
static bool slide_retargeted = false; // the slide began while another ran (a held D-pad's repeats)
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
// Sprite mode's picture (RowView_gameSprites): textures of its own under the screen (PLAT_spriteAddUnder), never a CPU
// pass. The target's is made once per resolved picture (PLAT_textureFromSurface, a full-screen upload), keyed by
// HomeArt's surface and its slot generation (a re-decode after Fetch artwork is another); owned, as HomeArt's small
// Backdrop pool evicts a picture a few selections later while it may still be showing under a newer one.
static SDL_Texture* to_tex = NULL;
static const SDL_Surface* to_tex_src = NULL;
static unsigned to_tex_gen = 0;
static int to_tex_w = 0, to_tex_h = 0;
// What shows under the target: the earlier targets whose crossfade a new selection cut short, each at the alpha it had
// reached (none: black, a target without a picture), over black, flattened as they're cut short into one opaque
// screen-size texture (two, drawn into in turn: the new one starts from the old). Instead of settlePicture's CPU bake,
// and instead of blending every cut-short layer on every composite: a held D-pad stacks a layer a step, and on the
// Smart Pro S's 1280x720 five full-screen layers a frame were more than its GPU fills in a frame.
static SDL_Texture* flat_tex[2] = {NULL, NULL};
static int flat_w = 0, flat_h = 0;
static int flat_cur = -1;				// the one that holds the stack, -1: none (black)
static bool flat_pic = false;			// a picture shows in it (else it's all black)
static bool to_flattened = false;		// the target, at full alpha, is flat_tex[flat_cur] already
static bool pic_sprites = false;		// the picture was last drawn as sprites (else the CPU's pic_from layers)
static bool game_frame_sprites = false; // this frame's game row draws as sprites (RowView_render, for RowView_renderExit)
// B out of a Backdrop game list: the outgoing screen fades to black before the list closes (menu_transition.h).
static MenuTransitionExit exit_fade;
static unsigned exit_top = 0; // the list it belongs to (the Directory serial)

// Item surfaces at the two rest sizes, keyed by kind, size, state, path and the drawn name (a rename changes it).
typedef struct {
	char key[ITEM_KEY];
	Uint32 hash;	// keyHash(key): compared before the ~300-byte strcmp
	SDL_Surface* s; // may be NULL (a remembered failed build)
	unsigned stamp;
	bool used;
} ItemSlot;
static ItemSlot items[ITEM_SLOTS];
static unsigned item_clock = 0;

// Prefetch (RowView_prefetchStep) works for the last frame's row: its geometry and selection, armed by each render and
// disarmed once everything ahead is built (or the row stops drawing). It runs between frames, never forcing one.
static struct {
	bool armed;
	RowGeo g;
	int n, sel;
} pf;

// The main-menu Carousel's "N games" line, in the count grey: Consoles' 8 dp under the selected logo as drawn, its y
// gliding as logo heights differ; Collections' on the selected item (its line is reserved in every item), fading in.
// The text surface is rebuilt only when its text, size or colour changes, the line's place on a new selection (and,
// for Consoles, when the selected logo's surface changes).
static struct {
	char key[96];
	SDL_Surface* s;
	int sel;				// the selection the place was worked out for (-1 = none)
	int y_from, y_to;		// Consoles: the line's top (px), gliding from → to
	const SDL_Surface* art; // unused (the logo is now known by its height: art_h)
	int art_w, art_h;		// Consoles: art_h the logo height y_to was worked out for (-1 = none, the name)
	int off;				// Collections: the line's centre below the item's centre, at the selected size (px)
	bool fade_pending;		// Collections: the fade starts when the selection's count is first known
	bool pending;			// Consoles: the selected logo is still being decoded (no line until it lands)
	Tween glide, fade;
} cnt = {.sel = -1};

// The caption under the row (or beside a Vertical stack), rebuilt only when its text or look changes. A few are kept
// (the selection's and the neighbours' made ahead between frames, RowView_prefetchStep): composing one (its name's
// text and shadow, the info line) took ~5-10 ms on the Brick, a late frame on every step of a held D-pad when made in
// the frame. `caption` is the one captionSurface last returned.
#define CAPTION_SLOTS 4
typedef struct {
	char key[CAPTION_KEY];
	SDL_Surface* s;
	SDL_Rect ink;  // the part with any alpha: only it is blitted (the rest of the block is clear)
	int content_h; // the lines' height as laid out (px; ≤ the surface's)
	unsigned stamp;
} CaptionSlot;
static CaptionSlot caption_slots[CAPTION_SLOTS];
static unsigned caption_clock = 0;
static CaptionSlot* cap_cur = &caption_slots[0];
#define caption (*cap_cur)
// A frame's caption may be a kept one of the same game and look whose info rows are older (sprite mode: the play time
// or trophies landing after the selection did would otherwise make the caption in that frame, a late one): it shows
// for a frame or two while the new one is made between frames (cap_refresh), then a redraw shows it (cap_redraw).
static bool cap_allow_stale = false, cap_refresh = false, cap_redraw = false;
static Uint32 cap_refresh_at = 0; // when the older one first stood in: past CAP_STALE_MS the frame makes it after all
#define CAP_STALE_MS 250

static void captionsClear(void) {
	for (int i = 0; i < CAPTION_SLOTS; i++)
		if (caption_slots[i].s)
			GFX_freeSurfaceAndTexture(caption_slots[i].s);
	memset(caption_slots, 0, sizeof(caption_slots));
	caption_clock = 0;
	cap_cur = &caption_slots[0];
	cap_refresh = cap_redraw = false;
}

///////////////////////////////////////
// Timing

static float currentPos(void) {
	if (!slide_tw.active)
		return pos_to;
	return pos_from + (pos_to - pos_from) * UI_easeStandard(tweenProgress(&slide_tw, SLIDE_MS));
}

///////////////////////////////////////
// Units

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
	int tab = GameList_lookTab(); // the root tab, or a hidden tab's list pushed over it
	if (tab >= 0) {
		switch (tab) {
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

// The caption name's lines at most: the Carousel's two on a 4:3 screen, one on a 16:9 one (its wide caption fits a
// name on one line, so the reserve matches what's drawn and the block centres); the Backdrop's one.
static int captionNameLines(const SDL_Surface* screen, RowKind k) {
	if (k != ROW_CAROUSEL)
		return 1;
	return screen && screen->w * 2 > screen->h * 3 ? 1 : 2;
}

// The caption's reserved height (px): the Carousel's name lines + the info line; the Backdrop's name's one line +
// two info rows.
static int captionReserve(const SDL_Surface* screen, RowKind k, CapKind c) {
	if (c == CAP_NONE)
		return 0;
	TTF_Font* fn = UIFont_get(CAPTION_NAME_SP, false);
	int nh = fn ? TTF_FontHeight(fn) : 0;
	TTF_Font* fi = UIFont_get(CAPTION_INFO_SP, false);
	int ih = fi ? TTF_FontHeight(fi) : 0;
	return k == ROW_CAROUSEL ? captionNameLines(screen, k) * nh + ih : nh + 2 * ih;
}

static void computeGeo(SDL_Surface* screen, RowKind kind, RowGeo* g) {
	float pd = pxPerDp();
	float sw = screen->w / pd, sh = screen->h / pd;
	float body_h = sh - 2 * BAR_DP;
	g->kind = kind;
	g->vertical = false;
	g->cap = captionKind(kind);
	g->sz = Row_sizes(kind, sw, body_h);
	// a UI scale above the device's default: the main-menu Carousel's Collections slots at 85% (their name too, through
	// the slot content scale), else they take most of the smaller body. Consoles' logo slot is sized by the screen
	// alone (Row_sizes), the same at every UI scale.
	if (FIXED_SCALE > NATIVE_SCALE && kind == ROW_BACKDROP_COLL) {
		g->sz.item_w *= ROW_BIG_UI_SHRINK;
		g->sz.item_h *= ROW_BIG_UI_SHRINK;
		g->sz.gap *= ROW_BIG_UI_SHRINK;
		g->sz.f *= ROW_BIG_UI_SHRINK;
	}
	g->cap_h = captionReserve(screen, kind, g->cap); // 0: the row alone is centred between the tab row and the hint bar
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
	float spec_w = kind == ROW_BACKDROP_LOGO   ? LOGO_SLOT_W_SPEC
				   : kind == ROW_BACKDROP_COLL ? ROWVIEW_COLL_SLOT_W_SPEC
				   : kind == ROW_BACKDROP_TOOL ? ROWVIEW_TOOLROW_SLOT_W_SPEC
											   : BOX_SLOT_W_SPEC;
	g->k = fminf(1.0f, g->sz.item_w / spec_w);
	if (kind == ROW_BACKDROP_TOOL) // the bigger slot's icon and name
		g->k *= ROWVIEW_TOOLROW_CONTENT;
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
	int tab = GameList_lookTab(); // the root tab, or a hidden tab's list pushed over it (-1: a game list)
	bool at_root = tab >= 0;
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

static const char* consoleLogoFile(const char* folder_name, char* out, size_t size) {
	const char* id = MenuLogo_idForFolder(folder_name);
	if (!id)
		return NULL;
	snprintf(out, size, "menu_logo_%s.png", id);
	return out;
}

const char* RowView_padId(Entry* e, TileKind kind) {
	if (kind != TILE_LOGO || !e || !CFG_getMenuControllerArt())
		return NULL;
	const char* slash = strrchr(e->path, '/');
	return Pad_idForFolder(slash ? slash + 1 : e->path);
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
	return icon ? icon : TILE_UNKNOWN_TOOL_ICON;
}

// "N games" for a Consoles or Collections row ("" while unknown).
static void countLabel(Entry* e, TileKind kind, char* out, size_t size) {
	out[0] = '\0';
	if (GameList_lookTab() < 0)
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
		GFX_freeSurfaceAndTexture(stretch_scratch);
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
// GPU sprite mode (RowView_beginSprites): the Consoles row's and the game lists' Carousel and Backdrop rows' pictures
// go to the GPU as sprites (PLAT_spriteAdd) instead of being blended into the screen, so a slide neither blends them
// on the CPU nor re-uploads the screen.
static bool sprite_mode = false;
static bool sprites_used = false;
static bool draw_selected = false; // the item RowView_render is drawing is the selection (its crisp logo)
// a Consoles logo or controller was still on the art loader's thread in the last render: redraw until it lands
static bool art_waiting = false;

// Pictures given their texture this frame (sprite mode): one a frame at most, past the selection's. A box art or
// screenshot landing from the art thread while on screen was uploaded in the frame that drew it (~3-5 ms each on the
// Brick), several at once on a held D-pad; the others now show a frame or two later (art_waiting keeps the redraws).
static int frame_uploads = 0;

void RowView_beginSprites(bool on) {
	sprite_mode = on;
	if (on) {
		sprites_used = true;
		art_waiting = false;
		frame_uploads = 0;
	}
}

static bool uploadAllowed(SDL_Surface* s, bool selected) {
	if (!s || s->userdata)
		return true;
	if (selected || frame_uploads == 0) {
		frame_uploads++;
		return true;
	}
	art_waiting = true;
	return false;
}

// What the screen's body (between the bars) holds, as a sprite frame left it: transparent (a Backdrop game list: its
// picture under the screen shows through), or plain black (a game list's Carousel, and Consoles). A
// sprite frame draws nothing else there, and the host clears only the bars' rows after one, so the next sprite frame
// needs no fill when the body already holds what it wants. Unknown after any other frame (a software one, the Grid's,
// another screen's): the next sprite frame fills it, and says so (RowView_takeBodyChanged), as the screen texture's
// body must then be uploaded whole once.
typedef enum { BODY_UNKNOWN,
			   BODY_CLEAR,
			   BODY_BLACK } BodyFill;
static BodyFill body_left = BODY_UNKNOWN; // as the last frame left it
static BodyFill body_now = BODY_UNKNOWN;  // as this frame leaves it
static bool body_changed = false;

bool RowView_takeSpritesUsed(void) {
	bool used = sprites_used;
	sprites_used = false;
	body_left = used ? body_now : BODY_UNKNOWN;
	body_now = BODY_UNKNOWN;
	return used;
}

bool RowView_takeBodyChanged(void) {
	bool changed = body_changed;
	body_changed = false;
	return changed;
}

// The body rows (bar to the screen's height less a bar) as `want` for this sprite frame: filled only when they may
// hold something else (another kind of frame left them, or another screen came between).
static void bodyFill(SDL_Surface* screen, BodyFill want, int lastScreen) {
	int bar = barPx();
	if (want != body_left || lastScreen != SCREEN_GAMELIST) {
		Uint32 c = want == BODY_BLACK ? SDL_MapRGBA(screen->format, 0, 0, 0, 255) : 0;
		SDL_FillRect(screen, &(SDL_Rect){0, bar, screen->w, screen->h - 2 * bar}, c);
		body_changed = true;
	}
	body_now = want;
}

// A 1x1 opaque black surface, its texture stretched by the GPU wherever black goes over (or under) the sprites: a
// Carousel tile's darkening while it crossfades, the picture's black, B's fade out.
static SDL_Surface* black_px = NULL;

static SDL_Texture* blackTexture(void) {
	if (!black_px) {
		black_px = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!black_px)
			return NULL;
		SDL_FillRect(black_px, NULL, SDL_MapRGBA(black_px->format, 0, 0, 0, 255));
	}
	return PLAT_textureForSurface(black_px);
}

// A sprite's alpha: a at the tab-focus dim (lit everywhere but the main menu)
static Uint8 spriteAlpha(int a) {
	if (a <= 0)
		return 0;
	return (Uint8)((a > 255 ? 255 : a) * MenuTabs_contentAlpha() + 0.5f);
}

// s's texture into rect r at alpha a, clipped as the screen is
static void surfaceSprite(SDL_Surface* screen, SDL_Surface* s, const SDL_Rect* src, SDL_Rect r, int a) {
	if (!s || r.w <= 0 || r.h <= 0)
		return;
	Uint8 sa = spriteAlpha(a);
	SDL_Texture* t = sa ? PLAT_textureForSurface(s) : NULL;
	if (t)
		PLAT_spriteAdd(t, src, &r, sa, &screen->clip_rect);
}

static void blitCentred(SDL_Surface* dst, SDL_Surface* s, int cx, int cy, float factor, Uint8 a, Uint8 c) {
	if (!s || a == 0 || !(factor > 0.0f))
		return;
	bool exact = fabsf(factor - 1.0f) < SCALE_EPS;
	int w = exact ? s->w : (int)(s->w * factor + 0.5f);
	int h = exact ? s->h : (int)(s->h * factor + 0.5f);
	if (w <= 0 || h <= 0)
		return;
	SDL_Rect r = {cx - w / 2, cy - h / 2, w, h};
	// sprite mode: the GPU draws it, scaled (linear) and blended over the screen, clipped as the screen is, at the
	// tab-focus dim (the body under it is black, or a Backdrop game list's picture)
	if (sprite_mode) {
		if (uploadAllowed(s, draw_selected))
			surfaceSprite(dst, s, NULL, r, a);
		return;
	}
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

// 32-bit FNV-1a of a cache key.
static Uint32 keyHash(const char* key) {
	Uint32 h = 2166136261u;
	for (const unsigned char* p = (const unsigned char*)key; *p; p++)
		h = (h ^ *p) * 16777619u;
	return h;
}

static bool itemFind(const char* key, SDL_Surface** out) {
	Uint32 h = keyHash(key);
	for (int i = 0; i < ITEM_SLOTS; i++) {
		if (items[i].used && items[i].hash == h && strcmp(items[i].key, key) == 0) {
			items[i].stamp = ++item_clock;
			*out = items[i].s;
			return true;
		}
	}
	return false;
}

// Takes ownership of s (NULL remembers a failed build). Evicts the least recently used slot.
static SDL_Surface* itemStore(const char* key, SDL_Surface* s) {
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
		GFX_freeSurfaceAndTexture(victim->s);
	snprintf(victim->key, sizeof(victim->key), "%s", key);
	victim->hash = keyHash(victim->key); // of the stored (possibly truncated) key, as itemFind compares it
	victim->s = s;
	victim->used = true;
	victim->stamp = ++item_clock;
	return s;
}

static void logoNamesClear(void);

static void itemsClear(void) {
	for (int i = 0; i < ITEM_SLOTS; i++) {
		if (items[i].s)
			GFX_freeSurfaceAndTexture(items[i].s);
	}
	memset(items, 0, sizeof(items));
	item_clock = 0;
	logoNamesClear();
}

///////////////////////////////////////
// Carousel tiles

// The tile for item `e` at w×h px, plain (lit false) or lit, with its ring room around it. A game's screenshot is
// requested at the centre size for both sizes (one cache entry per game; the side tile scales it down once).
static SDL_Surface* carouselTile(const RowGeo* g, Entry* e, TileKind kind, int w, int h, bool lit) {
	SDL_Surface* pic = NULL;
	int state = 0;
	unsigned gen = 0;
	if (kind == TILE_GAME) {
		HomeArtState st = HomeArt_pin(e->path, g->full_w, g->full_h, 0, &pic); // valid until the next HomeArt_*
		gen = HomeArt_lastGen();											   // changes after HomeArt_forget (Fetch artwork), so a re-decoded art misses the cache
		state = st == HOMEART_READY && pic ? 2 : (st == HOMEART_LOADING ? 1 : 0);
	}
	char key[ITEM_KEY];
	// the lit look's ring (a game) or fill (a tool) is in the accent, a tool's content in its ink
	SDL_Color ac = lit ? UI_accent() : (SDL_Color){0, 0, 0, 0};
	SDL_Color ink = lit ? UI_onAccent() : (SDL_Color){0, 0, 0, 0};
	snprintf(key, sizeof(key), "C|%d|%d|%d|%d|%02x%02x%02x%02x%02x%02x|%d|%u|%s|%s", w, h, (int)kind, lit, ac.r,
			 ac.g, ac.b, ink.r, ink.g, ink.b, state, gen, e->path, View_displayName(e));
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
		TileSpec t = {.kind = kind, .name = View_displayName(e), .carousel = true};
		t.scale = tile_dp / TILE_W_SPEC > 1.0f ? 1.0f : tile_dp / TILE_W_SPEC;
		if (kind == TILE_LOGO)
			t.logo_file = entryLogoFile(e, logo, sizeof(logo));
		else if (kind == TILE_TOOL)
			t.icon_file = toolIconFile(e, t.name);
		else if (kind == TILE_GAME) {
			if (state == 2)
				t.picture = pic; // its screenshot, or its abstract picture (HomeArt_pin's fallback)
			else
				t.kind = TILE_TITLE; // none at all (HomeArt couldn't make one): a title tile
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

// A collection item's text at w×h px, a side item at lvl of the selected size (k_full: the selected size's slot
// content scale). The name size is worked out at the selected size, so a side item is the same item smaller: 30 sp ×
// k_full (× ROW_COLL_NAME_VERTICAL in a Vertical stack), shrunk in whole sp until its longest word fits the slot less 8 dp each side, never below max(0.75 × that,
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
	float start = ROW_COLL_NAME_SP * k_full * (fit ? ROW_COLL_NAME_VERTICAL : 1.0f);
	float count_sp = Row_countSp(k_full);
	float sp = Tiles_collNameSp(name, start, count_sp, full_avail);
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
	TTF_Font* f = UIFont_get(c.sp, false);
	int nh = Tiles_textBlockStep(NULL, f, name, 0, 0, c.avail, ROW_COLL_LINES, false, 255, 255, false, c.step);
	int lines = c.step > 0 && nh > 0 ? nh / c.step : 1;
	c.t = Row_collText(h, lines, c.step, NX_DPF(ROW_COLL_COUNT_GAP_DP * lvl), count_h);
	return c;
}

// A logo-less console in a w×h logo slot: the unknown-console emblem (SLOT_UNKNOWN_ICON_DP × k, at most 36% of h) over
// its name (24 sp × k, two lines), both in the logo off-white, the block's top at y. Returns the block's height; a NULL
// s only measures.
static int logoName(SDL_Surface* s, const char* name, int w, int h, int y, float k, float lvl) {
	int box = NX_DPF(SLOT_UNKNOWN_ICON_DP * k);
	if (box > (int)(h * 0.36f))
		box = (int)(h * 0.36f);
	SDL_Surface* icon = box > 0 ? MenuArt_get(TILE_UNKNOWN_CONSOLE_ICON, box, box) : NULL;
	int ih = icon ? icon->h : 0, gap = icon ? NX_DPF(SLOT_UNKNOWN_GAP_DP * k) : 0;
	if (s && icon) {
		SDL_SetSurfaceColorMod(icon, TILE_MENU_GREY, TILE_MENU_GREY, TILE_MENU_GREY);
		SDL_BlitSurface(icon, NULL, s, &(SDL_Rect){(w - icon->w) / 2, y, icon->w, icon->h});
		SDL_SetSurfaceColorMod(icon, 255, 255, 255); // MenuArt's copy is shared
	}
	// fonts after MenuArt_get: a font pointer is only good until the next UIFont_get
	TTF_Font* f = UIFont_get(SLOT_LOGO_NAME_SP * k, false);
	int nh = Tiles_textBlock(s, f, name, w / 2, y + ih + gap, w - 2 * NX_DPF(SLOT_TEXT_PAD_DP * lvl), 2, false,
							 TILE_MENU_GREY, 255, false);
	return ih + gap + nh;
}

// logoName's measured height for a logo-less console at the full slot size, kept per name so the count lines (selection
// and neighbours, every frame) don't re-wrap and re-measure it: keyed on everything it depends on (the name, the slot,
// the content scale and FIXED_SCALE, which set the emblem and font sizes). Cleared with the item cache.
#define LOGO_NAME_SLOTS 16
static struct {
	char name[MAX_PATH];
	int w, h, scale;
	float k;
	int drawn;
} logo_name_h[LOGO_NAME_SLOTS];
static int logo_name_next = 0;

static void logoNamesClear(void) {
	memset(logo_name_h, 0, sizeof(logo_name_h));
	logo_name_next = 0;
}

static int logoNameHeight(const char* name, int w, int h, float k) {
	for (int i = 0; i < LOGO_NAME_SLOTS; i++) {
		if (logo_name_h[i].w == w && logo_name_h[i].h == h && logo_name_h[i].k == k &&
			logo_name_h[i].scale == FIXED_SCALE && strcmp(logo_name_h[i].name, name) == 0)
			return logo_name_h[i].drawn;
	}
	int drawn = logoName(NULL, name, w, h, 0, k, 1.0f);
	int i = logo_name_next;
	logo_name_next = (logo_name_next + 1) % LOGO_NAME_SLOTS;
	snprintf(logo_name_h[i].name, sizeof(logo_name_h[i].name), "%s", name);
	logo_name_h[i].w = w, logo_name_h[i].h = h, logo_name_h[i].k = k, logo_name_h[i].scale = FIXED_SCALE;
	logo_name_h[i].drawn = drawn;
	return drawn;
}

// A Backdrop slot item at w×h px (pad none, transparent white ground): content scale k already includes the size's
// own scale (lvl, 1 or the neighbour scale), and px paddings scale by lvl.
static SDL_Surface* buildSlot(Entry* e, TileKind kind, int w, int h, float k, float lvl, bool fit) {
	SDL_Surface* s = newSurface(w, h, true);
	if (!s)
		return NULL;
	const char* name = View_displayName(e);
	char logo[64];
	switch (kind) {
	case TILE_LOGO: {
		const char* file = entryLogoFile(e, logo, sizeof(logo));
		SDL_Surface* art = file ? MenuArt_get(file, w, h) : NULL;
		if (art) {
			// the fixed off-white (TILE_MENU_GREY, baked into the logo PNGs); the ground matches so the
			// anti-aliased edges keep that grey
			const Uint8 v = TILE_MENU_GREY;
			SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, v, v, v, 0));
			SDL_BlitSurface(art, NULL, s, &(SDL_Rect){(w - art->w) / 2, (h - art->h) / 2, art->w, art->h});
			break;
		}
		// no logo: the unknown-console emblem over the name, centred as one block (on the off-white ground)
		SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, TILE_MENU_GREY, TILE_MENU_GREY, TILE_MENU_GREY, 0));
		logoName(s, name, w, h, (h - logoName(NULL, name, w, h, 0, k, lvl)) / 2, k, lvl);
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
			 View_displayName(e));
	SDL_Surface* full;
	if (!itemFind(key, &full)) {
		// Consoles: a logo still on the art loader's thread is not waited for; the item shows once it lands
		if (g->kind == ROW_BACKDROP_LOGO && kind == TILE_LOGO) {
			char logo[64];
			bool pending = false;
			if (entryLogoFile(e, logo, sizeof(logo)))
				MenuArt_peek(logo, g->full_w, g->full_h, &pending);
			if (pending) {
				art_waiting = true;
				return NULL;
			}
		}
		full = itemStore(key, buildSlot(e, kind, g->full_w, g->full_h, g->k, 1.0f, g->vertical));
	}
	return side ? sideCopy(key, full, g->sz.scale) : full;
}

// The placeholder box's size (px) in a slot_w×slot_h slot: the 0.72 portrait case, fitted.
static void placeholderBox(int slot_w, int slot_h, int* bw, int* bh) {
	*bw = slot_w;
	if ((float)slot_h * PH_ASPECT < (float)slot_w)
		*bw = (int)(slot_h * PH_ASPECT + 0.5f);
	*bh = (int)(*bw / PH_ASPECT + 0.5f);
	if (*bh > slot_h)
		*bh = slot_h;
}

// The placeholder box (§8b.4) fitted in a slot_w×slot_h slot, over the soft shadow, padded for it on every side. With
// `plate` (the game's abstract picture at the box's size, HomeArt_boxPlaceholder) the case shows it under a light dim
// instead of the plain dark plate.
// The placeholder box's shadow alone (W x H, black at the blurred box's alpha): the box's shape at 50%, 4 dp down,
// box-blurred 3 times (≈ the 8 dp blur). The same for every game's box of a size, so made once per size and copied:
// blurring it per game took ~40 ms on the Brick, in the frame a game without box art came into a Backdrop row.
static SDL_Surface* ph_shadow = NULL;

static SDL_Surface* placeholderShadow(int W, int H, int bw, int bh, int pad, int off, int r) {
	if (ph_shadow && ph_shadow->w == W && ph_shadow->h == H)
		return ph_shadow;
	GFX_freeSurfaceAndTexture(ph_shadow);
	ph_shadow = newSurface(W, H, false);
	if (!ph_shadow)
		return NULL;
	unsigned char* a = calloc((size_t)W * H, 1);
	unsigned char* tmp = malloc((size_t)W * H);
	if (a && tmp) {
		for (int y = pad + off; y < pad + off + bh && y < H; y++)
			memset(a + (size_t)y * W + pad, SHADOW_ALPHA, (size_t)bw);
		Row_boxBlurAlpha(a, tmp, W, H, r, 3);
		if (SDL_MUSTLOCK(ph_shadow))
			SDL_LockSurface(ph_shadow);
		for (int y = 0; y < H; y++) {
			Uint32* row = (Uint32*)((Uint8*)ph_shadow->pixels + y * ph_shadow->pitch);
			for (int x = 0; x < W; x++)
				row[x] = (Uint32)a[(size_t)y * W + x] << 24; // black
		}
		if (SDL_MUSTLOCK(ph_shadow))
			SDL_UnlockSurface(ph_shadow);
	}
	free(a);
	free(tmp);
	return ph_shadow;
}

static SDL_Surface* buildPlaceholder(int slot_w, int slot_h, float lvl, SDL_Surface* plate) {
	int bw, bh;
	placeholderBox(slot_w, slot_h, &bw, &bh);
	if (bw <= 0 || bh <= 0)
		return NULL;
	int off = NX_DPF(SHADOW_OFF_DP * lvl);
	int r = NX_DPF(SHADOW_BLUR_DP * lvl) / 3;
	if (r < 1)
		r = 1;
	int pad = off + r * 3 + 1;
	int W = bw + 2 * pad, H = bh + 2 * pad;
	SDL_Surface* base = placeholderShadow(W, H, bw, bh, pad, off, r);
	SDL_Surface* s = newSurface(W, H, false);
	if (!s)
		return NULL;
	if (base) { // a copy of the shadow every box of this size shares
		SDL_SetSurfaceBlendMode(base, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(base, NULL, s, NULL);
	}

	// the plate, its 1 dp border and the spine
	int b = NX_DPF(1.0f * lvl);
	if (b < 1)
		b = 1;
	if (plate && plate->w == bw && plate->h == bh) {
		// the abstract picture, opaque, then a light dim (the case's tone over its glows)
		SDL_SetSurfaceBlendMode(plate, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(plate, NULL, s, &(SDL_Rect){pad, pad, bw, bh});
		SDL_SetSurfaceBlendMode(plate, SDL_BLENDMODE_BLEND);
		overRect(s, pad, pad, bw, bh, 0, PH_ART_DIM_ALPHA);
	} else {
		overRect(s, pad, pad, bw, bh, 0, PH_PLATE_ALPHA);
	}
	overRect(s, pad, pad, bw, b, 255, PH_BORDER_ALPHA);
	overRect(s, pad, pad + bh - b, bw, b, 255, PH_BORDER_ALPHA);
	overRect(s, pad, pad + b, b, bh - 2 * b, 255, PH_BORDER_ALPHA);
	overRect(s, pad + bw - b, pad + b, b, bh - 2 * b, 255, PH_BORDER_ALPHA);
	overRect(s, pad + b, pad + b, (int)(bw * PH_SPINE_SHARE + 0.5f), bh - 2 * b, 255, PH_SPINE_ALPHA);

	return s;
}

// The placeholder box: composed once at the selected size, no title (the caption names the game); a neighbour is that
// scaled.
// The case with the game's abstract plate once HomeArt has it ("PA" key); blank (NULL) while it's being made, so
// nothing flashes in; the plain dark case ("P") when it can't be made.
static SDL_Surface* placeholderItem(const RowGeo* g, Entry* e, TileKind kind, bool side) {
	(void)kind;
	char key[ITEM_KEY];
	SDL_Surface* full;
	snprintf(key, sizeof(key), "PA|%d|%d|%s", g->full_w, g->full_h, e->path);
	if (itemFind(key, &full))
		return side ? sideCopy(key, full, g->sz.scale) : full;
	int bw, bh;
	placeholderBox(g->full_w, g->full_h, &bw, &bh);
	SDL_Surface* plate = NULL;
	HomeArtState st = bw > 0 && bh > 0 ? HomeArt_boxPlaceholder(e->path, bw, bh, &plate) : HOMEART_NONE;
	if (st == HOMEART_LOADING)
		return NULL;
	if (st == HOMEART_READY && plate) {
		full = itemStore(key, buildPlaceholder(g->full_w, g->full_h, 1.0f, plate));
		return side ? sideCopy(key, full, g->sz.scale) : full;
	}
	snprintf(key, sizeof(key), "P|%d|%d|%s", g->full_w, g->full_h, e->path);
	if (!itemFind(key, &full))
		full = itemStore(key, buildPlaceholder(g->full_w, g->full_h, 1.0f, NULL));
	return side ? sideCopy(key, full, g->sz.scale) : full;
}

///////////////////////////////////////
// Drawing the row

// An item drawn from its two rest-size surfaces: 1:1 at a rest size, else the centre one scaled. In sprite mode the
// full surface scaled by the GPU, at rest too (no side copy to make and upload: a Backdrop row's neighbours are at
// half size, where the GPU's linear filter averages as the area scale did).
static void drawRested(SDL_Surface* screen, SDL_Surface* (*get)(const RowGeo*, Entry*, TileKind, bool),
					   const RowGeo* g, Entry* e, TileKind kind, float s, int cx, int cy, Uint8 a) {
	if (sprite_mode)
		blitCentred(screen, get(g, e, kind, false), cx, cy, s, a, 255);
	else if (fabsf(s - g->sz.scale) < SCALE_EPS)
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

// A Carousel game tile as GPU sprites (sprite mode), no composed look: composing one (Tiles_draw: the rounded base,
// border and ring) cost 9-13 ms on the Brick, and a held D-pad brings several new tiles in per step. The picture
// (HomeArt_pin's, at the centre size) is drawn square with the black corner mask rounding it on the black ground, the
// plain border and the lit ring over it, all at the darkening (over black: the same lerp the composed tile had). The
// overlays are made once per tile size (the centre's and a neighbour's; an item changing size scales the centre's) and
// accent, as the Grid's (gridview.c drawGameSprites).
typedef struct {
	int w, h;
	SDL_Color ring_c;
	SDL_Surface *mask, *border, *ring;
	unsigned stamp;
} TileOverlays;
static TileOverlays tile_ov[2];
static unsigned tile_ov_clock = 0;

static void tileOverlaysClear(void) {
	for (int i = 0; i < 2; i++) {
		GFX_freeSurfaceAndTexture(tile_ov[i].mask);
		GFX_freeSurfaceAndTexture(tile_ov[i].border);
		GFX_freeSurfaceAndTexture(tile_ov[i].ring);
	}
	memset(tile_ov, 0, sizeof(tile_ov));
}

static TileOverlays* tileOverlays(int w, int h) {
	SDL_Color ac = UI_accent();
	TileOverlays* o = NULL;
	for (int i = 0; i < 2 && !o; i++)
		if (tile_ov[i].mask && tile_ov[i].w == w && tile_ov[i].h == h)
			o = &tile_ov[i];
	if (!o) {
		o = tile_ov[0].stamp <= tile_ov[1].stamp ? &tile_ov[0] : &tile_ov[1];
		GFX_freeSurfaceAndTexture(o->mask);
		GFX_freeSurfaceAndTexture(o->border);
		GFX_freeSurfaceAndTexture(o->ring);
		memset(o, 0, sizeof(*o));
		o->w = w, o->h = h;
		o->mask = Tiles_cornerMask(w, h);
		o->border = Tiles_borderOverlay(w, h);
	}
	if (!o->ring || o->ring_c.r != ac.r || o->ring_c.g != ac.g || o->ring_c.b != ac.b) {
		GFX_freeSurfaceAndTexture(o->ring);
		o->ring = Tiles_ringOverlay(w, h, ac);
		o->ring_c = ac;
	}
	o->stamp = ++tile_ov_clock;
	return o->mask ? o : NULL;
}

// False when the tile has no picture to show (HomeArt couldn't make one): the composed title tile draws instead.
static bool carouselGameSprites(SDL_Surface* screen, const RowGeo* g, Entry* e, int cx, int cy, float scale, Uint8 c,
								float lit) {
	SDL_Surface* pic = NULL;
	HomeArtState st = HomeArt_pin(e->path, g->full_w, g->full_h, 0, &pic); // valid until the next HomeArt_*
	bool ready = st == HOMEART_READY && pic;
	if (!ready && st != HOMEART_LOADING)
		return false;
	bool side = fabsf(scale - g->sz.scale) < SCALE_EPS;
	bool exact = fabsf(scale - 1.0f) < SCALE_EPS;
	int w = side ? g->side_w : (exact ? g->full_w : (int)(g->full_w * scale + 0.5f));
	int h = side ? g->side_h : (exact ? g->full_h : (int)(g->full_h * scale + 0.5f));
	if (w <= 0 || h <= 0)
		return true;
	// the overlays at this rest size, or the centre size's scaled while the item changes size
	TileOverlays* o = tileOverlays(side ? g->side_w : g->full_w, side ? g->side_h : g->full_h);
	float k = (float)w / (float)(side ? g->side_w : g->full_w);
	SDL_Rect r = {cx - w / 2, cy - h / 2, w, h};
	if (ready && !uploadAllowed(pic, lit >= 1.0f))
		ready = false; // its texture next frame: the black base and border meanwhile, as while it loads
	if (ready)
		surfaceSprite(screen, pic, NULL, r, c);
	if (o && ready)
		surfaceSprite(screen, o->mask, NULL, r, 255); // black corners over the black ground
	if (o && o->border && lit < 1.0f)
		surfaceSprite(screen, o->border, NULL, r, (int)(Tiles_borderAlpha() * (1.0f - lit) * c / 255.0f + 0.5f));
	if (o && o->ring && lit > 0.0f) {
		int ring = (int)((o->ring->w - o->w) / 2 * k + 0.5f);
		surfaceSprite(screen, o->ring, NULL, (SDL_Rect){r.x - ring, r.y - ring, w + 2 * ring, h + 2 * ring},
					  (int)(lit * c + 0.5f));
	}
	return true;
}

// The same tile's pieces made ahead (between frames): the picture requested and uploaded, the overlays at its size.
static void carouselGameWarm(const RowGeo* g, Entry* e, bool side) {
	SDL_Surface* pic = NULL;
	if (HomeArt_pin(e->path, g->full_w, g->full_h, 0, &pic) == HOMEART_READY && pic)
		PLAT_textureForSurface(pic);
	TileOverlays* o = tileOverlays(side ? g->side_w : g->full_w, side ? g->side_h : g->full_h);
	if (o) {
		PLAT_textureForSurface(o->mask);
		if (o->border)
			PLAT_textureForSurface(o->border);
		if (o->ring)
			PLAT_textureForSurface(o->ring);
	}
}

// A Carousel tile centred on (cx, cy): the side one 1:1 at rest; while it changes size, the plain look with the lit
// one fading in over it, both from the centre size. The darkening is the lerp toward the black ground under the tile
// (the tiles are opaque and never overlap: the gap between them stays `gap` through the slide, on X or on Y).
// Sprite mode: the same surfaces as sprites over the black ground, the GPU scaling the centre size (linear). At rest
// the darkening is the tile's own alpha (over black, the same lerp); while it changes size the plain and lit looks
// crossfade at full strength and the black ground goes over them at the darkening (what UI_dimRect does here).
static void drawCarouselItem(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy,
							 float scale, float darken, float d) {
	Uint8 c = (Uint8)(255.0f * (1.0f - darken) + 0.5f);
	if (c == 0)
		return; // black on black
	float lit = 1.0f - (d < 1.0f ? d : 1.0f);
	if (sprite_mode && kind == TILE_GAME && carouselGameSprites(screen, g, e, cx, cy, scale, c, lit))
		return;
	if (fabsf(scale - g->sz.scale) < SCALE_EPS) {
		SDL_Surface* s = carouselTile(g, e, kind, g->side_w, g->side_h, false);
		SDL_Rect r = s ? (SDL_Rect){cx - s->w / 2, cy - s->h / 2, s->w, s->h} : (SDL_Rect){0, 0, 0, 0};
		if (s && sprite_mode)
			surfaceSprite(screen, s, NULL, r, c);
		else if (s)
			drawTileSurface(screen, s, r, c);
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
	if (sprite_mode) {
		surfaceSprite(screen, plain, NULL, r, 255);
		surfaceSprite(screen, lit_s, NULL, r, plain ? (int)(lit * 255.0f + 0.5f) : 255);
		Uint8 dim = spriteAlpha(255 - c);
		SDL_Texture* black = dim ? blackTexture() : NULL;
		if (black)
			PLAT_spriteAdd(black, NULL, &r, dim, &screen->clip_rect);
		return;
	}
	drawTileSurface(screen, plain, r, 255);
	drawTileSurface(screen, lit_s, r, plain ? (int)(lit * 255.0f + 0.5f) : 255);
	if (c < 255)
		UI_dimRect(screen, &r, (Uint8)(255 - c));
}

// A box art at a neighbour's rest size: stretched once and kept, so a rested neighbour isn't stretched every frame.
// Keyed by the art surface itself (HomeArt keeps a READY surface until it evicts it) and its HomeArt slot generation
// `gen` (HomeArt_lastGen right after the HomeArt_boxart call that returned `art`).
static SDL_Surface* sideArt(const RowGeo* g, Entry* e, SDL_Surface* art, unsigned gen) {
	int w = (int)(art->w * g->sz.scale + 0.5f), h = (int)(art->h * g->sz.scale + 0.5f);
	if (w <= 0 || h <= 0)
		return NULL;
	char key[ITEM_KEY];
	// the gen beside the pointer: a re-decoded art (after HomeArt_forget) may reuse the old surface's address
	snprintf(key, sizeof(key), "A|%d|%d|%p|%u|%s", w, h, (void*)art, gen, e->path);
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
			unsigned gen = HomeArt_lastGen(); // the slot of that art: keys its stretched copy
			if (st == HOMEART_READY && art) {
				// the shadow padding is the same on every side: the surface's centre is the art's
				// (sprite mode: the GPU scales the art itself, no stretched copy to make and upload)
				SDL_Surface* side =
					!sprite_mode && fabsf(scale - g->sz.scale) < SCALE_EPS ? sideArt(g, e, art, gen) : NULL;
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
	Uint8 a8 = (Uint8)(alpha * 255.0f + 0.5f);
	if (RowView_drawConsoleLogo(screen, g, e, kind, cx, cy, scale, a8, draw_selected))
		return; // a Consoles logo as a GPU sprite (sprite mode)
	drawRested(screen, slotItem, g, e, kind, scale, cx, cy, a8);
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

// The caption's look: under the row (centred, in its reserve) or beside a Vertical stack (left-aligned, §8f.4; with
// Vertical alignment Right, right-aligned against the stack).
typedef struct {
	int w;			// the text column (px)
	int max_h;		// the most it may take (px): the reserve and the whole free lines under it, or the body
	int reserve_h;	// the reserve (the row's caption room: its rows never move); 0 beside a stack
	int name_lines; // the name's lines at most
	int gap;		// between rows (px): 0 under the row, 3 dp beside a stack
	bool left;		// left-aligned from the column's left edge, else centred
	bool shadow;	// the name's dark shadow
	bool right;		// beside a stack (left): right-aligned to the column's right edge instead
} CapStyle;

#define CAP_LINES (2 + CAPTION_FIT_NEXT_LINES) // info lines: time, trophy and Next's own lines

static int captionMeasure(void* ctx, const char* text) {
	return textW((TTF_Font*)ctx, text);
}

// The info rows as drawn lines: a row ending in a Next too long for the column gives Next its own lines, out of the
// whole free lines past the reserve (caption_fit.h). Returns the line count; *h the block's height with the name's.
// The caption's name and info sizes (sp): the row's under it; beside a stack (left) a step smaller at the Large UI scale
// and a step larger at the Small one.
static float capNameSp(const CapStyle* cs) {
	if (!cs->left)
		return CAPTION_NAME_SP;
	return FIXED_SCALE >= 3 ? CAPTION_SIDE_NAME_SP : CAPTION_SIDE_NAME_SP_SMALL;
}
static float capInfoSp(const CapStyle* cs) {
	if (!cs->left)
		return CAPTION_INFO_SP;
	return FIXED_SCALE >= 3 ? CAPTION_INFO_SP : CAPTION_SIDE_INFO_SP_SMALL;
}

static int captionLines(const CapStyle* cs, int name_h, const SegRow* rows, int nrows, SegRow* out, int* h) {
	TTF_Font* fi = UIFont_get(capInfoSp(cs), false);
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

// path: the entry's, so an older caption stands in (cap_allow_stale) only for the same game, not another of the same
// display name (the same title in two folders, a rename alias) with its play time or trophies
static SDL_Surface* captionSurface(const CapStyle* cs, const char* path, const char* name, const SegRow* rows,
								   int nrows) {
	char key[CAPTION_KEY];
	int n = snprintf(key, sizeof(key), "%d|%d|%d|%d|%d|%d|%d|%d|%d|%08x|%s|", (int)FIXED_SCALE, cs->w, cs->max_h,
					 cs->reserve_h, cs->name_lines, cs->gap, cs->left, cs->shadow, cs->right,
					 (unsigned)View_fnvStr(2166136261u, path), name ? name : "");
	int look_n = n; // the key up to the name: the same game in the same look, whatever its info rows
	for (int r = 0; r < nrows && n > 0 && (size_t)n < sizeof(key); r++) {
		for (int i = 0; i < rows[r].n && n > 0 && (size_t)n < sizeof(key); i++)
			n += snprintf(key + n, sizeof(key) - n, "%d:%s|", (int)rows[r].segs[i].kind, rows[r].segs[i].text);
		if (n > 0 && (size_t)n < sizeof(key))
			n += snprintf(key + n, sizeof(key) - n, "/");
	}
	CaptionSlot* victim = &caption_slots[0];
	for (int i = 0; i < CAPTION_SLOTS; i++) {
		CaptionSlot* c = &caption_slots[i];
		if (c->s && strcmp(c->key, key) == 0) {
			c->stamp = ++caption_clock;
			cap_cur = c;
			return c->s;
		}
		if (!victim->s)
			continue; // an empty slot found first stays the pick
		if (!c->s || c->stamp < victim->stamp)
			victim = c;
	}
	if (cap_allow_stale && look_n > 0 && (size_t)look_n < sizeof(key) &&
		!(cap_refresh && SDL_GetTicks() - cap_refresh_at > CAP_STALE_MS)) {
		for (int i = 0; i < CAPTION_SLOTS; i++) {
			CaptionSlot* c = &caption_slots[i];
			if (c->s && strncmp(c->key, key, (size_t)look_n) == 0) {
				c->stamp = ++caption_clock;
				cap_cur = c;
				if (!cap_refresh)
					cap_refresh_at = SDL_GetTicks();
				cap_refresh = true; // the selection's own is made next, between frames
				return c->s;
			}
		}
	}
	if (cap_allow_stale && cap_refresh) // the frame makes the selection's own (it waited past CAP_STALE_MS)
		cap_refresh = false;
	cap_cur = victim;
	caption.stamp = ++caption_clock;
	if (caption.s)
		GFX_freeSurfaceAndTexture(caption.s);
	caption.s = NULL;
	caption.content_h = 0;
	caption.ink = (SDL_Rect){0, 0, 0, 0};
	snprintf(caption.key, sizeof(caption.key), "%s", key);

	// the layout first (a font is only good until a later UIFont_get: each is fetched where it's used)
	int w = cs->w;
	bool has_name = name && name[0];
	float name_sp = capNameSp(cs);
	TTF_Font* fn = UIFont_get(name_sp, false);
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
	fn = UIFont_get(name_sp, false);
	if (fn && has_name) {
		y += cs->left && cs->right ? Tiles_textBlockRight(caption.s, fn, name, w, 0, w, cs->name_lines, 255, cs->shadow)
			 : cs->left			   ? Tiles_textBlockLeft(caption.s, fn, name, 0, 0, w, cs->name_lines, 255, cs->shadow)
								   : Tiles_textBlock(caption.s, fn, name, w / 2, 0, w, cs->name_lines, false, 255, 255,
													 cs->shadow);
	}
	TTF_Font* fi = UIFont_get(capInfoSp(cs), false);
	for (int l = 0; fi && l < nlines; l++) {
		int ly = y + (y > 0 ? cs->gap : 0);
		if (ly + TTF_FontHeight(fi) > h)
			break; // past the room it may take
		int tw = InfoBand_segmentsWidth(lines[l].segs, lines[l].n, w, fi);
		if (tw <= 0)
			continue;
		// under the row the info keeps its shadow; beside the stack it follows the name's (Backdrop-Vertical only)
		int lx = cs->left ? (cs->right ? w - tw : 0) : (w - tw) / 2;
		InfoBand_drawSegmentsEx(caption.s, lines[l].segs, lines[l].n, lx, false, ly, w, fi,
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

// The caption's ink at (x, y): blended into the screen, or in sprite mode a sprite of it (the caption surface's
// texture, made once per caption and freed with it)
static void blitCaption(SDL_Surface* screen, SDL_Surface* s, int x, int y) {
	if (!s || caption.ink.w <= 0)
		return;
	SDL_Rect r = {x + caption.ink.x, y + caption.ink.y, caption.ink.w, caption.ink.h};
	if (sprite_mode)
		surfaceSprite(screen, s, &caption.ink, r, 255);
	else
		SDL_BlitSurface(s, &caption.ink, screen, &r);
}

static SDL_Surface* rowCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, CapStyle* out);
static void drawCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind) {
	if (g->cap == CAP_NONE || g->cap_draw_h <= 0)
		return;
	CapStyle cs;
	cap_allow_stale = sprite_mode;
	SDL_Surface* s = rowCaption(screen, g, e, kind, &cs);
	cap_allow_stale = false;
	blitCaption(screen, s, g->cx - cs.w / 2, g->cap_y);
}

// The caption under the row for e, as drawCaption shows it (made, or found among the kept ones), its look in *cs
static SDL_Surface* rowCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, CapStyle* out) {
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
				   captionNameLines(screen, g->kind),
				   0,
				   false,
				   true};
	*out = cs;
	return captionSurface(&cs, e->path, View_displayName(e), rows, nrows);
}

// The caption beside a Vertical stack for e (made, or found among the kept ones)
static SDL_Surface* sideCaption(const RowGeo* g, Entry* e, TileKind kind, int w, int body_h, bool right) {
	SegRow rows[2];
	int nrows = captionRows(e, kind, false, rows);
	CapStyle cs = {w, body_h, 0, 2, NX_DPF(STACK_CAPTION_LINE_GAP_DP), true, g->kind == ROW_BACKDROP_BOX, right};
	return captionSurface(&cs, e->path, View_displayName(e), rows, nrows);
}

// Beside a Vertical stack (§8f.4): Backdrop's three rows for both renderings, 3 dp apart, the name on up to two lines;
// it grows with a long Next (up to the body) and stays centred on the selection.
static void drawSideCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int x, int w, int cy,
							int body_top, int body_h, bool right) {
	if (w <= 0 || body_h <= 0)
		return;
	cap_allow_stale = sprite_mode;
	SDL_Surface* s = sideCaption(g, e, kind, w, body_h, right);
	cap_allow_stale = false;
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

// The "N games" grey (TILE_COUNT_GREY), a step under the logo and the name, not the accent.
static SDL_Color countGrey(void) {
	return (SDL_Color){TILE_COUNT_GREY, TILE_COUNT_GREY, TILE_COUNT_GREY, 255};
}

// The "N games" text in colour ac (opaque) at sp: the one cached surface, rebuilt when the text, size or colour
// changes. NULL for an empty text.
static SDL_Surface* countSurface(const char* text, float sp, SDL_Color ac) {
	if (!text || !text[0])
		return NULL;
	char key[sizeof(cnt.key)];
	snprintf(key, sizeof(key), "%d|%02x%02x%02x|%s", NX_SP(sp), ac.r, ac.g, ac.b, text);
	if (cnt.s && strcmp(cnt.key, key) == 0)
		return cnt.s;
	if (cnt.s)
		GFX_freeSurfaceAndTexture(cnt.s);
	snprintf(cnt.key, sizeof(cnt.key), "%s", key);
	TTF_Font* f = UIFont_get(sp, false);
	cnt.s = f ? GFX_renderText(f, text, (SDL_Color){ac.r, ac.g, ac.b, 255}) : NULL;
	if (cnt.s)
		SDL_SetSurfaceBlendMode(cnt.s, SDL_BLENDMODE_BLEND);
	return cnt.s;
}

// Consoles' logos as GPU sprites (sprite mode): each logo decoded at its own size on the art loader's thread
// (MenuArt_loadLevel: the files ship pre-baked at the largest size any view draws them, so no resampling) and a
// half-size level halved from that here, the GPU scaling whichever is the smallest still as big as the drawn logo. Only
// the selected console's logo is the area-averaged one (MenuArt_peek at the slot's size, decoded when it is selected);
// the rest are in the background, where the softer edge doesn't show.
#define LOGO_MIPS 24
typedef struct {
	char file[64];
	SDL_Surface *level, *small; // the file at its own size, and half of that
	unsigned stamp;
	bool used, failed;
} LogoMip;
static LogoMip mips[LOGO_MIPS];
static unsigned mip_clock = 0;

// file's levels, or NULL: none (a failed decode) or still decoding (*pending; queued first in line)
static LogoMip* logoMip(const char* file, bool* pending) {
	*pending = false;
	for (int i = 0; i < LOGO_MIPS; i++) {
		if (mips[i].used && strcmp(mips[i].file, file) == 0) {
			mips[i].stamp = ++mip_clock;
			return mips[i].failed ? NULL : &mips[i];
		}
	}
	bool failed = false;
	SDL_Surface* level = ArtLoader_take(file, 0, 0, &failed);
	if (!level && !failed) {
		ArtLoader_request(file, 0, 0, 0);
		*pending = true;
		art_waiting = true;
		return NULL;
	}
	LogoMip* m = &mips[0];
	for (int i = 0; i < LOGO_MIPS; i++) {
		if (!mips[i].used) {
			m = &mips[i];
			break;
		}
		if (mips[i].stamp < m->stamp)
			m = &mips[i];
	}
	GFX_freeSurfaceAndTexture(m->level);
	GFX_freeSurfaceAndTexture(m->small);
	memset(m, 0, sizeof(*m));
	snprintf(m->file, sizeof(m->file), "%s", file);
	m->level = level;
	m->small = level ? MenuArt_halve(level) : NULL;
	m->failed = !level;
	m->used = true;
	m->stamp = ++mip_clock;
	return m->failed ? NULL : m;
}

static void mipsClear(void) {
	for (int i = 0; i < LOGO_MIPS; i++) {
		GFX_freeSurfaceAndTexture(mips[i].level);
		GFX_freeSurfaceAndTexture(mips[i].small);
	}
	memset(mips, 0, sizeof(mips));
}

// The logo's size in a slot_w x slot_h slot as MenuArt fits it (its own size: the level's)
static void logoFit(const LogoMip* m, int slot_w, int slot_h, int* w, int* h) {
	double sw = m->level->w, sh = m->level->h;
	double s = slot_w / sw < slot_h / sh ? slot_w / sw : slot_h / sh;
	*w = (int)(sw * s);
	*h = (int)(sh * s);
}

// Consoles: the height (px) its logo draws at in the slot, -1 for no logo (a logo-less console: its name). Never
// decoded here: while it is on the art loader's thread, *pending, and the caller leaves that console's count out.
static int logoHeight(const RowGeo* g, Entry* e, TileKind kind, bool* pending) {
	char logo[64];
	const char* file = kind == TILE_LOGO ? entryLogoFile(e, logo, sizeof(logo)) : NULL;
	*pending = false;
	LogoMip* m = file ? logoMip(file, pending) : NULL;
	if (!m)
		return -1;
	int w, h;
	logoFit(m, g->full_w, g->full_h, &w, &h);
	return h;
}

bool RowView_drawConsoleLogo(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
							 Uint8 a, bool selected) {
	if (!sprite_mode || g->kind != ROW_BACKDROP_LOGO || kind != TILE_LOGO)
		return false;
	char logo[64];
	const char* file = entryLogoFile(e, logo, sizeof(logo));
	if (!file)
		return false; // a logo-less console: its name slot, as before
	if (selected) {	  // the crisp one once it lands (asked for here, first in line); the level until then
		bool pending;
		if (MenuArt_peek(file, g->full_w, g->full_h, &pending)) {
			blitCentred(screen, slotItem(g, e, kind, false), cx, cy, scale, a, 255);
			return true;
		}
		if (pending)
			art_waiting = true;
	}
	bool pending;
	LogoMip* m = logoMip(file, &pending);
	if (!m)
		return true; // decoding (it shows once it lands) or nothing to show
	int lw, lh;
	logoFit(m, g->full_w, g->full_h, &lw, &lh);
	int dw = (int)(lw * scale + 0.5f), dh = (int)(lh * scale + 0.5f);
	if (dw <= 0 || dh <= 0 || a == 0)
		return true;
	SDL_Surface* src = m->small && m->small->w >= dw && m->small->h >= dh ? m->small : m->level;
	SDL_Texture* t = PLAT_textureForSurface(src);
	if (t) // the off-white is in the PNG; the tab-focus dim, clipped as the screen is
		PLAT_spriteAdd(t, NULL, &(SDL_Rect){cx - dw / 2, cy - dh / 2, dw, dh}, (Uint8)(a * MenuTabs_contentAlpha() + 0.5f),
					   &screen->clip_rect);
	return true;
}

// Consoles: the count line's top (px), 8 dp under what the selected item draws in its slot: its logo (lh: its height,
// logoHeight) or, with none (lh < 0), its name.
static int logoCountTop(const RowGeo* g, Entry* e, int lh) {
	int drawn = lh >= 0 ? lh : logoNameHeight(View_displayName(e), g->full_w, g->full_h, g->k);
	return (int)floorf(Row_logoCountY((float)g->cy, (float)drawn, (float)NX_DPF(ROW_LOGO_COUNT_GAP_DP)) + 0.5f);
}

// A Vertical stack's Consoles count belongs to its item (stackview.c, device fix round 2): up to two items show one
// at once (the outgoing and incoming selection), so their texts are kept apart, a few at a time, never re-rendered per
// frame while the stack slides.
#define ITEM_COUNT_SLOTS 16 // the Vertical stack's two, and a Horizontal row's visible neighbours
static struct {
	char key[96];
	SDL_Surface* s;
	unsigned stamp;
} item_counts[ITEM_COUNT_SLOTS];
static unsigned item_count_clock = 0;

static SDL_Surface* itemCountSurface(const char* text, float sp) {
	if (!text || !text[0])
		return NULL;
	SDL_Color ac = countGrey();
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
		GFX_freeSurfaceAndTexture(item_counts[victim].s);
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
			GFX_freeSurfaceAndTexture(item_counts[i].s);
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
	bool pending;
	int lh = logoHeight(g, e, kind, &pending);
	if (pending)
		return;
	int drawn = lh >= 0 ? lh : logoNameHeight(View_displayName(e), g->full_w, g->full_h, g->k);
	if (lh < 0 && g->vertical && drawn < g->full_h / 2)
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

// A Horizontal Collections item's count-line centre below its slot centre (px at the full size): collLayout's, kept
// per item so the neighbours don't re-measure their names every frame.
#define SIDE_OFF_SLOTS 16
static struct {
	char path[MAX_PATH];
	int w, h;
	float k;
	int off;
} side_offs[SIDE_OFF_SLOTS];
static int side_off_next = 0;

static int collCountOff(const RowGeo* g, Entry* e) {
	for (int i = 0; i < SIDE_OFF_SLOTS; i++) {
		if (side_offs[i].w == g->full_w && side_offs[i].h == g->full_h && side_offs[i].k == g->k &&
			strcmp(side_offs[i].path, e->path) == 0)
			return side_offs[i].off;
	}
	CollLayout c = collLayout(View_displayName(e), g->full_w, g->full_h, g->k, 1.0f, g->vertical);
	TTF_Font* fc = UIFont_get(Row_countSp(g->k), false);
	int off = c.t.count_y + (fc ? TTF_FontHeight(fc) : 0) / 2 - g->full_h / 2;
	int i = side_off_next;
	side_off_next = (side_off_next + 1) % SIDE_OFF_SLOTS;
	snprintf(side_offs[i].path, sizeof(side_offs[i].path), "%s", e->path);
	side_offs[i].w = g->full_w, side_offs[i].h = g->full_h, side_offs[i].k = g->k, side_offs[i].off = off;
	return off;
}

// A Horizontal Consoles or Collections neighbour's "N games" (every visible item but the selection, which drawCount
// draws): at ROW_SIDE_COUNT_SCALE of the selection's text size (rendered at that size, never scaled down to the item's
// 0.4-0.5), riding with its item at its alpha: 8 dp × scale under a console's logo as drawn, on a collection's line.
#define ROW_SIDE_COUNT_SCALE 0.75f
static void drawSideCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, float scale,
						  float alpha) {
	if (g->vertical || (g->kind != ROW_BACKDROP_LOGO && g->kind != ROW_BACKDROP_COLL))
		return;
	Uint8 a = (Uint8)(255.0f * alpha + 0.5f);
	if (!a)
		return;
	char text[32];
	countLabel(e, kind, text, sizeof(text));
	if (!text[0])
		return;
	int centre;
	if (g->kind == ROW_BACKDROP_LOGO) {
		bool pending;
		int lh = logoHeight(g, e, kind, &pending);
		if (pending)
			return;
		int drawn = lh >= 0 ? lh : logoNameHeight(View_displayName(e), g->full_w, g->full_h, g->k);
		SDL_Surface* s = itemCountSurface(text, Row_countSp(g->k) * ROW_SIDE_COUNT_SCALE);
		if (!s)
			return;
		int top = g->cy + (int)((drawn / 2.0f + NX_DPF(ROW_LOGO_COUNT_GAP_DP)) * scale + 0.5f);
		blitCentred(screen, s, cx, top + s->h / 2, 1.0f, a, 255);
		return;
	}
	centre = g->cy + (int)(collCountOff(g, e) * scale + 0.5f);
	SDL_Surface* s = itemCountSurface(text, Row_countSp(g->k) * ROW_SIDE_COUNT_SCALE);
	if (s)
		blitCentred(screen, s, cx, centre, 1.0f, a, 255);
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
		bool pending;
		int lh = logoHeight(g, e, kind, &pending);
		if (pending) { // placed once its logo lands (sel differs from cnt.sel until then); drawCount shows none
			cnt.pending = true;
			return;
		}
		cnt.pending = false;
		if (!snap && sel == cnt.sel && lh == cnt.art_h)
			return;
		retargetCount(logoCountTop(g, e, lh), snap || cnt.sel < 0);
		cnt.art_h = lh;
	} else {
		if (!snap && sel == cnt.sel)
			return;
		CollLayout c = collLayout(View_displayName(e), g->full_w, g->full_h, g->k, 1.0f, g->vertical);
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
		if (cnt.pending)
			return;
		SDL_Surface* s = countSurface(text, Row_countSp(g->k), countGrey());
		if (!s)
			return;
		float p = cnt.glide.active ? UI_easeStandard(tweenProgress(&cnt.glide, ROW_COUNT_GLIDE_MS)) : 1.0f;
		int y = (int)floorf(cnt.y_from + (cnt.y_to - cnt.y_from) * p + 0.5f);
		blitCentred(screen, s, g->cx, y + s->h / 2, 1.0f, 255, 255);
		return;
	}
	// Collections: on the selected item, wherever the slide has it, at its scale and alpha
	SDL_Surface* s = countSurface(text, Row_countSp(g->k), countGrey());
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

// rows of the target's texture uploaded so far (pictureUploadStep): its crossfade starts once all of them are
static int to_up_y = 0;

static void toTexFree(void) {
	to_up_y = 0;
	to_flattened = false;
	PLAT_freeTexture(to_tex);
	to_tex = NULL;
	to_tex_src = NULL;
	to_tex_gen = 0;
}

static void picLayersClear(void) {
	flat_cur = -1;
	flat_pic = false;
	to_flattened = false;
}

static void picLayersFree(void) {
	picLayersClear();
	for (int i = 0; i < 2; i++) {
		PLAT_freeTexture(flat_tex[i]);
		flat_tex[i] = NULL;
	}
	flat_w = flat_h = 0;
}

// A layer over the stack at alpha a (t NULL: black), drawn into the next flat texture over the current one (once: the
// composite then draws a single opaque layer). Doesn't take t.
static void picLayerPush(SDL_Surface* screen, SDL_Texture* t, int w, int h, Uint8 a) {
	if (a == 0 || (!t && flat_cur < 0))
		return; // nothing shows of it, or black over black
	if (flat_w != screen->w || flat_h != screen->h) {
		picLayersFree();
		flat_w = screen->w;
		flat_h = screen->h;
	}
	int next = flat_cur == 0 ? 1 : 0;
	if (!flat_tex[next])
		flat_tex[next] = PLAT_targetCreate(flat_w, flat_h);
	if (!flat_tex[next]) { // (no target: black from here on)
		picLayersClear();
		return;
	}
	SDL_Rect full = {0, 0, flat_w, flat_h};
	PLAT_targetBegin(flat_tex[next]); // black
	if (flat_cur >= 0 && a < 255)
		PLAT_targetDraw(flat_tex[flat_cur], &full, 255);
	if (t)
		PLAT_targetDraw(t, &(SDL_Rect){(flat_w - w) / 2, (flat_h - h) / 2, w, h}, a);
	else
		PLAT_targetDraw(blackTexture(), &full, a);
	PLAT_targetEnd();
	flat_pic = t ? true : (a < 255 && flat_pic);
	flat_cur = next;
}

static void resetPicture(void) {
	picLayersFree();
	toTexFree();
	pic_on = false;
	from_valid = false;
	to_set = false;
	fade_started = false;
	fade_tw.active = false;
	to_path[0] = '\0';
	pic_top = 0;
	if (pic_from) { // 3-4 MB: only held on a Backdrop game row
		GFX_freeSurfaceAndTexture(pic_from);
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
			GFX_freeSurfaceAndTexture(pic_from);
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

// settlePicture's sprite-mode counterpart: the target as it shows now becomes a layer of the from stack (its texture
// moves there), frozen at the alpha its crossfade reached; a target that never showed leaves the stack as it is.
static void settlePictureSprites(SDL_Surface* screen) {
	if (!fade_started) {
		toTexFree();
		return;
	}
	Uint8 a = (Uint8)(fadeProgress() * 255.0f + 0.5f);
	if (!(to_flattened && a == 255))						 // (a finished target is in the stack already)
		picLayerPush(screen, to_tex, to_tex_w, to_tex_h, a); // NULL (no picture): black
	toTexFree();
	to_flattened = false;
}

// The picture is a game list's (Backdrop box row) only: a main-menu tab never has one.
static bool pictureRow(void) {
	return top && stack->count > 1 && RowView_active() && currentKind() == ROW_BACKDROP_BOX;
}

bool RowView_gameSprites(void) {
	if (!top || !stack || stack->count <= 1 || !RowView_active() || ContextMenu_isOpen() || top->entries->count <= 0)
		return false;
	RowKind k = currentKind(); // a game list's (a main-menu tab's kinds are the Consoles, Collections and Tools ones)
	return k == ROW_CAROUSEL || k == ROW_BACKDROP_BOX;
}

bool RowView_hasPicture(void) {
	return pictureRow();
}

bool RowView_paintsScreen(void) {
	return pictureRow() && !RowView_gameSprites();
}

// One layer of the picture under the screen: its texture (NULL: black over the whole screen) centred, at alpha a
static void pictureSprite(SDL_Surface* screen, SDL_Texture* t, int w, int h, Uint8 a) {
	SDL_Rect r = {0, 0, screen->w, screen->h};
	if (t)
		r = (SDL_Rect){(screen->w - w) / 2, (screen->h - h) / 2, w, h};
	else
		t = blackTexture();
	if (t && a)
		PLAT_spriteAddUnder(t, NULL, &r, a, NULL);
}

// Sprite mode's picture: the from stack over black, the target over it at the crossfade's alpha (black when it has no
// picture), all under the screen, whose body this frame leaves transparent. The target's texture is made the frame its
// picture resolves. Returns whether a picture shows (as RowView_renderPicture).
// The target's texture, made when its picture resolves and filled a band a frame: a full-screen upload in one go
// (3-4 MB) cost ~10 ms on the Brick, a late frame each time a held D-pad settled on a new game. Four bands ~2.5 ms each,
// and the crossfade waits for the last (~4 frames, unseen at the fade's start). Returns whether the upload is still
// under way (the fade must not start yet).
static bool pictureUploadStep(HomeArtState st, SDL_Surface* to, unsigned gen) {
	if (st == HOMEART_READY && to && (to != to_tex_src || gen != to_tex_gen)) {
		toTexFree();
		bool bands = to->format->format == SDL_PIXELFORMAT_ARGB8888;
		// (a failure: black, not retried)
		to_tex = bands ? PLAT_textureCreate(to->w, to->h) : PLAT_textureFromSurface(to);
		to_tex_src = to;
		to_tex_gen = gen;
		to_tex_w = to->w;
		to_tex_h = to->h;
		to_up_y = bands ? 0 : to->h;
	} else if (st == HOMEART_NONE) {
		toTexFree(); // none (any more): black. While a re-decode is LOADING the old picture stays.
		return false;
	}
	if (!to_tex || to_up_y >= to_tex_h)
		return false;
	if (st != HOMEART_READY || to != to_tex_src) // the surface went (evicted mid-upload): start over when it's back
		return true;
	int band = (to_tex_h + PIC_UPLOAD_BANDS - 1) / PIC_UPLOAD_BANDS;
	PLAT_textureUpdateRows(to_tex, to, to_up_y, band);
	to_up_y += band;
	return to_up_y < to_tex_h;
}

static bool drawPictureSprites(SDL_Surface* screen, HomeArtState st, SDL_Surface* to, unsigned gen, float p) {
	(void)st, (void)to, (void)gen; // the target's texture: pictureUploadStep
	bool to_ready = to_tex && to_up_y >= to_tex_h;
	Uint8 a = (Uint8)(p * 255.0f + 0.5f);
	// a finished crossfade: the target becomes the stack, alone (one opaque layer from then on)
	if (a == 255 && !to_flattened) {
		picLayersClear();
		picLayerPush(screen, to_ready ? to_tex : NULL, to_tex_w, to_tex_h, 255);
		to_flattened = true;
	}
	SDL_Rect full = {0, 0, screen->w, screen->h};
	// the stack (or black): opaque over the whole screen, so the background layers under it aren't drawn
	SDL_Texture* base = flat_cur >= 0 ? flat_tex[flat_cur] : blackTexture();
	if (base)
		PLAT_spriteAddUnderOpaque(base, &full);
	if (p > 0.0f && a < 255)
		pictureSprite(screen, to_ready ? to_tex : NULL, to_tex_w, to_tex_h, a);
	return (p < 1.0f && flat_pic) || (to_ready && p > 0.0f);
}

bool RowView_renderPicture(SDL_Surface* screen) {
	pic_on = false;
	if (!screen || !pictureRow()) {
		if (to_set || pic_from || to_tex || flat_cur >= 0 || flat_tex[0] || flat_tex[1])
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
	// sprite mode (RowView_gameSprites) or the CPU's, as the frame is drawn: a switch carries the from layer over as
	// far as it can. Into sprites the CPU's from picture becomes the stack's base (a texture of it); out of sprites the
	// stack isn't on the CPU, so a crossfade cut short there starts from black (only under a context menu opened mid-fade).
	bool spr = RowView_gameSprites();
	if (spr != pic_sprites) {
		picLayersClear();
		toTexFree();
		if (spr && from_valid && pic_from && fadeProgress() < 1.0f) { // (a finished crossfade's target covers it)
			SDL_Texture* t = PLAT_textureFromSurface(pic_from);
			picLayerPush(screen, t, pic_from->w, pic_from->h, 255);
			PLAT_freeTexture(t);
		}
		from_valid = false;
		if (pic_from) { // sprite mode keeps no CPU copy (3-4 MB); the CPU's next settle makes it again
			GFX_freeSurfaceAndTexture(pic_from);
			pic_from = NULL;
		}
		pic_sprites = spr;
	}
	const char* want = "";
	if (n > 0) {
		int sel = View_selectedIndex(n);
		Entry* e = top->entries->items[sel];
		if (kindFor(sel, e) == TILE_GAME)
			want = e->path;
	}
	if (!to_set || strcmp(want, to_path) != 0) {
		if (to_set && spr)
			settlePictureSprites(screen);
		else if (to_set)
			settlePicture(screen);
		else
			toTexFree();
		snprintf(to_path, sizeof(to_path), "%s", want);
		to_set = true;
		fade_started = false;
		fade_tw.active = false;
	}

	SDL_Surface* to = NULL;
	HomeArtState st = to_path[0] ? HomeArt_backdrop(to_path, screen->w, screen->h, &to) : HOMEART_NONE;
	unsigned gen = to_path[0] ? HomeArt_lastGen() : 0; // the slot of that picture: keys its texture (sprite mode)
	bool uploading = spr && pictureUploadStep(st, to, gen);
	if (st != HOMEART_LOADING && !uploading && !fade_started) {
		fade_started = true;
		tweenStart(&fade_tw); // animations off: inactive, so the progress is 1 at once
	}
	float p = fadeProgress();
	if (spr) {
		pic_on = drawPictureSprites(screen, st, to, gen, p);
		return pic_on;
	}
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

// Whether a build ahead would stall the picture's frames: its crossfade on the CPU (each frame a full-screen pass), or
// B's fade out. In sprite mode the crossfade is the GPU's, so a held D-pad's steps (which keep it running) still get
// their neighbours built.
static void prefetchTexture(SDL_Surface* s, bool sprites);
static bool captionTexture(SDL_Surface* c, Uint32 deadline);

// a first guess per kind, then what the jobs ahead took (only theirs: a caption made in a frame, under its input and
// render and the art threads' load, ran up to twice as long and locked prefetch out)
static float job_cost_ms[PF_JOB_COUNT] = {5.0f, 3.0f};

bool RowView_jobFits(Uint32 deadline, PrefetchJob job) {
	return (Sint32)(deadline - SDL_GetTicks()) >= (Sint32)(job_cost_ms[job] + 0.5f);
}

void RowView_jobDone(PrefetchJob job, Uint32 t0) {
	Uint32 d = SDL_GetTicks() - t0;
	if (d >= 1) // built something (a hit costs nothing): a moving average of what they took
		job_cost_ms[job] = 0.75f * job_cost_ms[job] + 0.25f * d;
}

bool RowView_prefetchBlocked(void) {
	return exit_fade.active || (fade_tw.active && !RowView_gameSprites());
}

bool RowView_prefetchSideCaption(const RowGeo* g, Entry* e, TileKind kind, int w, int body_h, bool right,
								 Uint32 deadline) {
	Uint32 t0 = SDL_GetTicks();
	SDL_Surface* c = sideCaption(g, e, kind, w, body_h, right);
	RowView_jobDone(PF_JOB_CAPTION, t0);
	return captionTexture(c, deadline);
}

// A caption made ahead, then its texture as a job of its own (sprite mode): the two together rarely fit what's left of
// a frame. Returns false when the texture didn't fit (the caller stops for this frame; a later one finds the caption
// kept and uploads it).
static bool captionTexture(SDL_Surface* c, Uint32 deadline) {
	if (!c || !RowView_gameSprites() || c->userdata)
		return true;
	if (!RowView_jobFits(deadline, PF_JOB_ITEM))
		return false;
	PLAT_textureForSurface(c);
	return true;
}

bool RowView_captionStale(void) {
	return cap_refresh;
}

void RowView_captionRefreshed(void) {
	cap_refresh = false;
	cap_redraw = true;
}

// One more frame once the selection's caption was made between frames (it shows on the next)
static bool takeCaptionRedraw(void) {
	bool r = cap_redraw;
	cap_redraw = false;
	return r;
}

bool RowView_exitStep(bool key_pressed) {
	return RowView_exiting() && MenuTransition_exitStep(&exit_fade, SDL_GetTicks(), key_pressed);
}

void RowView_renderExit(SDL_Surface* screen) {
	if (!screen || !RowView_exiting())
		return;
	float d = MenuTransition_exitDarkness(&exit_fade, SDL_GetTicks());
	if (d <= 0.0f)
		return;
	Uint8 a = (Uint8)(d * 255.0f + 0.5f);
	SDL_Texture* black = game_frame_sprites ? blackTexture() : NULL;
	if (black) // sprite mode: black over everything as the last sprite (the screen, its bars included, is under it)
		PLAT_spriteAdd(black, NULL, &(SDL_Rect){0, 0, screen->w, screen->h}, a, NULL);
	else
		UI_dimRect(screen, NULL, a); // one pass, only while the fade runs
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

static bool prefetchItems(const RowGeo* g, int n, int sel, Uint32 deadline);

// A surface built ahead, and in sprite mode its texture too (between frames, not in the frame that first shows it)
static void prefetchTexture(SDL_Surface* s, bool sprites) {
	if (s && sprites)
		PLAT_textureForSurface(s);
}

// One item ahead: a Carousel tile (plain or lit), a box art's neighbour-size copy or the placeholder box, a slot.
static void prefetchOne(const RowGeo* g, Entry* e, TileKind k, bool side, bool lit) {
	bool sprites = (g->kind == ROW_CAROUSEL || g->kind == ROW_BACKDROP_BOX) && RowView_gameSprites();
	if (g->kind == ROW_CAROUSEL && sprites && k == TILE_GAME) {
		carouselGameWarm(g, e, side); // no composed look in sprite mode (carouselGameSprites)
	} else if (g->kind == ROW_CAROUSEL) {
		prefetchTexture(carouselTile(g, e, k, side ? g->side_w : g->full_w, side ? g->side_h : g->full_h, lit),
						sprites);
	} else if (lit) {
		return; // a Carousel look only
	} else if (g->kind == ROW_BACKDROP_BOX) {
		SDL_Surface* art = NULL;
		HomeArtState st =
			k == TILE_GAME ? HomeArt_boxart(e->path, g->full_w, g->full_h, &art, NULL, NULL) : HOMEART_NONE;
		unsigned gen = HomeArt_lastGen(); // the slot of that art (0 when not looked up: art is then NULL)
		if (st == HOMEART_READY && art)	  // HomeArt frees a texture on its surface with it (PLAT_freeSurfaceTexture)
			prefetchTexture(side && !sprites ? sideArt(g, e, art, gen) : art, sprites);
		else if (st != HOMEART_LOADING)
			prefetchTexture(placeholderItem(g, e, k, side && !sprites), sprites);
	} else if (g->kind == ROW_BACKDROP_LOGO) {
		// Consoles draws as GPU sprites (RowView_beginSprites): its logos are the level and its half scaled by the
		// GPU (RowView_drawConsoleLogo), so no slot or side copy; their textures are made here, between frames, not in
		// the frame that first shows them
		char logo[64];
		bool pending;
		LogoMip* m = k == TILE_LOGO && entryLogoFile(e, logo, sizeof(logo)) ? logoMip(logo, &pending) : NULL;
		if (m) {
			PLAT_textureForSurface(m->level);
			if (m->small)
				PLAT_textureForSurface(m->small);
		} else if (k != TILE_LOGO && !side) {
			slotItem(g, e, k, false); // a logo-less console's name slot
		}
	} else {
		slotItem(g, e, k, side);
	}
}

// Consoles: the logos and controllers within ART_WARM_RADIUS of the selection, queued on the art loader's thread
// (nearest first, the controllers a step ahead of the logos), once per selection, list or size. The draw and the
// item builds then find them decoded, so a slide never stops for a PNG.
#define ART_WARM_RADIUS 6
void RowView_warmConsoleArt(int slot_w, int slot_h, int pad_w, int pad_h, int sel) {
	static unsigned seen_serial;
	static unsigned seen_gen;
	static int seen_sel = -1, seen_n, seen_sw, seen_sh, seen_pw, seen_ph;
	int n = top->entries->count;
	if (top->serial == seen_serial && MenuTabs_generation() == seen_gen && sel == seen_sel && n == seen_n &&
		slot_w == seen_sw && slot_h == seen_sh && pad_w == seen_pw && pad_h == seen_ph)
		return;
	seen_serial = top->serial, seen_gen = MenuTabs_generation(), seen_sel = sel, seen_n = n;
	seen_sw = slot_w, seen_sh = slot_h, seen_pw = pad_w, seen_ph = pad_h;
	for (int d = 0; d <= ART_WARM_RADIUS; d++) {
		for (int side = -1; side <= 1; side += 2) {
			int i = sel + side * d;
			if (i < 0 || i >= n || (d == 0 && side > 0))
				continue;
			Entry* e = top->entries->items[i];
			TileKind k = kindFor(i, e);
			if (k != TILE_LOGO)
				continue;
			const char* pad = RowView_padId(e, k);
			if (pad && pad_w > 0 && pad_h > 0) {
				char file[64];
				snprintf(file, sizeof(file), "menu_pad_%s.png", pad);
				ArtLoader_request(file, pad_w, pad_h, 2 * d);
			}
			char logo[64];
			if (entryLogoFile(e, logo, sizeof(logo))) {
				ArtLoader_request(logo, 0, 0, 2 * d + 1); // the level (RowView_drawConsoleLogo)
				if (d == 0)								  // and the selection's crisp one
					ArtLoader_request(logo, slot_w, slot_h, 0);
			}
		}
	}
}

void RowView_prefetchPad(Entry* e, TileKind kind, int box_w, int box_h) {
	const char* id = RowView_padId(e, kind);
	SDL_Surface* s = id ? ControllerArt_carousel(id, box_w, box_h) : NULL;
	if (s) // its texture between frames too (the pad draws as a GPU sprite)
		PLAT_textureForSurface(s);
}

int RowView_countLineH(const RowGeo* g) {
	TTF_Font* f = UIFont_get(Row_countSp(g->k), false);
	return f ? TTF_FontHeight(f) : 0;
}

void RowView_drawPad(SDL_Surface* screen, Entry* e, TileKind kind, int box_w, int box_h, int cx, int cy, float scale,
					 float d) {
	float a = Pad_alpha(d);
	const char* id = a > 0 ? RowView_padId(e, kind) : NULL;
	if (!id)
		return;
	SDL_Surface* s = ControllerArt_carousel(id, box_w, box_h);
	if (s)
		blitCentred(screen, s, cx, cy, scale, (Uint8)(a * 255.0f + 0.5f), 255);
	else if (ControllerArt_pending())
		art_waiting = true;
}

void RowView_render(SDL_Surface* screen, int lastScreen) {
	pf.armed = false;
	if (!screen || !top)
		return;
	int bar = barPx();
	int body_h = screen->h - 2 * bar;
	RowKind kind = currentKind();
	// A game list's Carousel or Backdrop as GPU sprites (both orientations): the body is only filled when it may hold
	// something else (bodyFill), the Carousel's plain black (and under the hint bar's scrim, every frame: the host
	// clears the bars' rows), the Backdrop's transparent over its picture, under the screen. Consoles' sprites keep
	// their black fill every frame.
	bool game_sprites = RowView_gameSprites();
	bool consoles_sprites = kind == ROW_BACKDROP_LOGO && !ContextMenu_isOpen();
	game_frame_sprites = game_sprites;
	if (game_sprites) {
		bodyFill(screen, kind == ROW_CAROUSEL ? BODY_BLACK : BODY_CLEAR, lastScreen);
		if (kind == ROW_CAROUSEL)
			SDL_FillRect(screen, &(SDL_Rect){0, screen->h - bar, screen->w, bar},
						 SDL_MapRGBA(screen->format, 0, 0, 0, 255));
	} else if (!pictureRow()) {
		// plain black under the row (and under the hint bar's scrim); a Backdrop game list's picture is already drawn
		SDL_FillRect(screen, &(SDL_Rect){0, bar, screen->w, screen->h - bar},
					 SDL_MapRGBA(screen->format, 0, 0, 0, 255));
		if (consoles_sprites) {
			body_changed = body_changed || body_left != BODY_BLACK || lastScreen != SCREEN_GAMELIST;
			body_now = BODY_BLACK;
		}
	}

	// the Vertical orientation (§8f) draws its own stack over the same black; the row snaps when it shows again
	if (StackView_active()) {
		seen_top = 0;
		pf.armed = false;
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

	int sel = View_selectedIndex(n);
	// the slide toward the selection, retargeted from where the row is now
	if (snap || !animationsOn()) {
		pos_from = pos_to = (float)sel;
		slide_tw.active = false;
	} else if ((float)sel != pos_to) {
		slide_retargeted = slide_tw.active && tweenProgress(&slide_tw, SLIDE_MS) < 1.0f;
		pos_from = currentPos();
		pos_to = (float)sel;
		tweenStart(&slide_tw);
	}
	float pos = currentPos();

	RowGeo g;
	computeGeo(screen, kind, &g);

	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	// a hidden page title's row and hint bar stay open: what reaches into them still shows
	int vis_top = CFG_getPageTitle() ? bar : 0, vis_bottom = CFG_getButtonHints() ? screen->h - bar : screen->h;
	SDL_SetClipRect(screen, &(SDL_Rect){0, vis_top, screen->w, vis_bottom - vis_top});
	PadSize pad_box = Pad_rowBox((float)g.full_h); // 3.0 x 1.95 the logo slot's height
	int pad_w = (int)(pad_box.w + 0.5f), pad_h = (int)(pad_box.h + 0.5f);
	if (kind == ROW_BACKDROP_LOGO)
		RowView_warmConsoleArt(g.full_w, g.full_h, pad_w, pad_h, sel);

	// Consoles and a game list's Carousel and Backdrop: the pictures, captions and counts go to the GPU
	// (RowView_beginSprites), the screen's body stays as bodyFill left it (not under a context menu: it draws over the
	// body on the screen, under the sprites)
	RowView_beginSprites(consoles_sprites || game_sprites);

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
		else {
			if (kind == ROW_BACKDROP_LOGO) // the focused console's controller, under its logo
				RowView_drawPad(screen, e, k, pad_w, pad_h, cx, g.cy, it.scale, d);
			draw_selected = i == sel;
			drawBackdropItem(screen, &g, e, k, cx, g.cy, it.scale, it.alpha);
			draw_selected = false;
			if (i != sel)
				drawSideCount(screen, &g, e, k, cx, it.scale, it.alpha);
		}
	}

	// the selection's caption (its game info requested last: GameInfo's queue keeps the latest request)
	Entry* e = top->entries->items[sel];
	drawCaption(screen, &g, e, kindFor(sel, e));
	RowItem sel_it = Row_item(&g.sz, kind, (float)sel, pos);
	RowPlace at = {dpToPx(sel_it.dx), 0, sel_it.scale, sel_it.alpha, sel_it.visible};
	drawCount(screen, &g, e, kindFor(sel, e), sel, &at, snap);
	sprite_mode = false;

	SDL_SetClipRect(screen, &prev_clip);

	// what's ahead of this row is built between frames (RowView_prefetchStep), for this geometry and selection
	pf.armed = true;
	pf.g = g;
	pf.n = n;
	pf.sel = sel;
}

// The items the next step in either direction draws first: the neighbours at the centre size (they grow into the
// centre; plain and lit for a Carousel tile) and the items that come into view at d = 4 (a neighbour's size). Builds
// until done or the deadline; true when it stopped with more to build.
static bool prefetchItems(const RowGeo* g, int n, int sel, Uint32 deadline) {
	const struct {
		int di;
		bool side, lit;
	} jobs[] = {{1, false, false}, {1, false, true}, {-1, false, false}, {-1, false, true}, {4, true, false}, {-4, true, false}};
	bool skipped = false;
	// the selection's caption first when the frame showed an older one of it (cap_refresh)
	if (cap_refresh && screen && sel >= 0 && sel < n && g->cap != CAP_NONE) {
		if (!RowView_jobFits(deadline, PF_JOB_CAPTION))
			return true;
		Uint32 t0 = SDL_GetTicks();
		Entry* e = top->entries->items[sel];
		CapStyle cs;
		SDL_Surface* c = rowCaption(screen, g, e, kindFor(sel, e), &cs);
		RowView_jobDone(PF_JOB_CAPTION, t0);
		RowView_captionRefreshed();
		if (!captionTexture(c, deadline))
			return true;
	}
	// the neighbours' captions (and in sprite mode their textures): the next step shows one of them at once
	for (int di = 1; g->cap != CAP_NONE && screen && di >= -1; di -= 2) {
		int i = sel + di;
		if (i < 0 || i >= n)
			continue;
		if (!RowView_jobFits(deadline, PF_JOB_CAPTION)) {
			skipped = true; // the items still get their turn: the caption waits for a roomier frame
			break;
		}
		Uint32 t0 = SDL_GetTicks();
		Entry* e = top->entries->items[i];
		CapStyle cs;
		SDL_Surface* c = rowCaption(screen, g, e, kindFor(i, e), &cs);
		RowView_jobDone(PF_JOB_CAPTION, t0);
		if (!captionTexture(c, deadline)) {
			skipped = true;
			break;
		}
	}
	for (size_t j = 0; j < sizeof(jobs) / sizeof(jobs[0]); j++) {
		int i = sel + jobs[j].di;
		if (i < 0 || i >= n)
			continue;
		if (!RowView_jobFits(deadline, PF_JOB_ITEM))
			return true;
		Uint32 t0 = SDL_GetTicks();
		Entry* e = top->entries->items[i];
		prefetchOne(g, e, kindFor(i, e), jobs[j].side, jobs[j].lit);
		RowView_jobDone(PF_JOB_ITEM, t0);
	}
	if (g->kind == ROW_BACKDROP_LOGO) { // the neighbours' controllers, which show as soon as a step starts
		PadSize box = Pad_rowBox((float)g->full_h);
		for (int di = -1; di <= 1; di += 2) {
			int i = sel + di;
			if (i < 0 || i >= n)
				continue;
			if (RowView_pastDeadline(deadline))
				return true;
			Entry* e = top->entries->items[i];
			RowView_prefetchPad(e, kindFor(i, e), (int)(box.w + 0.5f), (int)(box.h + 0.5f));
		}
	}
	return skipped;
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
	int sel = n > 0 ? View_selectedIndex(n) : 0;
	int next = sel + dir;
	if (n > 0 && next >= 0 && next < n) {
		top->selected = next;
		// keep the List window consistent for a later switch back to List: the selection at the top, clamped
		ListWindow_selectAtTop(n, GameList_rowCount(), next, &top->start, &top->end);
		*dirty = true;
	} else if (stack->count == 1 && PAD_justPressed(btn)) {
		// the main menu: past an end, a fresh press switches tab; a held key stops (game lists always stop)
		GameList_switchTab(dir, dirty);
	}
	return true;
}

bool RowView_animating(void) {
	if (!RowView_active()) {
		pf.armed = false;
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
		bool b = tweenTick(&fade_tw, FADE_MS);
		bool c = tweenTick(&cnt.glide, ROW_COUNT_GLIDE_MS);
		bool d = tweenTick(&cnt.fade, ROW_COUNT_FADE_MS);
		bool s = StackView_animating();
		bool cap = cap_refresh || takeCaptionRedraw(); // the selection's caption being made between frames
		return b || c || d || s || cap || art_waiting || RowView_exiting();
	}
	bool a = tweenTick(&slide_tw, SLIDE_MS);
	bool b = tweenTick(&fade_tw, FADE_MS);
	bool c = tweenTick(&cnt.glide, ROW_COUNT_GLIDE_MS);
	bool d = tweenTick(&cnt.fade, ROW_COUNT_FADE_MS);
	if (a && !slide_tw.active)
		pos_from = pos_to;
	bool cap = cap_refresh || takeCaptionRedraw(); // the selection's caption being made between frames
	return a || b || c || d || cap || art_waiting || RowView_exiting();
}

bool RowView_prefetchStep(Uint32 deadline) {
	if (StackView_active())
		return StackView_prefetchStep(deadline); // the Vertical orientation builds its own stack's
	if (!pf.armed)
		return false;
	// the row the geometry was worked out for is gone (a new list, tab, size, scale or kind not drawn yet, or a moved
	// selection): nothing until the next render re-arms it
	if (!RowView_active() || !top || !screen || top->serial != seen_top || MenuTabs_generation() != seen_gen ||
		top->entries->count != pf.n || screen->w != seen_screen_w || (int)FIXED_SCALE != seen_scale ||
		(int)currentKind() != seen_kind || View_selectedIndex(pf.n) != pf.sel) {
		pf.armed = false;
		return false;
	}
	// not while the picture crossfades or fades out (a build would stall its frames), nor through the first part of a
	// single step's slide; past PREFETCH_SLIDE_SHARE, or on a held D-pad (its repeats retarget the slide before it
	// ever gets that far), the target's neighbours are built so the next steps land on cached items
	if (RowView_prefetchBlocked() ||
		(slide_tw.active && !slide_retargeted && tweenProgress(&slide_tw, SLIDE_MS) < PREFETCH_SLIDE_SHARE))
		return true;
	if (prefetchItems(&pf.g, pf.n, pf.sel, deadline))
		return true;
	pf.armed = false;
	return false;
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
							 int body_top, int body_h, bool right) {
	drawSideCaption(screen, g, e, kind, x, w, cy, body_top, body_h, right);
}

void RowView_drawItemCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
						   float d) {
	drawItemCount(screen, g, e, kind, cx, cy, scale, d);
}

void RowView_quit(void) {
	pf.armed = false;
	free(kinds);
	kinds = NULL;
	kinds_cap = 0;
	kinds_top = 0;
	kinds_n = -1;
	itemsClear();
	mipsClear();
	captionsClear();
	if (cnt.s)
		GFX_freeSurfaceAndTexture(cnt.s);
	memset(&cnt, 0, sizeof(cnt));
	cnt.sel = -1;
	resetPicture();
	MenuTransition_exitCancel(&exit_fade);
	if (stretch_scratch)
		GFX_freeSurfaceAndTexture(stretch_scratch);
	stretch_scratch = NULL;
	GFX_freeSurfaceAndTexture(black_px);
	black_px = NULL;
	tileOverlaysClear();
	GFX_freeSurfaceAndTexture(ph_shadow);
	ph_shadow = NULL;
	StackView_forget();
	itemCountsClear();
}
