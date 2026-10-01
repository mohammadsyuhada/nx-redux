#ifndef ROWVIEW_H
#define ROWVIEW_H

#include "sdl.h"
#include <stdbool.h>

// The Carousel and Backdrop styles (§8b) over the current Directory `top`: a root tab (Consoles, Collections,
// Tools) or a game list whose Layouts row is Carousel or Backdrop. One row with the selection in the middle, driven
// by one eased position (row_model.c). GameList_render/GameList_handleInput delegate here; the tab row, the title
// and the hint bar stay theirs. No info band: the selection's caption sits under the row.

bool RowView_active(void); // the style is Carousel or Backdrop (and not Home)
// nextui.c, before the top band, the bar and the tab row: a Backdrop game row's picture (the selected game's
// screenshot, dimmed and shaded, crossfading over 0.35 s) drawn full screen as the bottom-most layer of `screen`.
// Draws nothing on any other screen. Returns RowView_backdropPicture().
bool RowView_renderPicture(SDL_Surface* screen);
// RowView_renderPicture will paint every pixel of the screen this frame (a Backdrop game row: the picture, or black):
// the caller can skip clearing it first.
bool RowView_paintsScreen(void);
// The body: the row (far items first), the caption, plain black under a row without a picture (no hints).
void RowView_render(SDL_Surface* screen, int lastScreen);
// The D-pad only: LEFT/RIGHT step the selection (at the root a fresh press past an end switches tab; a held key and
// game lists stop), UP/DOWN do nothing. Returns true when it handled a D-pad press; false for every other button.
bool RowView_handleInput(unsigned long now, bool* dirty);
// The slide or the picture crossfade is running (one settled frame after each).
bool RowView_animating(void);
// A Backdrop picture is on screen now (the last RowView_renderPicture): no top band, the tab row over the art.
bool RowView_backdropPicture(void);
// Free the cached surfaces and the per-list item kinds.
void RowView_quit(void);

#endif // ROWVIEW_H
