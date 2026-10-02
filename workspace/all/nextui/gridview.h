#ifndef GRIDVIEW_H
#define GRIDVIEW_H

#include "sdl.h"
#include <stdbool.h>

// The Grid style (§8) over the current Directory `top`: a root tab (Consoles, Collections, Tools) or a game list
// whose Layouts row is Grid. GameList_render/GameList_handleInput delegate here; the tab row, the title, the top
// band and the hint bar stay theirs.

bool GridView_active(void); // GameList_currentStyle() == MENU_STYLE_GRID && !Home_active()
// The body: plain black, the visible tiles, the cut-column shade and the edge fades (no hints).
void GridView_render(SDL_Surface* screen, int lastScreen);
// The D-pad only (moves, and at the root a fresh press past an end switches tab). Returns true when it handled a
// D-pad press (sets *dirty, and *switched_tab when the tab changed); false for every other button.
bool GridView_handleInput(unsigned long now, bool* dirty, bool* switched_tab);
// UP on the tab row: select the bottom row's tile in the selection's column (GridLayout_bottomOf).
void GridView_focusBottom(void);
// The slide tween or the lit crossfade is running (one settled frame after each).
bool GridView_animating(void);
// Compose ahead (between frames: no redraw) the tiles the last frame's grid needs next, until `deadline` (SDL
// ticks): the columns just off screen at the slide's target and the selection's neighbours' lit looks. True while
// more remains; false once done, or when the grid it was for is gone (the next render starts it again).
bool GridView_prefetchStep(Uint32 deadline);
// Free the cached surfaces and the per-list tile kinds.
void GridView_quit(void);

#endif // GRIDVIEW_H
