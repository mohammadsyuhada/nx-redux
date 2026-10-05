// Main menu tab rules, SDL-free so they can be host-tested
// (common/tests/test_menutabs_model.c). menutabs.c applies them.

#include "menutabs_model.h"

#include <string.h>

static const char* tab_labels[MENU_TAB_COUNT] = {"Home", "Consoles", "Collections", "Tools"};
static const char* tab_keys[MENU_TAB_COUNT] = {"home", "consoles", "collections", "tools"};

int MenuTabs_visible(const MenuTabInputs* in, MenuTabId out[MENU_TAB_COUNT]) {
	int n = 0;
	if (!in->hide_home)
		out[n++] = MENU_TAB_HOME;
	if (in->show_consoles && in->has_consoles)
		out[n++] = MENU_TAB_CONSOLES;
	if (in->show_collections && in->has_collections)
		out[n++] = MENU_TAB_COLLECTIONS;
	// simple mode keeps Settings reachable whatever the Tools toggle says
	if (in->simple_mode ? in->has_settings : (in->show_tools && in->has_tools))
		out[n++] = MENU_TAB_TOOLS;
	if (n == 0) // Home hidden and nothing else to show: keep it, the menu needs a tab
		out[n++] = MENU_TAB_HOME;
	return n;
}

int MenuTabs_wrap(int count, int index, int delta) {
	if (count <= 0)
		return 0;
	int i = (index + delta) % count;
	return i < 0 ? i + count : i;
}

int MenuTabs_indexOf(const MenuTabId* tabs, int count, MenuTabId id) {
	for (int i = 0; i < count; i++)
		if (tabs[i] == id)
			return i;
	return -1;
}

int MenuTabs_resolve(const MenuTabId* tabs, int count, MenuTabId current) {
	int i = MenuTabs_indexOf(tabs, count, current);
	if (i >= 0)
		return i;
	for (i = 0; i < count; i++)
		if (tabs[i] > current)
			return i;
	return count > 0 ? count - 1 : 0;
}

// path == base, or path continues below base with a '/'
static int underPath(const char* path, const char* base) {
	if (!base || !base[0])
		return 0;
	size_t n = strlen(base);
	return strncmp(path, base, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

MenuTabId MenuTabs_forPath(const char* path, const MenuTabPaths* paths) {
	if (!path)
		return MENU_TAB_HOME;
	if (underPath(path, paths->roms))
		return MENU_TAB_CONSOLES;
	if (underPath(path, paths->collections))
		return MENU_TAB_COLLECTIONS;
	if (underPath(path, paths->tools) || underPath(path, paths->sys_tools))
		return MENU_TAB_TOOLS;
	return MENU_TAB_HOME;
}

const char* MenuTabs_label(MenuTabId id) {
	return (id >= 0 && id < MENU_TAB_COUNT) ? tab_labels[id] : "";
}

const char* MenuTabs_key(MenuTabId id) {
	return (id >= 0 && id < MENU_TAB_COUNT) ? tab_keys[id] : "";
}

bool MenuTabs_parseKey(const char* key, MenuTabId* out) {
	if (!key)
		return false;
	for (int i = 0; i < MENU_TAB_COUNT; i++) {
		if (strcmp(key, tab_keys[i]) == 0) {
			*out = (MenuTabId)i;
			return true;
		}
	}
	// legacy keys from the Pinned/Recent tab set
	if (strcmp(key, "pinned") == 0 || strcmp(key, "recent") == 0) {
		*out = MENU_TAB_HOME;
		return true;
	}
	return false;
}

int MenuTabs_scrollOffset(int content_w, int view_w, int cur_x, int cur_w, int margin) {
	if (content_w <= view_w)
		return 0;
	int off = cur_x + cur_w / 2 - view_w / 2;
	// keep the label inside [margin, view_w - margin] (its left edge wins when it can't fit)
	if (cur_x + cur_w - off > view_w - margin)
		off = cur_x + cur_w - (view_w - margin);
	if (cur_x - off < margin)
		off = cur_x - margin;
	if (off < 0)
		off = 0;
	if (off > content_w - view_w)
		off = content_w - view_w;
	return off;
}

MenuTabId MenuTabs_pickInitial(const MenuTabId* tabs, int count, bool saved_valid, MenuTabId saved,
							   bool has_path, MenuTabId path_tab, bool saved_owns_path) {
	if (saved_valid && MenuTabs_indexOf(tabs, count, saved) >= 0 && (!has_path || saved_owns_path))
		return saved;
	if (has_path && MenuTabs_indexOf(tabs, count, path_tab) >= 0)
		return path_tab;
	return count > 0 ? tabs[0] : MENU_TAB_HOME; // the cold-boot tab: Home, unless it is hidden
}

void MenuTabs_clampWindow(int count, int rows, int* selected, int* start, int* end) {
	int sel = *selected, first = *start, last = *end;
	if (count < 0)
		count = 0;
	if (rows < 1)
		rows = 1;
	// buildRoot's rule: a hint that no longer fits the list starts over at the top
	if (sel < 0 || sel >= count || last > count || first < 0 || first > sel || (last && sel >= last)) {
		sel = 0;
		first = 0;
		last = 0;
	}
	int visible = count < rows ? count : rows;
	if (last == 0 || last - first != visible) {
		// rows changed (a layout or scale switch while the root was parked): same first row where it
		// still fits, else the window ends on the selection; never past the list's end
		if (sel >= first + visible)
			first = sel - visible + 1;
		if (first > count - visible)
			first = count - visible;
		if (first < 0)
			first = 0;
		last = first + visible;
	}
	*selected = sel;
	*start = first;
	*end = last;
}

static bool slotIdOk(MenuTabSlot* slots, MenuTabId id) {
	return slots && (int)id >= 0 && id < MENU_TAB_COUNT;
}

void* MenuTabs_slotPark(MenuTabSlot* slots, MenuTabId id, void* root, int selected, int start, int end) {
	if (!slotIdOk(slots, id))
		return root; // nowhere to keep it: the caller frees it
	void* displaced = slots[id].root;
	slots[id] = (MenuTabSlot){root, true, selected, start, end};
	return displaced == root ? NULL : displaced;
}

void* MenuTabs_slotTake(MenuTabSlot* slots, MenuTabId id) {
	if (!slotIdOk(slots, id))
		return NULL;
	void* root = slots[id].root;
	slots[id].root = NULL;
	return root;
}

void* MenuTabs_slotDrop(MenuTabSlot* slots, MenuTabId id) {
	return MenuTabs_slotTake(slots, id); // the same move; named for what the caller means
}

MenuReloadPlan MenuTabs_reloadPlan(unsigned what) {
	MenuReloadPlan plan = {0};
	plan.validate_pins = (what & MENU_RELOAD_PINS) != 0;
	plan.load_recents = (what & MENU_RELOAD_RECENTS) != 0;
	plan.check_consoles = (what & MENU_RELOAD_ROMS) != 0;
	plan.check_collections = (what & MENU_RELOAD_COLLECTIONS) != 0;
	plan.check_all = (what & MENU_RELOAD_ALL) == MENU_RELOAD_ALL;
	if (what & MENU_RELOAD_PINS)
		plan.stale_tabs |= 1u << MENU_TAB_HOME;
	if (what & MENU_RELOAD_ROMS)
		plan.stale_tabs |= 1u << MENU_TAB_CONSOLES;
	if (what & MENU_RELOAD_COLLECTIONS)
		plan.stale_tabs |= 1u << MENU_TAB_COLLECTIONS;
	if (what & MENU_RELOAD_TOOLS)
		plan.stale_tabs |= 1u << MENU_TAB_TOOLS;
	return plan;
}
