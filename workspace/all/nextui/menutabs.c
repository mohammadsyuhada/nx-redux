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
#include "list_window.h"
#include "menu_transition.h"
#include "recents.h"
#include "shortcuts.h"
#include "ui_accent.h"
#include "ui_ease.h"
#include "ui_list.h"
#include "ui_menubar.h"

static MenuTabId tabs[MENU_TAB_COUNT];
static int tab_count = 0;
static MenuTabId current = MENU_TAB_HOME;
static bool simple_mode = false;

// Parked roots (T1-1, menutabs_model.h): L1/R1 parks the outgoing tab's stack[0] here and takes the incoming
// one's, so a tab switch costs no card I/O. Only tabs that are NOT current have a root here (the stack owns
// stack[0]), so every existing free of stack[0] stays correct. A slot's hint keeps its selection after the root
// is dropped. Dropped by MenuTabs_reload (the tabs in its plan's stale_tabs), MenuTabs_setCurrent and MenuTabs_dropCached (one).
static MenuTabSlot slots[MENU_TAB_COUNT];
// Content_libraryGen() when stack[0] was built, and when the parked Consoles root was: a Consoles root built
// against an older emulist (a refresh, a delete, a Search rescan) is rebuilt instead of reused.
static unsigned root_gen = 0;
static unsigned consoles_gen = 0;

static void dropSlot(MenuTabId id) {
	Directory* root = MenuTabs_slotDrop(slots, id);
	if (root)
		Directory_free(root);
}

static void dropAllSlots(void) {
	for (int id = 0; id < MENU_TAB_COUNT; id++)
		dropSlot((MenuTabId)id);
}

static bool settingsPakExists(void) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/Settings.pak", TOOLS_PATH);
	if (exists(path))
		return true;
	snprintf(path, sizeof(path), "%s/Tools/Settings.pak", PAKS_PATH);
	return exists(path);
}

// What the visible tabs are computed from, kept between reloads (T2-11): a reload re-reads only the inputs its
// mask says may have changed (MenuTabs_reloadPlan), and recomputes the tabs from the rest as last read.
static MenuTabInputs last_in;

// Re-read what `plan` asks for, then recompute the visible tabs. The CFG flags, simple mode, Tools and the
// Settings pak change only outside nextui (Settings, a card edit), so only the full pass (boot, MENU_RELOAD_ALL)
// reads them.
static void refreshInputs(const MenuReloadPlan* plan) {
	if (plan->check_all)
		simple_mode = exists(SIMPLE_MODE_PATH);
	if (plan->validate_pins && Shortcuts_getCount() > 0)
		Shortcuts_validate();
	// No tab lists recents any more, but this is what fills the Game
	// Switcher's list (and applies a pending disc change).
	if (plan->load_recents)
		Recents_load();
	if (plan->check_all) {
		last_in.hide_home = !CFG_getShowHome();
		last_in.show_consoles = CFG_getShowEmulators();
		last_in.show_collections = CFG_getShowCollections();
		last_in.show_tools = CFG_getShowTools();
		last_in.has_tools = hasTools() != 0;
		last_in.simple_mode = simple_mode;
		last_in.has_settings = settingsPakExists();
	}
	if (plan->check_consoles)
		last_in.has_consoles = Content_hasConsoles() != 0;
	if (plan->check_collections)
		last_in.has_collections = hasCollections() != 0;
	tab_count = MenuTabs_visible(&last_in, tabs);
}

void MenuTabs_init(void) {
	MenuReloadPlan full = MenuTabs_reloadPlan(MENU_RELOAD_ALL);
	refreshInputs(&full);
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
	// pathToStack just built a fresh stack[0] for id: a parked copy would be a second root for one tab
	dropSlot(id);
	current = id;
	root_gen = Content_libraryGen();
}

void MenuTabs_dropCached(MenuTabId id) {
	if (id != current) // the current tab's root is the stack's: its caller reloads it if it must
		dropSlot(id);
}

void MenuTabs_quit(void) {
	dropAllSlots();
}

bool MenuTabs_isVisible(MenuTabId id) {
	return MenuTabs_indexOf(tabs, tab_count, id) >= 0;
}

bool MenuTabs_pushable(MenuTabId id) {
	if (simple_mode || MenuTabs_isVisible(id))
		return false;
	switch (id) {
	case MENU_TAB_CONSOLES:
		return last_in.has_consoles;
	case MENU_TAB_COLLECTIONS:
		return last_in.has_collections;
	case MENU_TAB_TOOLS:
		return last_in.has_tools;
	default:
		return false;
	}
}

// The hidden tab whose list sits pushed at stack[1] (the root context menu's item), else MENU_TAB_HOME.
static MenuTabId pushedTab(void) {
	if (!stack || stack->count < 2)
		return MENU_TAB_HOME;
	const char* path = ((Directory*)stack->items[1])->path;
	for (int id = MENU_TAB_CONSOLES; id < MENU_TAB_COUNT; id++)
		if (!MenuTabs_isVisible((MenuTabId)id) && exactMatch((char*)path, (char*)MenuTabs_path((MenuTabId)id)))
			return (MenuTabId)id;
	return MENU_TAB_HOME;
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
	return MenuTabs_isVisible(id) ? id : MenuTabs_at(0); // Home, unless it is hidden
}

// Build the Directory for a tab with a clamped selection window. `current` must already be id: the row count
// depends on the tab's style.
static Directory* buildRoot(MenuTabId id, int selected, int start, int end) {
	Directory* dir = Directory_new((char*)MenuTabs_path(id), 0);
	root_gen = Content_libraryGen(); // after Directory_new: listing Consoles may have refilled the emulist
	MenuTabs_clampWindow(dir->entries->count, GameList_rowCountAt(true), &selected, &start, &end);
	dir->selected = selected;
	dir->start = start;
	dir->end = end;
	return dir;
}

static unsigned generation = 0;

// Tab-row focus: a static, so a cold boot and a return from a game (nextui restarts) start without it.
static bool focused = false;

bool MenuTabs_focused(void) {
	return focused; // a pure read: whatever pushes a list clears the focus (see menutabs.h)
}

// The tab-focus dim, aimed eagerly wherever the focus changes: a d-pad move, A or B on or off the row tweens it (180 ms),
// any other way out (SELECT, START, MENU, the F keys, a launch or a list opened, a context menu) snaps it lit, so a
// screen that slides in or a menu that opens over the content never shows it part-dimmed.
static MenuTransitionDim dim;

static void aimDim(bool animate) {
	bool root = stack && stack->count == 1;
	MenuTransition_dimAim(&dim, focused && root, SDL_GetTicks(), animate && root && CFG_getMenuAnimations());
}

void MenuTabs_setFocused(bool on) {
	// a lone tab draws no row (MenuTabs_renderRow), nor does a hidden page title (Layouts > Page title), so there is
	// nothing to focus: UP from the content's top stops
	focused = on && stack && stack->count == 1 && tab_count > 1 && CFG_getPageTitle();
	aimDim(true);
}

void MenuTabs_leaveFocus(void) {
	focused = false;
	aimDim(false);
}

float MenuTabs_contentAlpha(void) {
	if (!stack || stack->count != 1)
		aimDim(false); // off the main menu the content is lit, whatever path got there
	return MenuTransition_dimAlpha(&dim, SDL_GetTicks());
}

bool MenuTabs_dimAnimating(void) {
	return MenuTransition_dimStep(&dim, SDL_GetTicks());
}

unsigned MenuTabs_generation(void) {
	return generation;
}

// stack[0] for `id`, replacing the whole stack; the focus and the dim are the caller's. The outgoing root is
// parked (when it is the current tab's plain root) and id's parked root reused, so a switch reads nothing from
// the card; generation still moves, so per-view state resets as for a fresh root.
static void openRootKeepFocus(MenuTabId id) {
	generation++;
	if (!stack)
		stack = Array_new();
	while (stack->count > 1)
		DirectoryArray_pop(stack); // lists pushed over the old root belong to it
	Directory* old = stack->count ? Array_pop(stack) : NULL;
	if (old && exactMatch(old->path, MenuTabs_path(current))) {
		// not a PLATFORM-merged root (pathToStack): those are rebuilt, never parked
		Directory* displaced = MenuTabs_slotPark(slots, current, old, old->selected, old->start, old->end);
		if (displaced)
			Directory_free(displaced);
		if (current == MENU_TAB_CONSOLES)
			consoles_gen = root_gen;
	} else if (old)
		Directory_free(old);

	current = id; // before anything reads the row count
	Directory* dir = MenuTabs_slotTake(slots, id);
	if (dir && id == MENU_TAB_CONSOLES && consoles_gen != Content_libraryGen()) {
		Directory_free(dir);
		dir = NULL;
	}
	if (dir) {
		// the rows depend on the layout and scale, which may have changed while it was parked
		MenuTabs_clampWindow(dir->entries->count, GameList_rowCountAt(true), &dir->selected, &dir->start, &dir->end);
		if (id == MENU_TAB_CONSOLES)
			root_gen = consoles_gen;
	} else {
		const MenuTabSlot* hint = &slots[id];
		dir = hint->hint ? buildRoot(id, hint->selected, hint->start, hint->end) : buildRoot(id, 0, 0, 0);
	}
	Array_push(stack, dir);
	top = dir;
}

void MenuTabs_openRoot(MenuTabId id) {
	openRootKeepFocus(id);
	MenuTabs_leaveFocus(); // boot, a launch return, a tab opened from Home: the content has focus, lit at once
}

bool MenuTabs_step(int delta) {
	if (tab_count < 2 || stack->count != 1)
		return false;
	int i = MenuTabs_indexOf(tabs, tab_count, current);
	// LEFT/RIGHT (and L1/R1) on the tab row stay on it, and the dim stays as it is
	openRootKeepFocus(tabs[MenuTabs_wrap(tab_count, i < 0 ? 0 : i, delta)]);
	return true;
}

void MenuTabs_reload(int keep_selected, unsigned what) {
	MenuReloadPlan plan = MenuTabs_reloadPlan(what);
	generation++; // per-view state resets as for a fresh root, whether or not stack[0] is rebuilt
	// only a tab the change can show in has an out-of-date parked root; it is rebuilt on its next visit (from its
	// hint). The others stay parked: they equal a rebuild (and a Consoles root parked before an emulist refill is
	// still caught by openRootKeepFocus's consoles_gen check).
	for (int id = 0; id < MENU_TAB_COUNT; id++) {
		if (plan.stale_tabs & (1u << id))
			dropSlot((MenuTabId)id);
	}
	refreshInputs(&plan);
	MenuTabId next = tabs[MenuTabs_resolve(tabs, tab_count, current)];
	MenuTabId old_tab = current;
	Directory* old = stack->items[0];
	// the current tab's root, when the change can't show in it, is what a rebuild would give: kept, but
	// re-windowed exactly as a rebuild is (ListWindow_reload ignores the old window)
	bool keep_root = next == old_tab && !(plan.stale_tabs & (1u << old_tab)) &&
					 exactMatch(old->path, MenuTabs_path(old_tab)) &&
					 (old_tab != MENU_TAB_CONSOLES || root_gen == Content_libraryGen());
	if (keep_root) {
		old->selected =
			ListWindow_reload(old->entries->count, GameList_rowCountAt(true), keep_selected, &old->start, &old->end);
	} else {
		int sel = (next == old_tab) ? keep_selected : (slots[next].hint ? slots[next].selected : 0);
		dropSlot(next); // a parked copy would be a second root for this tab (its hint stays)
		current = next; // before anything reads the row count (it depends on the tab's style)
		Directory* fresh = buildRoot(next, 0, 0, 0);
		// buildRoot's window from the top, re-windowed so the kept selection shows (as reloadDirectoryAt)
		fresh->selected =
			ListWindow_reload(fresh->entries->count, GameList_rowCountAt(true), sel, &fresh->start, &fresh->end);
		if (next != old_tab) {
			// the current tab vanished (e.g. its last ROM was deleted from inside Consoles › GBA): a list pushed
			// over it belongs to that tab, so it can't stay over another tab's root -- B would land on the wrong
			// tab, and the caller's reloadDirectoryAt would rebuild it there. Back to the new root.
			while (stack->count > 1)
				DirectoryArray_pop(stack);
		}
		Directory_free(old);
		stack->items[0] = fresh;
		if (stack->count == 1)
			top = fresh;
		if (next != old_tab)
			MenuTabs_leaveFocus(); // a different tab's content: lit, with focus
	}
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
	else if (pushedTab() != MENU_TAB_HOME) {
		// a hidden tab's list pushed over a tab by the context menu: the tab it was pushed over plus the pushed
		// tab's key, so boot reopens that tab and loadLast re-pushes the list
		char state[64];
		snprintf(state, sizeof(state), "%s\n%s\n", MenuTabs_key(current), MenuTabs_key(pushedTab()));
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

// The pushed tab a second line names (Consoles, Collections or Tools), else MENU_TAB_HOME.
static MenuTabId pushedFromLine(const char* second) {
	MenuTabId id = MENU_TAB_HOME;
	if (!second || !MenuTabs_parseKey(second, &id))
		return MENU_TAB_HOME;
	return id;
}

MenuTabId MenuTabs_savedPush(void) {
	char key[64];
	char* second;
	readSavedState(key, sizeof(key), &second);
	return pushedFromLine(second);
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
		// a launch from a hidden tab's list pushed over the saved tab: boot reopens that tab (loadLast re-pushes the list)
		MenuTabId pushed = pushedFromLine(second);
		bool list_push = pushed != MENU_TAB_HOME && path_tab == pushed && !MenuTabs_isVisible(pushed);
		owns = home_launched || list_push || (saved_valid && savedOwnsPath(saved, last_path, path_tab));
	}
	return MenuTabs_pickInitial(tabs, tab_count, saved_valid, saved, has_path, path_tab, owns);
}

// ---- Tab row ----
// Drawn in the menu-bar strip at the root: the labels, left-aligned on the
// title's edge, clipped to the width left of the status group. When they don't
// fit they scroll (current tab kept 16 dp clear of the edges), with a 16 dp
// fade on each side that has more to scroll. While the row has focus the
// current label wears the selection plate (12 dp past the word each side, 5 dp
// above and below) and the underline hides; the words keep their positions.

// The tab row keeps the page title's size whatever the UI scale (UI scale enlarges the content, not the tabs): its
// font is the page title's (UI_pageTitleFont) and its measures dp at CHROME_SCALE, the Brick's physical size on every panel
#define TAB_LABEL_GAP NX_CHROME_DP(20)	 // pixels between labels
#define TAB_UNDERLINE_H NX_CHROME_DP(3)	 // underline height in pixels
#define TAB_EDGE NX_CHROME_DP(16)		 // scroll margin + edge fade width
#define TAB_PLATE_PAD_X NX_CHROME_DP(12) // the plate past the word, each side
#define TAB_PLATE_PAD_Y NX_CHROME_DP(5)	 // and above and below
#define TAB_DIM_ALPHA 97				 // 38%: labels of the other tabs
#define TAB_GLIDE_MS 240				 // underline glide, eased with UI_easeStandard

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

// The last layoutLabels result, so a dirty frame doesn't re-measure the labels: keyed on the font, the visible tabs
// (each id's label is a fixed string) and FIXED_SCALE (a system-font reload follows a scale change).
static struct {
	TTF_Font* font;
	int count, scale, width;
	MenuTabId ids[MENU_TAB_COUNT];
	int xs[MENU_TAB_COUNT], ws[MENU_TAB_COUNT];
} label_layout;

// Label strip layout for one font: label x/width per visible tab (strip x 0
// = the first label's left edge); returns the strip's width.
static int layoutLabels(TTF_Font* f, int xs[MENU_TAB_COUNT], int ws[MENU_TAB_COUNT]) {
	if (label_layout.font != f || label_layout.count != tab_count || label_layout.scale != FIXED_SCALE ||
		memcmp(label_layout.ids, tabs, sizeof(MenuTabId) * tab_count) != 0) {
		int x = 0;
		for (int i = 0; i < tab_count; i++) {
			int w = 0;
			TTF_SizeUTF8(f, MenuTabs_label(tabs[i]), &w, NULL);
			label_layout.xs[i] = x;
			label_layout.ws[i] = w;
			x += w + (i + 1 < tab_count ? TAB_LABEL_GAP : 0);
		}
		label_layout.font = f;
		label_layout.count = tab_count;
		label_layout.scale = FIXED_SCALE;
		label_layout.width = x;
		memcpy(label_layout.ids, tabs, sizeof(MenuTabId) * tab_count);
	}
	memcpy(xs, label_layout.xs, sizeof(int) * tab_count);
	memcpy(ws, label_layout.ws, sizeof(int) * tab_count);
	return label_layout.width;
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

// A glyph's height above the baseline (px) in f: its maxy, or `fallback` when the font can't tell.
static int glyphTop(TTF_Font* f, Uint16 ch, int fallback) {
	int minx, maxx, miny, maxy, adv;
	return f && TTF_GlyphMetrics(f, ch, &minx, &maxx, &miny, &maxy, &adv) == 0 && maxy > 0 ? maxy : fallback;
}

// The selection plate around the current label (pw x ph px): an accent pill (radius = half its height, anti-aliased by
// coverage) with the label in the accent's ink, composed once and cached per label, size, font and accent.
static struct {
	SDL_Surface* surf;
	TTF_Font* font;
	MenuTabId id;
	int w, h, scale, text_y;
	uint64_t key; // the accent and its ink
} plate;

static SDL_Surface* plateSurface(TTF_Font* f, MenuTabId id, int pw, int ph, int text_y) {
	SDL_Color ac = UI_accent(), ink = UI_onAccent(); // opaque here: only the List pill wears Color 1's opacity
	uint32_t accent = ((uint32_t)ac.r << 16) | ((uint32_t)ac.g << 8) | ac.b;
	// GFX_getCachedText keys its cache on the colour including alpha, so the ink is forced to a=255: one cached label
	// whatever Color 5's opacity, drawn opaque like the plate.
	ink.a = 255;
	uint64_t key = ((uint64_t)accent << 24) | ((uint32_t)ink.r << 16) | ((uint32_t)ink.g << 8) | ink.b;
	if (plate.surf && plate.font == f && plate.id == id && plate.w == pw && plate.h == ph && plate.scale == FIXED_SCALE &&
		plate.text_y == text_y && plate.key == key)
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
		SDL_BlitSurface(text, NULL, s, &(SDL_Rect){TAB_PLATE_PAD_X, text_y}); // its line box may overhang: clipped
	plate.surf = s;
	plate.font = f;
	plate.id = id;
	plate.w = pw;
	plate.h = ph;
	plate.text_y = text_y;
	plate.scale = FIXED_SCALE;
	plate.key = key;
	return s;
}

void MenuTabs_renderRow(SDL_Surface* screen, int ow) {
	// one tab left (the others hidden or empty): no row, a lone label has nothing to switch to
	if (tab_count <= 1)
		return;

	// the row fills the top bar: both the default scale's height whatever the UI scale
	int bar_h = BAR_HEIGHT;
	int row_h = bar_h;
	int row_y = 0;
	int band_h = row_h - TAB_UNDERLINE_H * 2;	// labels centre above the underline
	int left = NX_NATIVE_DP(NX_MENU_GUTTER_DP); // the first label on the 24 dp gutter (§5), where the List rows' text starts
	int right_limit = screen->w - ow - SCALE1(PADDING);
	int band_w = right_limit - left;
	if (band_w <= 0)
		return;
	bool tf = MenuTabs_focused();

	int xs[MENU_TAB_COUNT], ws[MENU_TAB_COUNT];
	// the page title's font; labels that don't fit scroll (never a smaller font)
	TTF_Font* f = UI_pageTitleFont();
	if (!f)
		f = font.medium;
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

	SDL_Surface* labels = labelStrip(f, xs, labels_w, off, cur, band_w, row_h, band_h);
	if (labels)
		SDL_BlitSurface(labels, NULL, screen, &(SDL_Rect){band_x, row_y});

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
		// Sized and centred on the word as seen, not on its line box (which carries the descender room and nearly
		// fills the strip): the capital height plus 5 dp each side, centred halfway between the cap and the
		// lowercase midlines, where a mostly lowercase label's weight sits. Clamped inside the bar.
		int base_y = ty + TTF_FontAscent(f);
		int cap_h = glyphTop(f, 'H', TTF_FontAscent(f) * 7 / 10), x_h = glyphTop(f, 'x', cap_h * 7 / 10);
		int centre = base_y - (cap_h + x_h) / 4;
		int ph = cap_h + 2 * TAB_PLATE_PAD_Y;
		int py = centre - ph / 2;
		if (py < 0)
			py = 0;
		if (py + ph > row_h)
			py = row_h - ph;
		int px = base + cur_x - TAB_PLATE_PAD_X;
		SDL_Surface* p = plateSurface(f, current, cur_w + 2 * TAB_PLATE_PAD_X, ph, ty - py);
		int cl = band_x - TAB_PLATE_PAD_X, cr = right_limit + TAB_PLATE_PAD_X;
		if (cl < 0)
			cl = 0;
		if (cr > screen->w - ow)
			cr = screen->w - ow;
		if (p) {
			SDL_SetClipRect(screen, &(SDL_Rect){cl, 0, cr - cl, bar_h});
			SDL_BlitSurface(p, NULL, screen, &(SDL_Rect){px, row_y + py});
		}
	} else {
		int ux, uw;
		underlineNow(&ux, &uw);
		SDL_FillRect(screen, &(SDL_Rect){base + ux, row_y + row_h - TAB_UNDERLINE_H - CHROME1(2), uw, TAB_UNDERLINE_H},
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
