#include "../../nextui/menutabs_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const MenuTabPaths P = {"/mnt/SDCARD/Roms", "/mnt/SDCARD/Collections", "/mnt/SDCARD/Tools",
							   "/mnt/SDCARD/.system/paks/Tools"};

static MenuTabInputs all_on(void) {
	MenuTabInputs in = {.show_consoles = true, .has_consoles = true, .show_collections = true, .has_collections = true, .show_tools = true, .has_tools = true};
	return in;
}

static void visible_fixed_order_and_toggles(void) {
	MenuTabId t[MENU_TAB_COUNT];
	MenuTabInputs in = all_on();
	assert(MenuTabs_visible(&in, t) == 4);
	assert(t[0] == MENU_TAB_HOME && t[1] == MENU_TAB_CONSOLES && t[2] == MENU_TAB_COLLECTIONS &&
		   t[3] == MENU_TAB_TOOLS);
	in.show_consoles = false;	// hidden by setting
	in.has_collections = false; // empty
	assert(MenuTabs_visible(&in, t) == 2);
	assert(t[0] == MENU_TAB_HOME && t[1] == MENU_TAB_TOOLS);
}

static void home_always_first_and_visible(void) {
	MenuTabInputs in = {0}; // nothing shown, no content
	MenuTabId out[MENU_TAB_COUNT];
	int n = MenuTabs_visible(&in, out);
	assert(n == 1 && out[0] == MENU_TAB_HOME);
	in = (MenuTabInputs){.show_consoles = true, .has_consoles = true, .show_collections = true, .has_collections = true, .show_tools = true, .has_tools = true};
	n = MenuTabs_visible(&in, out);
	assert(n == 4 && out[0] == MENU_TAB_HOME && out[1] == MENU_TAB_CONSOLES && out[2] == MENU_TAB_COLLECTIONS &&
		   out[3] == MENU_TAB_TOOLS);
}

static void visible_simple_mode_tools_is_settings(void) {
	MenuTabId t[MENU_TAB_COUNT];
	MenuTabInputs in = all_on();
	in.simple_mode = true;
	in.show_tools = false; // ignored in simple mode
	in.has_tools = false;
	in.has_settings = true;
	int n = MenuTabs_visible(&in, t);
	assert(t[n - 1] == MENU_TAB_TOOLS);
	in.has_settings = false;
	n = MenuTabs_visible(&in, t);
	assert(t[n - 1] != MENU_TAB_TOOLS);
}

static void wrap_steps_both_ways(void) {
	assert(MenuTabs_wrap(4, 0, -1) == 3);
	assert(MenuTabs_wrap(4, 3, 1) == 0);
	assert(MenuTabs_wrap(4, 1, 1) == 2);
	assert(MenuTabs_wrap(0, 0, 1) == 0);
}

static void resolve_vanished_tab_picks_next_then_last(void) {
	MenuTabId now[] = {MENU_TAB_HOME, MENU_TAB_CONSOLES, MENU_TAB_TOOLS};
	assert(MenuTabs_resolve(now, 3, MENU_TAB_TOOLS) == 2);		 // still there
	assert(MenuTabs_resolve(now, 3, MENU_TAB_COLLECTIONS) == 2); // gone: next in order is Tools
	MenuTabId only[] = {MENU_TAB_HOME, MENU_TAB_CONSOLES};
	assert(MenuTabs_resolve(only, 2, MENU_TAB_TOOLS) == 1); // gone, nothing later: last
	assert(MenuTabs_indexOf(only, 2, MENU_TAB_COLLECTIONS) == -1);
}

static void home_survives_no_pins(void) {
	MenuTabId tabs[] = {MENU_TAB_HOME, MENU_TAB_CONSOLES};
	assert(MenuTabs_resolve(tabs, 2, MENU_TAB_HOME) == 0);
	// Collections vanished while current: the next later tab, else the last
	assert(MenuTabs_resolve(tabs, 2, MENU_TAB_COLLECTIONS) == 1);
}

static void for_path_by_prefix(void) {
	assert(MenuTabs_forPath("/mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb", &P) == MENU_TAB_CONSOLES);
	assert(MenuTabs_forPath("/mnt/SDCARD/Roms", &P) == MENU_TAB_CONSOLES);
	assert(MenuTabs_forPath("/mnt/SDCARD/Collections/Co-op.txt/Tetris.gb", &P) == MENU_TAB_COLLECTIONS);
	assert(MenuTabs_forPath("/mnt/SDCARD/Tools/Clock.pak", &P) == MENU_TAB_TOOLS);
	assert(MenuTabs_forPath("/mnt/SDCARD/.system/paks/Tools/Settings.pak", &P) == MENU_TAB_TOOLS);
	assert(MenuTabs_forPath("/mnt/SDCARD/Recently Played", &P) == MENU_TAB_HOME);
	assert(MenuTabs_forPath("/mnt/SDCARD", &P) == MENU_TAB_HOME);
	// a prefix is a whole path segment: "Roms2" is not Roms
	assert(MenuTabs_forPath("/mnt/SDCARD/Roms2/x", &P) == MENU_TAB_HOME);
}

static void for_path_defaults_to_home(void) {
	MenuTabPaths p = {"/mnt/SDCARD/Roms", "/mnt/SDCARD/Collections", "/mnt/SDCARD/Tools/tg5040",
					  "/mnt/SDCARD/.system/paks/Tools"};
	assert(MenuTabs_forPath(NULL, &p) == MENU_TAB_HOME);
	assert(MenuTabs_forPath("/mnt/SDCARD/Roms/GB (GB)/a.gb", &p) == MENU_TAB_CONSOLES);
	assert(MenuTabs_forPath("/mnt/SDCARD/Collections/Faves.txt", &p) == MENU_TAB_COLLECTIONS);
	assert(MenuTabs_forPath("/mnt/SDCARD/.system/paks/Tools/Settings.pak", &p) == MENU_TAB_TOOLS);
	assert(MenuTabs_forPath("/mnt/SDCARD/Some Folder/x.gb", &p) == MENU_TAB_HOME);
}

static void keys_round_trip(void) {
	for (int i = 0; i < MENU_TAB_COUNT; i++) {
		MenuTabId id;
		assert(MenuTabs_parseKey(MenuTabs_key((MenuTabId)i), &id) && id == (MenuTabId)i);
		assert(MenuTabs_label((MenuTabId)i)[0]);
	}
	assert(strcmp(MenuTabs_label(MENU_TAB_HOME), "Home") == 0);
	assert(strcmp(MenuTabs_key(MENU_TAB_HOME), "home") == 0);
	MenuTabId id;
	assert(!MenuTabs_parseKey("bogus", &id));
	assert(!MenuTabs_parseKey("", &id));
}

static void legacy_keys_open_home(void) {
	MenuTabId id = MENU_TAB_TOOLS;
	assert(MenuTabs_parseKey("pinned", &id) && id == MENU_TAB_HOME);
	id = MENU_TAB_TOOLS;
	assert(MenuTabs_parseKey("recent", &id) && id == MENU_TAB_HOME);
	assert(MenuTabs_parseKey("home", &id) && id == MENU_TAB_HOME);
	assert(!MenuTabs_parseKey("bogus", &id));
}

static void scroll_offset_keeps_margin(void) {
	assert(MenuTabs_scrollOffset(300, 400, 250, 40, 16) == 0);	 // fits
	assert(MenuTabs_scrollOffset(600, 300, 0, 50, 16) == 0);	 // first tab: no scroll
	assert(MenuTabs_scrollOffset(600, 300, 550, 50, 16) == 300); // last tab: clamped to the end
	int off = MenuTabs_scrollOffset(600, 300, 320, 60, 16);		 // middle tab
	assert(320 - off >= 16 && 320 + 60 - off <= 300 - 16);
}

static void pick_initial_saved_tab_must_own_path(void) {
	MenuTabId all[] = {MENU_TAB_HOME, MENU_TAB_CONSOLES, MENU_TAB_COLLECTIONS, MENU_TAB_TOOLS};
	const char* rom = "/mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb";
	MenuTabId rom_tab = MenuTabs_forPath(rom, &P);
	// Home + a pinned ROM itself, or a ROM inside a pinned folder: owned -> Home
	assert(MenuTabs_pickInitial(all, 4, true, MENU_TAB_HOME, true, rom_tab, true) == MENU_TAB_HOME);
	// Home + an unpinned ROM (Search / Game Switcher launch): the ROM's tab
	assert(MenuTabs_pickInitial(all, 4, true, MENU_TAB_HOME, true, rom_tab, false) == MENU_TAB_CONSOLES);
	// Tools saved + a Roms path (owned only when forPath says Tools): Consoles
	assert(MenuTabs_pickInitial(all, 4, true, MENU_TAB_TOOLS, true, rom_tab, rom_tab == MENU_TAB_TOOLS) ==
		   MENU_TAB_CONSOLES);
	// Collections saved + a collection path (owned, as savedOwnsPath's path_tab == saved): Collections
	MenuTabId coll_tab = MenuTabs_forPath("/mnt/SDCARD/Collections/Favorites.txt", &P);
	assert(coll_tab == MENU_TAB_COLLECTIONS);
	assert(MenuTabs_pickInitial(all, 4, true, MENU_TAB_COLLECTIONS, true, coll_tab, coll_tab == MENU_TAB_COLLECTIONS) ==
		   MENU_TAB_COLLECTIONS);
	// Collections saved + a Roms path: not owned -> Consoles
	assert(MenuTabs_pickInitial(all, 4, true, MENU_TAB_COLLECTIONS, true, rom_tab, rom_tab == MENU_TAB_COLLECTIONS) ==
		   MENU_TAB_CONSOLES);
	// saved tab hidden -> path's tab; path's tab hidden -> Home
	MenuTabId some[] = {MENU_TAB_HOME, MENU_TAB_TOOLS};
	assert(MenuTabs_pickInitial(some, 2, true, MENU_TAB_COLLECTIONS, true, MENU_TAB_TOOLS, true) == MENU_TAB_TOOLS);
	assert(MenuTabs_pickInitial(some, 2, true, MENU_TAB_TOOLS, true, rom_tab, false) == MENU_TAB_HOME);
	assert(MenuTabs_pickInitial(some, 2, false, MENU_TAB_HOME, true, rom_tab, false) == MENU_TAB_HOME);
}

static void cold_boot_opens_home(void) {
	MenuTabId tabs[] = {MENU_TAB_HOME, MENU_TAB_CONSOLES, MENU_TAB_TOOLS};
	assert(MenuTabs_pickInitial(tabs, 3, false, MENU_TAB_HOME, false, MENU_TAB_HOME, false) == MENU_TAB_HOME);
	// a saved Tools tab without a path (e.g. HDMI restart) is kept
	assert(MenuTabs_pickInitial(tabs, 3, true, MENU_TAB_TOOLS, false, MENU_TAB_HOME, false) == MENU_TAB_TOOLS);
	// a saved, now hidden tab falls to the path's tab, then Home
	MenuTabId two[] = {MENU_TAB_HOME, MENU_TAB_TOOLS};
	assert(MenuTabs_pickInitial(two, 2, true, MENU_TAB_CONSOLES, true, MENU_TAB_CONSOLES, true) == MENU_TAB_HOME);
}

static void home_launch_owns_path(void) {
	MenuTabId tabs[] = {MENU_TAB_HOME, MENU_TAB_CONSOLES};
	// launched from Home (marker) → Home, even though the ROM lives under Consoles
	assert(MenuTabs_pickInitial(tabs, 2, true, MENU_TAB_HOME, true, MENU_TAB_CONSOLES, true) == MENU_TAB_HOME);
	// a Search/Switcher launch while on Home (no marker, not a pin) → the ROM's own tab
	assert(MenuTabs_pickInitial(tabs, 2, true, MENU_TAB_HOME, true, MENU_TAB_CONSOLES, false) ==
		   MENU_TAB_CONSOLES);
}

int main(void) {
	visible_fixed_order_and_toggles();
	home_always_first_and_visible();
	visible_simple_mode_tools_is_settings();
	wrap_steps_both_ways();
	resolve_vanished_tab_picks_next_then_last();
	home_survives_no_pins();
	for_path_by_prefix();
	for_path_defaults_to_home();
	keys_round_trip();
	legacy_keys_open_home();
	scroll_offset_keeps_margin();
	pick_initial_saved_tab_must_own_path();
	cold_boot_opens_home();
	home_launch_owns_path();
	printf("test_menutabs_model: ok\n");
	return 0;
}
