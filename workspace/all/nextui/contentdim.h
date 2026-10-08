#ifndef CONTENTDIM_H
#define CONTENTDIM_H

#include "sdl.h"
#include <stdbool.h>

// The tab-focus dim (LIST-LAYOUT §5): the whole content below the tab row drawn as one layer at
// MenuTabs_contentAlpha() over the page under it (the page background, the hint bar), not just its selection.
//
// A renderer draws its content between ContentDim_begin and ContentDim_end, onto the screen as usual. While the
// content is lit (alpha 1: the tab row has no focus, or off the main menu) both are no-ops and cost nothing. Otherwise
// begin snapshots `area` of the screen and end mixes the content back toward that snapshot (snapshot + alpha ·
// (content − snapshot), per premultiplied channel, via UI_blitOpaque): one copy and one pass over the area, only on
// frames that draw. The host redraws only while something moves, so a settled, focused menu draws nothing at all. The
// snapshot is freed on the first lit frame after the dim settles back.
//
// While layered, the content must draw in software onto the screen: no GPU layer (the List's band) and no marquee
// (it presents its own frames, lit). ContentDim_layered tells the renderers.
void ContentDim_begin(SDL_Surface* screen, SDL_Rect area);
// ContentDim_begin for an area the caller knows is clear (0,0,0,0) under its content, across the screen's width, and
// that nothing drawn after ContentDim_end overlaps: the GPU draws those rows of the screen at the dim's opacity
// (PLAT_setScreenDim; with the clear snapshot, the same content · alpha) instead of the snapshot and the pass over the
// area, and the screen keeps the content lit. Not layered (no ContentDim_layered). In software as ContentDim_begin when
// the screen can't be dimmed that way.
void ContentDim_beginClear(SDL_Surface* screen, SDL_Rect area);
void ContentDim_end(SDL_Surface* screen);
bool ContentDim_layered(void);
// The content is dimmed now (begin would layer it): the same test begin makes, for drawing done outside begin/end.
bool ContentDim_dimmed(void);
void ContentDim_quit(void); // free the snapshot

#endif
