#ifndef MENUART_H
#define MENUART_H
#include "sdl.h"
// A bundled logo or icon (res/menu/<file>) scaled to fit box_w x box_h, keeping its aspect.
// Cached (one surface per file+box); do not free. NULL when the file is missing.
SDL_Surface* MenuArt_get(const char* file, int box_w, int box_h);
// The same, uncached: the caller owns (and frees) the surface. For images too large to share MenuArt's slots. A 0 x 0
// box loads the image at its own size.
SDL_Surface* MenuArt_load(const char* file, int box_w, int box_h);
void MenuArt_quit(void); // free the cache
#endif
