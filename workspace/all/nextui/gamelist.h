#ifndef GAMELIST_H
#define GAMELIST_H

#include "api.h"
#include "sdl.h"
#include "types.h"
#include "menutabs_model.h"
#include <stdbool.h>

typedef struct {
	int screen;			  // screen to show next (SCREEN_GAMELIST if unchanged)
	int animdir;		  // slide animation requested by this input (ANIM_NONE if none)
	bool folderbgchanged; // request a background layer refresh
} GameListResult;

void GameList_init(bool simple_mode);

// Handle one frame of input for the game list screen.
// `dirty` is the caller's redraw flag; it may already be set by other systems
// (power/status bar) and is also raised here on any visible change.
GameListResult GameList_handleInput(unsigned long now, int currentScreen,
									IndicatorType show_setting, bool* dirty);

// Render the full game list screen (menu bar excluded; caller draws that).
void GameList_render(SDL_Surface* screen, int lastScreen,
					 IndicatorType show_setting, SDL_Surface* blackBG);

// Draw the info band (fade, scroll arrows, the selected row's info text) onto LAYER_OVERLAY (above the
// thumbnail layer). GameList_render calls it; the main loop calls it again after a page slide, which clears
// that layer. Skipped while the context menu is open.
void GameList_renderInfoLayer(void);
// The current screen's main menu style (MENU_STYLE_*): a root tab's Layouts row, or the game lists' row. Home →
// MENU_STYLE_LIST (Home draws itself).
int GameList_currentStyle(void);
// A folder game: an ENTRY_DIR under Roms holding its folder-named .cue/.m3u (stats the disk: cache the answer).
bool GameList_entryIsFolderGame(Entry* entry);
// The List rows' text start: the 24 dp gutter on the main menu, the 14 dp list inset in game lists.
int GameList_textX(void);
// Rows on List screens for the current level (the stack's depth), down to the fixed band (InfoBand_fixedLayout):
// main-menu tabs start 12 dp under the tab row, game lists right under their header. GameList_rowCountAt names the level, for a
// Directory built before it reaches the stack (root = a tab's own list at stack[0]).
int GameList_rowCount(void);
int GameList_rowCountAt(bool root);
// Scroll-text (marquee) state, driven by the main loop's idle path.
bool GameList_scrollBusy(void);		   // still needs animation/render ticks
bool GameList_pillAnimating(void);	   // selection pill mid-glide, keep redrawing
bool GameList_scrollIsScrolling(void); // actively scrolling right now
void GameList_scrollTickIdle(void);	   // advance marquee on non-dirty frames
void GameList_clearScroll(void);	   // drop cached scroll state (screen switch/exit)

// Invalidate the folder-background cache so the next render reloads it.
// Call when another screen clears the shared background surface.
void GameList_invalidateBackground(void);

// Run a context-menu action selected in the overlay (the id built in
// GameList_handleInput). Called by nextui.c when ContextMenu returns SELECTED.
void GameList_runContextAction(int id);

// Netplay-capable = the entry's owning emu pak ships a "netplay" marker file.
bool GameList_entryNetplayCapable(Entry* entry);

// Home's context menu: the item set `entry` gets in a list (a game's ROM-listing items, a tool's Pin/Unpin Tool),
// plus the root's own items. The menu then acts on a copy of `entry`, not on the pins list's row.
void GameList_openContextMenuFor(Entry* entry, bool is_pin, bool is_continue);
// The context menu closed without an action (nextui.c); drops Home's entry copy.
void GameList_contextMenuClosed(void);
// The root's tab switch (L1/R1 and LEFT/RIGHT), for Home's edge moves; GameList_handleInput raises
// folderbgchanged when the tab generation changed.
void GameList_switchTab(int delta, bool* dirty);
// Open a visible tab at the root (Home's "Pick a game" → Consoles), with the same clears as a switch.
void GameList_openTab(MenuTabId id, bool* dirty);
// Simple mode: launching Settings asks for the parent PIN (true = go ahead).
bool GameList_settingsPinAllows(Entry* entry);

#endif // GAMELIST_H
