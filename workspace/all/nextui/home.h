#ifndef HOME_H
#define HOME_H

#include "sdl.h"
#include "types.h"
#include <stdbool.h>

// The Home tab's dashboard: Continue, the stats card and the pin tiles (landscape split, §9 of the layout
// reference). It owns the screen whenever the root is on Home; stack[0] stays the pins Directory. With Layouts > Home
// layout on Carousel: one row (Continue, then the pinned games) and a dock of the pinned tools under it instead
// (home_carousel_layout.h).

bool Home_active(void); // stack->count == 1 && MenuTabs_current() == MENU_TAB_HOME
// Body + hints. The tab row and the top band are drawn by nextui.c (the band only while Home_scrolled()).
void Home_render(SDL_Surface* screen, int lastScreen);
// Returns true when it handled input (the D-pad, A, MENU); sets *dirty. Edge moves switch tab through
// GameList_switchTab. SELECT, START, L1/R1 and the rest return false for the root's own handlers.
bool Home_handleInput(unsigned long now, bool* dirty);
// UP on the tab row: focus the last pin (HomeLayout_bottomFrom); no pins: the top item stays. Carousel: the dock when
// it has tools, else the row's selection stays.
void Home_focusBottom(void);
bool Home_animating(void); // scroll tween, selection crossfade, card flip (one settled frame after each)
bool Home_scrolled(void);  // offset > 0: nextui.c draws the top band fade (never on the Carousel, which doesn't scroll)
// Data changed (pins, rescan, menu show): rebuild the layout on the next use, clamp the focus, request stats.
void Home_reset(void);
// Continue's or the focused pin's entry (borrowed), NULL for the stats card and the Pick-a-game card.
Entry* Home_focusedEntry(void);
// Layouts > Home layout: List (home_list_layout.h). With a Continue, Home is one pill list that gamelist.c draws as its
// main-menu List over Home_listDir (Continue, the pinned games, the pinned tools; Home owns the input); without one (a
// fresh install) Home draws the Carousel's Pick a game itself and Home_isList is false.
bool Home_isList(void);
Directory* Home_listDir(void); // NULL unless Home_isList(); entries borrowed, valid until the next rebuild
int Home_listTop(void);		   // the first row's top (px): under the strip
// The selected game's picture for the List's background (imgloader.c startLoadThumb; homeart.h HomeArt_listPath), ""
// for a tool or a plain folder.
void Home_listArtPath(char* out, size_t size);
const char* Home_listToolIcon(void);		 // the selected tool's icon (MenuArt file), else NULL
void Home_renderListStrip(SDL_Surface* dst); // the stats strip, left-aligned over the rows
bool Home_listHasStrip(void);
void Home_renderListHints(SDL_Surface* dst); // Home's hint bar (as Grid's and the Carousel's)
// The "Continue" tag before Continue's name: its width with the gap after it, and the tag drawn at x, centred on the
// text line (text_y, text_h); lit under the selection pill; a sprite over the screen when sprite (else into dst).
int Home_listTagWidth(void);
void Home_listDrawTag(SDL_Surface* dst, int x, int text_y, int text_h, bool lit, bool sprite);
void Home_quit(void); // free the drawing surfaces and Continue's entry

#endif // HOME_H
