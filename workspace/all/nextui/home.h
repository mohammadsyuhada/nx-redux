#ifndef HOME_H
#define HOME_H

#include "sdl.h"
#include "types.h"
#include <stdbool.h>

// The Home tab's dashboard: Continue, the stats card and the pin tiles (landscape split, §9 of the layout
// reference). It owns the screen whenever the root is on Home; stack[0] stays the pins Directory.

bool Home_active(void); // stack->count == 1 && MenuTabs_current() == MENU_TAB_HOME
// Body + hints. The tab row and the top band are drawn by nextui.c (the band only while Home_scrolled()).
void Home_render(SDL_Surface* screen, int lastScreen);
// Returns true when it handled input (the D-pad, A, MENU); sets *dirty. Edge moves switch tab through
// GameList_switchTab. SELECT, START, L1/R1 and the rest return false for the root's own handlers.
bool Home_handleInput(unsigned long now, bool* dirty);
// UP on the tab row: focus the last pin (HomeLayout_bottomFrom); no pins: the top item stays.
void Home_focusBottom(void);
bool Home_animating(void); // scroll tween, selection crossfade, card flip (one settled frame after each)
bool Home_scrolled(void);  // offset > 0: nextui.c draws the top band fade
// Data changed (pins, rescan, menu show): rebuild the layout on the next use, clamp the focus, request stats.
void Home_reset(void);
// Continue's or the focused pin's entry (borrowed), NULL for the stats card and the Pick-a-game card.
Entry* Home_focusedEntry(void);
void Home_quit(void); // free the drawing surfaces and Continue's entry

#endif // HOME_H
