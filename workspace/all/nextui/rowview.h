#ifndef ROWVIEW_H
#define ROWVIEW_H

#include "sdl.h"
#include <stdbool.h>

// The Carousel and Backdrop styles (§8b) over the current Directory `top`, one row with the selection in the middle,
// driven by one eased position (row_model.c):
// - the main-menu Carousel (Consoles, Collections, Tools): the frameless row on plain black, no picture, no caption
//   (Consoles' and Collections' "N games" in the accent sit with the selected item);
// - a game list's Carousel (the tile row, the caption under it) or Backdrop (box art over the selected game's
//   picture, the caption under it).
// GameList_render/GameList_handleInput delegate here; the tab row, the title and the hint bar stay theirs. No info
// band.

bool RowView_active(void); // the style is Carousel or Backdrop (and not Home)
// nextui.c, before the top band and the bar: a Backdrop game list's picture (the selected game's screenshot, dimmed
// and shaded, crossfading over 0.35 s) drawn full screen as the bottom-most layer of `screen`. Draws nothing on any
// other screen, the main-menu tabs included. Returns RowView_backdropPicture().
bool RowView_renderPicture(SDL_Surface* screen);
// RowView_renderPicture will paint every pixel of the screen this frame (a Backdrop game list: the picture, or black):
// the caller can skip clearing it first.
bool RowView_paintsScreen(void);
// The body: the row (far items first), the caption, plain black under a row without a picture (no hints).
void RowView_render(SDL_Surface* screen, int lastScreen);
// The D-pad only: LEFT/RIGHT step the selection (at the root a fresh press past an end switches tab; a held key and
// game lists stop), UP/DOWN do nothing. Returns true when it handled a D-pad press; false for every other button.
bool RowView_handleInput(unsigned long now, bool* dirty);
// The slide, the picture crossfade or a main-menu count's glide or fade is running (one settled frame after each).
bool RowView_animating(void);
// Build ahead (between frames: no redraw) the items the last frame's row needs next, until `deadline` (SDL ticks):
// the selection's neighbours (plain and lit) and the items a step brings into view. The Vertical orientation's stack
// builds its own. True while more remains; false once done, or when the row it was for is gone (the next render
// starts it again).
bool RowView_prefetchStep(Uint32 deadline);
// A Backdrop game list's picture is on screen now (the last RowView_renderPicture): no top band.
bool RowView_backdropPicture(void);
// B out of a Backdrop game list (menu_transition.h): the outgoing screen, its last picture included, fades to black over
// 0.35 s, then the list closes. RowView_beginExit starts it when a picture is on screen and animations are on, and
// returns false otherwise (close now). While it runs the caller handles no input: once per frame RowView_exitStep
// says when to close (the time is up, or a key was pressed: close, then handle that key on the returned-to screen).
// RowView_renderExit darkens the finished frame (nextui.c, after the body and the hints). A launch never fades.
bool RowView_beginExit(void);
bool RowView_exiting(void);
bool RowView_exitStep(bool key_pressed);
void RowView_renderExit(SDL_Surface* screen);
// Free the cached surfaces and the per-list item kinds.
void RowView_quit(void);

#endif // ROWVIEW_H
