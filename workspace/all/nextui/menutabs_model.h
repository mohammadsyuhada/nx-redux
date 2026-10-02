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

// A tab root's selection window for a list of `count` rows shown `rows` at a time (rows >= 1). A selection that
// doesn't fit the list (out of range, or outside a stale window) resets to the top, as a fresh root does; a window
// whose size isn't min(count, rows) -- the list or the row count changed since it was set -- is rebuilt around
// the selection, kept visible and inside the list. An empty list ends up 0/0/0.
void MenuTabs_clampWindow(int count, int rows, int* selected, int* start, int* end);

// Parked tab roots (T1-1): a tab switch parks the outgoing tab's root Directory and takes the incoming one's, so
// L1/R1 costs no card I/O. The stack owns stack[0]; a slot holds only a tab that is NOT current, so no root is
// ever reachable from both. `root` is opaque here (a Directory* in menutabs.c). hint/selected/start/end outlive a
// dropped root, so a rebuilt one reopens on the same row.
typedef struct {
	void* root;
	bool hint;
	int selected, start, end;
} MenuTabSlot;

// Park root (with its selection as the hint) in slots[id]. Returns the root it displaced, for the caller to
// free (NULL when the slot was empty).
void* MenuTabs_slotPark(MenuTabSlot* slots, MenuTabId id, void* root, int selected, int start, int end);
// Take slots[id]'s root out (NULL when none is parked); the hint stays.
void* MenuTabs_slotTake(MenuTabSlot* slots, MenuTabId id);
// Empty slots[id] for a rebuild, keeping its hint. Returns the root for the caller to free (or NULL).
void* MenuTabs_slotDrop(MenuTabSlot* slots, MenuTabId id);

// What a reload re-reads (T2-11): callers pass a mask of MENU_RELOAD_* bits; MENU_RELOAD_ALL is every bit. Every
// caller's mask includes PINS.
enum {
	MENU_RELOAD_PINS = 1,
	MENU_RELOAD_RECENTS = 2,
	MENU_RELOAD_ROMS = 4,
	MENU_RELOAD_COLLECTIONS = 8,
	MENU_RELOAD_TOOLS = 16,
	MENU_RELOAD_ALL = 31
};
typedef struct {
	bool validate_pins;		// PINS: re-check the pinned entries
	bool load_recents;		// RECENTS: re-read the recents list
	bool check_consoles;	// ROMS: re-check whether the Consoles tab has content
	bool check_collections; // COLLECTIONS: re-check whether the Collections tab has content
	bool check_all;			// ALL: the full visibility pass
	unsigned stale_tabs;	// bitmask of (1u << MenuTabId) whose parked roots are out of date
} MenuReloadPlan;
// The reload plan for a mask of MENU_RELOAD_* bits. Tabs: PINS->Home, ROMS->Consoles, COLLECTIONS->Collections,
// TOOLS->Tools; RECENTS marks no tab. Pure.
MenuReloadPlan MenuTabs_reloadPlan(unsigned what);

#endif
