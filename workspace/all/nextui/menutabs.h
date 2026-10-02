#ifndef MENUTABS_H
#define MENUTABS_H

#include "menutabs_model.h"
#include "sdl.h"
#include "types.h"
#include <stdbool.h>

void MenuTabs_init(void); // compute the visible tabs from settings + content
int MenuTabs_count(void);
MenuTabId MenuTabs_at(int index);
MenuTabId MenuTabs_current(void);
// Only assigns the current tab (pathToStack builds stack[0] itself); drops id's parked root.
void MenuTabs_setCurrent(MenuTabId id);
// Something changed what a tab that isn't current lists, without a MenuTabs_reload (a new collection from Add to
// Collection): its parked root is dropped and rebuilt on its next visit. No-op for the current tab.
void MenuTabs_dropCached(MenuTabId id);
// Menu_quit: free the parked roots.
void MenuTabs_quit(void);
bool MenuTabs_isVisible(MenuTabId id);
const char* MenuTabs_path(MenuTabId id);			 // the stack[0] path for a tab
MenuTabId MenuTabs_forPathVisible(const char* path); // MenuTabs_forPath, then Home
// The tab's main menu style category (MENU_CAT_* in config.h), or -1 for Home.
int MenuTabs_styleCategory(MenuTabId id);
// Make `id` current with its root at stack[0], replacing the whole stack and setting `top`. The outgoing root is
// parked and id's parked root reused (re-windowed for today's row count); with none parked a fresh Directory is
// built on the tab's remembered selection.
void MenuTabs_openRoot(MenuTabId id);
// L1/R1 at the root: park the current tab's root, move by delta (wrapping), open it.
// Returns true when the tab changed.
bool MenuTabs_step(int delta);
// Something changed what the tabs hold (pin, unpin, delete, refresh). Drop every parked root, recompute the
// visible tabs and rebuild stack[0]; if the current tab vanished, open the resolved neighbour at its root (the
// lists pushed over the old tab are popped). keep_selected clamps.
void MenuTabs_reload(int keep_selected);
// Bumped whenever MenuTabs_openRoot/MenuTabs_reload replace stack[0] (a tab switch or reload). Readers use it
// to tell a tab change from a push/pop; per-list caches key on Directory.serial (which also changes on a
// push, pop or openDirectory stack rebuild) alongside it.
unsigned MenuTabs_generation(void);
// The next MenuTabs_saveState records that the launch came from Home itself (Continue or a pin):
// boot then reopens Home even when the ROM belongs to another tab. Call only right before Entry_open:
// Entry_open clears the mark when it returns, whether or not anything launched.
void MenuTabs_markHomeLaunch(void);
// Drop a pending mark (Entry_open calls this last, so a mark never outlives its own open).
void MenuTabs_clearHomeLaunch(void);
// saveLast() hook: write the current tab key to MENU_TAB_PATH ("home\nlaunch\n" after
// MenuTabs_markHomeLaunch while on Home; "<tab>\ntools\n" from the Tools list pushed over <tab> while the
// Tools tab is hidden).
// Clears the mark.
void MenuTabs_saveState(void);
// MENU_TAB_PATH carries a launch from Home itself (the "launch" line): loadLast then only reselects Home's row.
bool MenuTabs_savedHomeLaunch(void);
// MENU_TAB_PATH carries the "tools" line: the launch came from the Tools list pushed over the saved tab.
bool MenuTabs_savedToolsPush(void);
// The tab to open at boot: MENU_TAB_PATH if visible, else the tab of last_path, else Home.
MenuTabId MenuTabs_initialTab(const char* last_path);
// Tab-row focus (docs/superpowers/specs/2026-10-01-menu-sp6-tab-focus-design.md): UP from the top of a tab's content
// moves focus to the tab row. Meaningful only at the root: setFocused(true) is refused below it, and every path that
// pushes a list clears it (Entry_open, openDirectory, the context menu's Tools push), so B back to the root
// returns to the content; MenuTabs_focused is a pure read. MenuTabs_openRoot clears it (boot, a launch return);
// MenuTabs_step keeps it (LEFT/RIGHT, L1/R1 on the row).
bool MenuTabs_focused(void);
// Focus moved by the d-pad, A or B (onto the row, or back to the content): the dim tweens over 180 ms.
void MenuTabs_setFocused(bool focused);
// Focus lost to anything else (SELECT, START, MENU, the F keys, a launch, a list opened, a context menu): the content
// snaps lit, so nothing shows it part-dimmed.
void MenuTabs_leaveFocus(void);
// The opacity of the whole content below the tab row (LIST-LAYOUT §5): 1, easing to 0.4 over 180 ms while the tab row
// has focus and back when it returns to the content (MenuTabs_setFocused); MenuTabs_leaveFocus, off the main menu or
// animations off snap it. Drawn as one layer by contentdim.c.
float MenuTabs_contentAlpha(void);
// Once per frame: the dim is moving (true once more on its settled frame), so the host keeps drawing.
bool MenuTabs_dimAnimating(void);
// Tab row (Task 5) and its glide. While focused, the current label wears the selection plate (accent pill, onAccent text)
// and the underline hides.
void MenuTabs_renderRow(SDL_Surface* screen, int ow);
bool MenuTabs_animating(void);
// Read-only (never ticks): the underline is mid-glide at the root. For checks later in a frame
// (upload deferral) that must not consume MenuTabs_animating()'s one settled-frame "true".
bool MenuTabs_underlineGliding(void);

#endif
