#ifndef MENUTABS_MODEL_H
#define MENUTABS_MODEL_H

#include <stdbool.h>

// Fixed tab order. Home (the SDCARD_PATH listing: pins) is always visible.
typedef enum { MENU_TAB_HOME = 0,
			   MENU_TAB_CONSOLES,
			   MENU_TAB_COLLECTIONS,
			   MENU_TAB_TOOLS,
			   MENU_TAB_COUNT } MenuTabId;

typedef struct {
	bool show_consoles, has_consoles;
	bool show_collections, has_collections;
	bool show_tools, has_tools;
	bool simple_mode, has_settings; // simple mode: the Tools tab is just Settings
} MenuTabInputs;

typedef struct {
	const char* roms;		 // ROMS_PATH
	const char* collections; // COLLECTIONS_PATH
	const char* tools;		 // TOOLS_PATH
	const char* sys_tools;	 // PAKS_PATH "/Tools"
} MenuTabPaths;

// Visible tabs in fixed order into out; returns the count (>= 1: Home is always first).
int MenuTabs_visible(const MenuTabInputs* in, MenuTabId out[MENU_TAB_COUNT]);
// index + delta, wrapping over count (count <= 0 returns 0).
int MenuTabs_wrap(int count, int index, int delta);
// Index of id in tabs, or -1.
int MenuTabs_indexOf(const MenuTabId* tabs, int count, MenuTabId id);
// Index for `current` in the new visible list: its own index if still visible, else the first
// later tab in fixed order, else the last tab.
int MenuTabs_resolve(const MenuTabId* tabs, int count, MenuTabId current);
// The tab a saved/launched path belongs to (Home when nothing else claims it).
MenuTabId MenuTabs_forPath(const char* path, const MenuTabPaths* paths);
const char* MenuTabs_label(MenuTabId id); // "Home", "Consoles", "Collections", "Tools"
const char* MenuTabs_key(MenuTabId id);	  // "home", "consoles", "collections", "tools"
// Also accepts the legacy "pinned" and "recent" keys, both as Home.
bool MenuTabs_parseKey(const char* key, MenuTabId* out);
// Horizontal scroll for a tab strip wider than its view: 0 when it fits, else centres the current
// tab, kept inside [margin, view_w - margin] (so an edge fade never dims it), clamped to
// [0, content_w - view_w] (the first and last tabs sit at the ends).
int MenuTabs_scrollOffset(int content_w, int view_w, int cur_x, int cur_w, int margin);
// The tab to open at boot. The saved tab (saved_valid: MENU_TAB_PATH parsed) wins when it is
// visible and either there is no saved path or the path belongs to it (saved_owns_path, e.g. a
// Search launch from Home saves an unpinned ROM, which does not). Otherwise the path's tab
// (path_tab = MenuTabs_forPath), then Home.
MenuTabId MenuTabs_pickInitial(const MenuTabId* tabs, int count, bool saved_valid, MenuTabId saved,
							   bool has_path, MenuTabId path_tab, bool saved_owns_path);

#endif
