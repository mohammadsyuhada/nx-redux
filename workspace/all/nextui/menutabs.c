// Main menu tabs: the current tab is the Directory at stack[0] (see
// docs/superpowers/specs/2026-09-30-main-menu-tabs-design.md). The rules
// live in menutabs_model.c; this file applies them to the stack and draws
// the tab row.

#include "menutabs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "utils.h"

#include "content.h"
#include "gamelist.h"
#include "home.h"
#include "launcher.h"
#include "menu_transition.h"
#include "recents.h"
#include "shortcuts.h"
#include "ui_accent.h"
#include "ui_ease.h"
#include "ui_list.h"

static MenuTabId tabs[MENU_TAB_COUNT];
static int tab_count = 0;
static MenuTabId current = MENU_TAB_HOME;
static bool simple_mode = false;

// Per-tab remembered selection, restored on L1/R1 back to a tab.
typedef struct {
	bool valid;
	int selected, start, end;
} TabMemory;
static TabMemory remembered[MENU_TAB_COUNT];

static bool settingsPakExists(void) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/Settings.pak", TOOLS_PATH);
	if (exists(path))
		return true;
	snprintf(path, sizeof(path), "%s/Tools/Settings.pak", PAKS_PATH);
	return exists(path);
}

void MenuTabs_init(void) {
	simple_mode = exists(SIMPLE_MODE_PATH);
	if (Shortcuts_getCount() > 0)
		Shortcuts_validate();
	// No tab lists recents any more, but this is what fills the Game
	// Switcher's list (and applies a pending disc change).
	Recents_load();
	MenuTabInputs in = {
		.show_consoles = CFG_getShowEmulators(),
		.has_consoles = Content_hasConsoles() != 0,
		.show_collections = CFG_getShowCollections(),
		.has_collections = hasCollections() != 0,
		.show_tools = CFG_getShowTools(),
		.has_tools = hasTools() != 0,
		.simple_mode = simple_mode,
		.has_settings = settingsPakExists(),
	};
	tab_count = MenuTabs_visible(&in, tabs);
}

int MenuTabs_count(void) {
	return tab_count;
}

MenuTabId MenuTabs_at(int index) {
	if (index < 0 || index >= tab_count)
		return MENU_TAB_HOME;
	return tabs[index];
}

MenuTabId MenuTabs_current(void) {
	return current;
}

void MenuTabs_setCurrent(MenuTabId id) {
	current = id;
}

bool MenuTabs_isVisible(MenuTabId id) {
	return MenuTabs_indexOf(tabs, tab_count, id) >= 0;
}

int MenuTabs_styleCategory(MenuTabId id) {
	switch (id) {
	case MENU_TAB_CONSOLES:
		return MENU_CAT_CONSOLES;
	case MENU_TAB_COLLECTIONS:
		return MENU_CAT_COLLECTIONS;
	case MENU_TAB_TOOLS:
		return MENU_CAT_TOOLS;
	default:
		return -1; // Home has no style setting
	}
}

const char* MenuTabs_path(MenuTabId id) {
	switch (id) {
	case MENU_TAB_CONSOLES:
		return ROMS_PATH;
	case MENU_TAB_COLLECTIONS:
		return COLLECTIONS_PATH;
	case MENU_TAB_TOOLS:
		return TOOLS_PATH;
	default:
		return SDCARD_PATH; // Home
	}
}

static MenuTabPaths tabPaths(void) {
	static char sys_tools[MAX_PATH];
	snprintf(sys_tools, sizeof(sys_tools), "%s/Tools", PAKS_PATH);
	return (MenuTabPaths){ROMS_PATH, COLLECTIONS_PATH, TOOLS_PATH, sys_tools};
}

MenuTabId MenuTabs_forPathVisible(const char* path) {
	MenuTabPaths p = tabPaths();
	MenuTabId id = MenuTabs_forPath(path, &p);
	return MenuTabs_isVisible(id) ? id : MENU_TAB_HOME;
}

// Build the Directory for a tab with a clamped selection window.
static Directory* buildRoot(MenuTabId id, int selected, int start, int end) {
	Directory* dir = Directory_new((char*)MenuTabs_path(id), 0);
	int count = dir->entries->count;
	int rc = GameList_rowCountAt(true);
	if (selected < 0 || selected >= count || end > count || start < 0 || start > selected ||
		(end && selected >= end)) {
		selected = 0;
		start = 0;
		end = 0;
	}
	dir->selected = selected;
	dir->start = start;
	dir->end = end ? end : ((count < rc) ? count : rc);
	return dir;
}

static void rememberCurrent(void) {
	if (!stack || stack->count == 0)
		return;
	Directory* root = stack->items[0];
	remembered[current] = (TabMemory){true, root->selected, root->start, root->end};
}

static unsigned generation = 0;

// Tab-row focus: a static, so a cold boot and a return from a game (nextui restarts) start without it.
static bool focused = false;

bool MenuTabs_focused(void) {
	return focused; // a pure read: whatever pushes a list clears the focus (see menutabs.h)
}

void MenuTabs_setFocused(bool on) {
	focused = on && stack && stack->count == 1;
}

float MenuTabs_contentLit(void) {
	return MenuTabs_focused() ? 0.4f : 1.0f;
}

unsigned MenuTabs_generation(void) {
	return generation;
}

void MenuTabs_openRoot(MenuTabId id) {
	generation++;
	focused = false; // boot, a launch return, a tab opened from Home: the content has focus
	current = id;
	Directory* dir = remembered[id].valid
						 ? buildRoot(id, remembered[id].selected, remembered[id].start, remembered[id].end)
						 : buildRoot(id, 0, 0, 0);
	DirectoryArray_free(stack);
	stack = Array_new();
	Array_push(stack, dir);
	top = dir;
}

bool MenuTabs_step(int delta) {
	if (tab_count < 2 || stack->count != 1)
		return false;
	rememberCurrent();
	int i = MenuTabs_indexOf(tabs, tab_count, current);
	bool keep = focused; // LEFT/RIGHT (and L1/R1) on the tab row stay on it
	MenuTabs_openRoot(tabs[MenuTabs_wrap(tab_count, i < 0 ? 0 : i, delta)]);
	focused = keep;
	return true;
}

void MenuTabs_reload(int keep_selected) {
	generation++;
	MenuTabs_init();
	MenuTabId next = tabs[MenuTabs_resolve(tabs, tab_count, current)];
	Directory* old = stack->items[0];
	int sel = (next == current) ? keep_selected : (remembered[next].valid ? remembered[next].selected : 0);
	current = next;
	Directory* fresh = buildRoot(next, 0, 0, 0);
	int n = fresh->entries->count;
	if (sel >= n)
		sel = n > 0 ? n - 1 : 0;
	fresh->selected = sel < 0 ? 0 : sel;
	int rc = GameList_rowCountAt(true);
	if (fresh->selected >= fresh->end && n > rc) { // same windowing as reloadDirectoryAt
		fresh->end = fresh->selected + 1;
		fresh->start = fresh->end - rc;
	}
	Directory_free(old);
	stack->items[0] = fresh;
	if (stack->count == 1)
		top = fresh;
	Home_reset(); // the pins or what Continue shows may have changed
}

// MENU_TAB_PATH: the tab key, plus a second line "launch" when the game was
// launched from Home itself (Continue or a pin), so boot returns to Home
// even when the ROM's own tab is another one.
static bool home_launch = false;

void MenuTabs_markHomeLaunch(void) {
	home_launch = true;
}

void MenuTabs_clearHomeLaunch(void) {
	home_launch = false;
}

void MenuTabs_saveState(void) {
	if (home_launch && current == MENU_TAB_HOME)
		putFile(MENU_TAB_PATH, "home\nlaunch\n");
	else if (stack && stack->count > 1 && !MenuTabs_isVisible(MENU_TAB_TOOLS) &&
			 exactMatch(((Directory*)stack->items[1])->path, TOOLS_PATH)) {
		// the context menu's Tools list pushed over a tab (Tools tab hidden): the tab it was pushed over plus a
		// "tools" line, so boot reopens that tab and loadLast re-pushes Tools on the tool's row
		char state[64];
		snprintf(state, sizeof(state), "%s\ntools\n", MenuTabs_key(current));
		putFile(MENU_TAB_PATH, state);
	} else
		putFile(MENU_TAB_PATH, (char*)MenuTabs_key(current));
	home_launch = false;
}

// MENU_TAB_PATH split into the tab key and its optional second line.
static void readSavedState(char* key, size_t key_size, char** second) {
	key[0] = '\0';
	*second = NULL;
	if (exists(MENU_TAB_PATH))
		getFile(MENU_TAB_PATH, key, key_size);
	char* nl = strchr(key, '\n');
	if (nl) {
		*nl++ = '\0';
		trimTrailingNewlines(nl);
		*second = nl;
	}
	trimTrailingNewlines(key);
}

bool MenuTabs_savedToolsPush(void) {
	char key[64];
	char* second;
	readSavedState(key, sizeof(key), &second);
	return second && strcmp(second, "tools") == 0;
}

bool MenuTabs_savedHomeLaunch(void) {
	char key[64];
	char* second;
	readSavedState(key, sizeof(key), &second);
	MenuTabId saved = MENU_TAB_HOME;
	return MenuTabs_parseKey(key, &saved) && saved == MENU_TAB_HOME && second && strcmp(second, "launch") == 0;
}

// A saved path belongs to the Home tab when it, or a folder it sits in
// below the card root, is pinned (a ROM launched from a pinned game folder).
static bool homeOwnsPath(const char* path) {
	size_t root_len = strlen(SDCARD_PATH);
	if (!prefixMatch(SDCARD_PATH, path))
		return false;
	char p[MAX_PATH];
	snprintf(p, sizeof(p), "%s", path);
	while (strlen(p) > root_len) {
		if (Shortcuts_exists(p + root_len))
			return true;
		char* slash = strrchr(p, '/');
		if (!slash)
			break;
		*slash = '\0';
	}
	return false;
}

// Whether the saved tab is where last_path was saved from. A launch from
// Search or the Game Switcher saves the ROM under whatever tab was showing;
// that tab doesn't list it, so boot should open the ROM's own tab instead.
static bool savedOwnsPath(MenuTabId saved, const char* last_path, MenuTabId path_tab) {
	switch (saved) {
	case MENU_TAB_HOME:
		return homeOwnsPath(last_path);
	default:
		return path_tab == saved;
	}
}

MenuTabId MenuTabs_initialTab(const char* last_path) {
	char key[64];
	char* second;
	readSavedState(key, sizeof(key), &second);
	MenuTabId saved = MENU_TAB_HOME;
	bool saved_valid = MenuTabs_parseKey(key, &saved);
	bool home_launched = saved_valid && saved == MENU_TAB_HOME && second && strcmp(second, "launch") == 0;
	bool has_path = last_path && last_path[0];
	MenuTabId path_tab = MENU_TAB_HOME;
	bool owns = false;
	if (has_path) {
		MenuTabPaths p = tabPaths();
		path_tab = MenuTabs_forPath(last_path, &p);
		// a tool from the Tools list pushed over the saved tab: boot reopens that tab (loadLast re-pushes Tools)
		bool tools_push = second && strcmp(second, "tools") == 0 && path_tab == MENU_TAB_TOOLS && !MenuTabs_isVisible(MENU_TAB_TOOLS);
		owns = home_launched || tools_push || (saved_valid && savedOwnsPath(saved, last_path, path_tab));
	}
	return MenuTabs_pickInitial(tabs, tab_count, saved_valid, saved, has_path, path_tab, owns);
}

// ---- Tab row ----
// Drawn in the menu-bar strip at the root: the labels, left-aligned on the
// title's edge, clipped to the width left of the status group. When they don't
// fit they scroll (current tab kept NX_DP(16) clear of the edges), with a 16 dp
// fade on each side that has more to scroll. While the row has focus the
// current label wears the selection plate (12 dp past the word each side, 5 dp
// above and below) and the underline hides; the words keep their positions.

#define TAB_LABEL_GAP NX_DP(20)	  // pixels between labels
#define TAB_UNDERLINE_H NX_DP(3)  // underline height in pixels
#define TAB_EDGE NX_DP(16)		  // scroll margin + edge fade width
#define TAB_PLATE_PAD_X NX_DP(12) // the plate past the word, each side
#define TAB_PLATE_PAD_Y NX_DP(5)  // and above and below
#define TAB_DIM_ALPHA 97		  // 38%: labels of the other tabs
#define TAB_GLIDE_MS 240		  // underline glide, eased with UI_easeStandard

// Underline glide (strip coordinates). Position = lerp(from, to, UI_easeStandard(elapsed / 240 ms)).
static struct {
	int from_x, from_w, to_x, to_w;
	Uint32 start_ms;
	bool active;
	bool first; // started on the last drawn frame: the next one rebases start_ms (underlineRebase)
} ul;
static int ul_tab = -1; // MenuTabId last targeted: only a tab change glides

// The label strip, cached so an idle frame only blits it.
static struct {
	SDL_Surface* surf;
	TTF_Font* font;
	int off, cur, count, w, h;
	MenuTabId ids[MENU_TAB_COUNT];
	int scale;
	bool focused; // the current label is left out (the plate draws it)
} strip;

// Label strip layout for one font: label x/width per visible tab (strip x 0
// = the first label's left edge); returns the strip's width.
static int layoutLabels(TTF_Font* f, int xs[MENU_TAB_COUNT], int ws[MENU_TAB_COUNT]) {
	int x = 0;
	for (int i = 0; i < tab_count; i++) {
		int w = 0;
		TTF_SizeUTF8(f, MenuTabs_label(tabs[i]), &w, NULL);
		xs[i] = x;
		ws[i] = w;
		x += w + (i + 1 < tab_count ? TAB_LABEL_GAP : 0);
	}
	return x;
}

// Multiply the alpha of columns [x0, x0 + n) by a linear ramp (rising: 0 -> 1 left to right, else 1 -> 0).
static void fadeColumns(SDL_Surface* s, int x0, int n, bool rising) {
	if (n <= 0 || SDL_LockSurface(s) != 0)
		return;
	for (int i = 0; i < n; i++) {
		int x = x0 + i;
		if (x < 0 || x >= s->w)
			continue;
		int k = rising ? i : n - 1 - i; // 0 at the outer edge
		Uint32 f = (Uint32)(k * 256 / n);
		for (int y = 0; y < s->h; y++) {
			Uint32* px = (Uint32*)((Uint8*)s->pixels + y * s->pitch + x * 4);
			Uint32 a = (*px >> 24) & 0xff;
			*px = (*px & 0x00ffffff) | (((a * f) >> 8) << 24);
		}
	}
	SDL_UnlockSurface(s);
}

static bool stripMatches(TTF_Font* f, int off, int cur, int w, int h) {
	if (!strip.surf || strip.font != f || strip.off != off || strip.cur != cur || strip.count != tab_count ||
		strip.w != w || strip.h != h || strip.scale != FIXED_SCALE || strip.focused != focused)
		return false;
	return memcmp(strip.ids, tabs, sizeof(MenuTabId) * tab_count) == 0;
}

// The labels as one ARGB surface (band_w x bar_h), edge-faded on a side with more to scroll.
static SDL_Surface* labelStrip(TTF_Font* f, const int* xs, int labels_w, int off, int cur, int band_w, int bar_h,
							   int band_h) {
	if (stripMatches(f, off, cur, band_w, bar_h))
		return strip.surf;
	if (strip.surf)
		SDL_FreeSurface(strip.surf);
	strip.surf = NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, band_w, bar_h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	// Transparent *white*: the labels alpha-blend onto it and are white masks, so anti-aliased edges keep their white
	// instead of darkening towards a 0x00000000 fill (a dark fringe once blended again).
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 255, 255, 255, 0));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	for (int i = 0; i < tab_count; i++) {
		SDL_Surface* text = GFX_getCachedText(f, MenuTabs_label(tabs[i]), COLOR_WHITE);
		if (!text)
			continue;
		SDL_Rect dst = {xs[i] - off, (band_h - text->h) / 2};
		if (i == cur && focused)
			continue; // the plate draws it
		if (i == cur) {
			SDL_BlitSurface(text, NULL, s, &dst);
		} else {
			// The text cache is shared: dim for this blit only.
			SDL_SetSurfaceAlphaMod(text, TAB_DIM_ALPHA);
			SDL_BlitSurface(text, NULL, s, &dst);
			SDL_SetSurfaceAlphaMod(text, 255);
		}
	}
	int edge = TAB_EDGE < band_w / 2 ? TAB_EDGE : band_w / 2;
	if (off > 0)
		fadeColumns(s, 0, edge, true);
	if (off < labels_w - band_w)
		fadeColumns(s, band_w - edge, edge, false);

	strip.surf = s;
	strip.font = f;
	strip.off = off;
	strip.cur = cur;
	strip.count = tab_count;
	strip.w = band_w;
	strip.h = bar_h;
	strip.scale = FIXED_SCALE;
	strip.focused = focused;
	memcpy(strip.ids, tabs, sizeof(MenuTabId) * tab_count);
	return s;
}

// Underline progress 0..1 (1 once the glide's time is up, or when idle).
static float underlineProgress(void) {
	if (!ul.active)
		return 1.0f;
	Uint32 elapsed = SDL_GetTicks() - ul.start_ms;
	if (elapsed >= TAB_GLIDE_MS)
		return 1.0f;
	return UI_easeStandard((float)elapsed / (float)TAB_GLIDE_MS);
}

static void underlineNow(int* x, int* w) {
	float p = underlineProgress();
	*x = ul.from_x + (int)((ul.to_x - ul.from_x) * p + (ul.to_x >= ul.from_x ? 0.5f : -0.5f));
	*w = ul.from_w + (int)((ul.to_w - ul.from_w) * p + (ul.to_w >= ul.from_w ? 0.5f : -0.5f));
}

static void underlineSnap(int x, int w) {
	ul.from_x = ul.to_x = x;
	ul.from_w = ul.to_w = w;
	ul.active = false;
	ul.first = false;
}

// The glide's second frame. Its first one also built the new tab's body, which can take long when the tab has
// another layout (a Carousel or Grid builds its items on its first frame): count from one frame before now, so the
// underline moves on screen from where it started instead of jumping to its end (menu_transition.h).
static void underlineRebase(void) {
	if (!ul.active || !ul.first)
		return;
	ul.first = false;
	ul.start_ms = MenuTransition_rebaseStart(ul.start_ms, SDL_GetTicks(), MENU_TRANSITION_FRAME_MS);
}

// The accent's RGB, opaque (only the List pill wears Color 1's opacity).
static Uint32 accentOpaque(const SDL_PixelFormat* fmt) {
	SDL_Color ac = UI_accent();
	return SDL_MapRGB(fmt, ac.r, ac.g, ac.b);
}

// The selection plate around the current label (pw x ph px): an accent pill (radius = half its height, anti-aliased by
// coverage) with the label in the accent's ink, composed once and cached per label, size, font and accent.
static struct {
	SDL_Surface* surf;
	TTF_Font* font;
	MenuTabId id;
	int w, h, scale;
	uint64_t key; // the accent and its ink
} plate;

static SDL_Surface* plateSurface(TTF_Font* f, MenuTabId id, int pw, int ph, int pad_y) {
	SDL_Color ac = UI_accent(), ink = UI_onAccent(); // opaque here: only the List pill wears Color 1's opacity
	uint32_t accent = ((uint32_t)ac.r << 16) | ((uint32_t)ac.g << 8) | ac.b;
	// GFX_getCachedText keys its cache on the colour including alpha, so the ink is forced to a=255: one cached label
	// whatever Color 5's opacity, drawn opaque like the plate.
	ink.a = 255;
	uint64_t key = ((uint64_t)accent << 24) | ((uint32_t)ink.r << 16) | ((uint32_t)ink.g << 8) | ink.b;
	if (plate.surf && plate.font == f && plate.id == id && plate.w == pw && plate.h == ph && plate.scale == FIXED_SCALE &&
		plate.key == key)
		return plate.surf;
	if (plate.surf)
		SDL_FreeSurface(plate.surf);
	plate.surf = NULL;
	if (pw <= 0 || ph <= 0)
		return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	if (SDL_LockSurface(s) == 0) {
		float r = ph / 2.0f, x0 = r, x1 = pw - r;
		for (int y = 0; y < ph; y++) {
			Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
			float cy = y + 0.5f - r;
			for (int x = 0; x < pw; x++) {
				float cx = x + 0.5f;
				float dx = cx < x0 ? x0 - cx : (cx > x1 ? cx - x1 : 0.0f);
				float cover = r - sqrtf(dx * dx + cy * cy) + 0.5f;
				Uint32 a = cover >= 1.0f ? 255 : (cover <= 0.0f ? 0 : (Uint32)(cover * 255.0f + 0.5f));
				row[x] = (a << 24) | accent; // the accent: the label blends onto it
			}
		}
		SDL_UnlockSurface(s);
	}
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	SDL_Surface* text = GFX_getCachedText(f, MenuTabs_label(id), ink);
	if (text)
		SDL_BlitSurface(text, NULL, s, &(SDL_Rect){TAB_PLATE_PAD_X, pad_y});
	plate.surf = s;
	plate.font = f;
	plate.id = id;
	plate.w = pw;
	plate.h = ph;
	plate.scale = FIXED_SCALE;
	plate.key = key;
	return s;
}

void MenuTabs_renderRow(SDL_Surface* screen, int ow) {
	if (tab_count <= 0)
		return;

	int bar_h = SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2);
	int band_h = bar_h - TAB_UNDERLINE_H * 2; // labels centre above the underline
	int left = NX_DP(NX_MENU_GUTTER_DP);	  // the first label on the 24 dp gutter (§5), where the List rows' text starts
	int right_limit = screen->w - ow - SCALE1(PADDING);
	int band_w = right_limit - left;
	if (band_w <= 0)
		return;
	bool tf = MenuTabs_focused();

	int xs[MENU_TAB_COUNT], ws[MENU_TAB_COUNT];
	TTF_Font* f = font.medium; // 16 sp; labels that don't fit scroll (never a smaller font)
	int labels_w = layoutLabels(f, xs, ws);

	int cur = MenuTabs_indexOf(tabs, tab_count, current);
	if (cur < 0)
		cur = 0;
	int cur_x = xs[cur], cur_w = ws[cur];

	// The labels scroll inside the band; the plate's 12 dp fit in the 16 dp the current label keeps from its edges.
	int band_x = left;
	int off = MenuTabs_scrollOffset(labels_w, band_w, cur_x, cur_w, TAB_EDGE);
	int base = band_x - off; // screen x of label-strip x 0

	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &(SDL_Rect){band_x, 0, band_w, bar_h});

	SDL_Surface* labels = labelStrip(f, xs, labels_w, off, cur, band_w, bar_h, band_h);
	if (labels)
		SDL_BlitSurface(labels, NULL, screen, &(SDL_Rect){band_x, 0});

	// Underline: a tab change glides (x + width) from where it is drawn now; the
	// same tab moving (a relabel or rescale) snaps, and so does a change on the
	// focused row (the plate jumps, the hidden underline follows). The glide is
	// the row's own: whatever layout the old and new tabs' bodies use, it runs
	// on (underlineRebase keeps a slow first body frame from eating it).
	underlineRebase();
	if (ul_tab != (int)current || cur_x != ul.to_x) {
		bool tab_changed = ul_tab >= 0 && ul_tab != (int)current;
		if (tab_changed && CFG_getMenuAnimations() && !tf) {
			int x, w;
			underlineNow(&x, &w);
			ul.from_x = x;
			ul.from_w = w;
			ul.to_x = cur_x;
			ul.to_w = cur_w;
			ul.start_ms = SDL_GetTicks();
			ul.active = true;
			ul.first = true;
		} else {
			underlineSnap(cur_x, cur_w);
		}
	}
	ul_tab = (int)current;
	if (!ul.active || tf) // width changes outside a glide (relabel, rescale) snap; the focused row never glides
		underlineSnap(cur_x, cur_w);
	if (tf) {
		// The plate: on the label, which the strip left out. Its clip widens by the plate's padding (the first
		// label sits on the band's left edge when the row doesn't scroll), but never into the status group.
		SDL_Surface* text = GFX_getCachedText(f, MenuTabs_label(current), COLOR_WHITE);
		int th = text ? text->h : TTF_FontHeight(f);
		int ty = (band_h - th) / 2;
		// 5 dp above and below, less where the bar has no room (the label box nearly fills the 28-unit strip at
		// 3x): the plate stays whole and centred on the word instead of being cut by the screen's top edge
		int pad_y = TAB_PLATE_PAD_Y;
		if (pad_y > ty)
			pad_y = ty;
		if (pad_y > bar_h - ty - th)
			pad_y = bar_h - ty - th;
		if (pad_y < 0)
			pad_y = 0;
		int px = base + cur_x - TAB_PLATE_PAD_X, py = ty - pad_y;
		SDL_Surface* p = plateSurface(f, current, cur_w + 2 * TAB_PLATE_PAD_X, th + 2 * pad_y, pad_y);
		int cl = band_x - TAB_PLATE_PAD_X, cr = right_limit + TAB_PLATE_PAD_X;
		if (cl < 0)
			cl = 0;
		if (cr > screen->w - ow)
			cr = screen->w - ow;
		if (p) {
			SDL_SetClipRect(screen, &(SDL_Rect){cl, 0, cr - cl, bar_h});
			SDL_BlitSurface(p, NULL, screen, &(SDL_Rect){px, py});
		}
	} else {
		int ux, uw;
		underlineNow(&ux, &uw);
		SDL_FillRect(screen, &(SDL_Rect){base + ux, bar_h - TAB_UNDERLINE_H - SCALE1(2), uw, TAB_UNDERLINE_H},
					 accentOpaque(screen->format));
	}

	SDL_SetClipRect(screen, &prev_clip);
}

// Only the root draws the row, so leaving it mid-glide snaps the underline;
// otherwise the keep-alive in nextui.c would redraw every frame inside a game
// list. At the root, as UI_listGlideActive does: a glide whose time is up
// finalizes and clears `active`, but this call still reports true so the host
// draws exactly one settled frame; the next call sees `active` false and
// returns false. No latch: the state is `active` alone, which both paths clear.
bool MenuTabs_animating(void) {
	if (!ul.active)
		return false;
	if (!stack || stack->count != 1) {
		underlineSnap(ul.to_x, ul.to_w);
		return false;
	}
	underlineRebase(); // before the time-up check: a slow first frame must not end the glide
	if (SDL_GetTicks() - ul.start_ms >= TAB_GLIDE_MS)
		underlineSnap(ul.to_x, ul.to_w);
	return true;
}

bool MenuTabs_underlineGliding(void) {
	return stack && stack->count == 1 && ul.active;
}
