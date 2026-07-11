#ifndef __MODULE_LIBRARY_H__
#define __MODULE_LIBRARY_H__

#include <SDL2/SDL.h>
#include "module_common.h"

// Run the library submenu (Files, Playlists)
ModuleExitReason LibraryModule_run(SDL_Surface* screen);

#endif
