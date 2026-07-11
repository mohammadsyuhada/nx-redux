#ifndef __RATOOLS_PREFETCH_H__
#define __RATOOLS_PREFETCH_H__

#include "sdl.h"

/** Modal library prefetch: hash every rom, download achievement data +
 *  badges into the offline cache. Blocks; B cancels between games. */
void RATPrefetch_run(SDL_Surface* screen);

#endif
