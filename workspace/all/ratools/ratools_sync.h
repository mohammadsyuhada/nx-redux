#ifndef __RATOOLS_SYNC_H__
#define __RATOOLS_SYNC_H__

#include "sdl.h"

/** Modal sync screen: replays the journal with progress, shows the result.
 *  Blocks until done + user confirms. */
void RATSync_run(SDL_Surface* screen);

#endif
