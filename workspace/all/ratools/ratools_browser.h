#ifndef __RATOOLS_BROWSER_H__
#define __RATOOLS_BROWSER_H__

#include "sdl.h"

/** Modal achievements browser: game list -> per-game achievement list.
 *  Blocks until the user backs out. */
void RATBrowser_run(SDL_Surface* screen);

#endif
