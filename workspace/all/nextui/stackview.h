#ifndef STACKVIEW_H
#define STACKVIEW_H

#include "sdl.h"
#include <stdbool.h>

// The Vertical orientation (LIST-LAYOUT §8f): the Carousel's or Backdrop's row stood on end, over the current
// Directory `top`, driven by one eased position (stack_model.c):
// - the main-menu Carousel-Vertical (§8f.3): Consoles, Collections and Tools on plain black with SP8's frameless
//   slots (rowview_shared.h), centred, fading 20 dp to black at the body's edges;
// - a game list's Backdrop-Vertical (§8f.5): box art over the Backdrop picture, no edge fade;
// - a game list's Carousel-Vertical (§8f.6): the game tiles on black, darkening, with the edge fade.
//   Both game stacks sit left of centre with the game caption to their right (§8f.4).
//
// rowview.c delegates here while it is active (RowView_render, RowView_handleInput, RowView_animating), so the rest
// of GameList treats it as the Carousel it is: the tab row, the hint bar, the tab-focus dim layer and every button
// but the D-pad stay GameList's.

// A main-menu tab in Carousel, or a game list in Carousel or Backdrop, whose effective orientation is Vertical
// (GameList_currentOrientation).
bool StackView_active(void);
// The body: the stack (far items first), the selection's "N games", the edge fade. Over the black RowView_render laid.
void StackView_render(SDL_Surface* screen, int lastScreen);
// The D-pad only (Stack_navigate): UP/DOWN step and stop at the ends; UP on the first item (or an empty tab) focuses
// the tab row on a fresh press; LEFT/RIGHT switch tab on a fresh press. In a game list UP on the first item and
// LEFT/RIGHT do nothing. True when it handled a D-pad key.
bool StackView_handleInput(bool* dirty);
// UP on the tab row: the selection goes to the last item, the stack snapped there.
void StackView_focusBottom(void);
// The slide is running (one settled frame after it).
bool StackView_animating(void);
// RowView_prefetchStep for the stack: builds ahead, until `deadline`, what the last frame's stack needs next. True
// while more remains.
bool StackView_prefetchStep(Uint32 deadline);
// Forget the list the position belongs to: the next frame snaps (the row drew instead, or nothing did).
void StackView_forget(void);

#endif // STACKVIEW_H
