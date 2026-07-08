#ifndef GAMELIST_H
#define GAMELIST_H

#include "api.h"
#include "sdl.h"
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

// Scroll-text (marquee) state, driven by the main loop's idle path.
bool GameList_scrollBusy(void);		   // still needs animation/render ticks
bool GameList_scrollIsScrolling(void); // actively scrolling right now
void GameList_scrollTickIdle(void);	   // advance marquee on non-dirty frames
void GameList_clearScroll(void);	   // drop cached scroll state (screen switch/exit)

// Invalidate the folder-background cache so the next render reloads it.
// Call when another screen clears the shared background surface.
void GameList_invalidateBackground(void);

// Run a context-menu action selected in the overlay (the id built in
// GameList_handleInput). Called by nextui.c when ContextMenu returns SELECTED.
void GameList_runContextAction(int id);

#endif // GAMELIST_H
