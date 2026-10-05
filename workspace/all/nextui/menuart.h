#ifndef MENUART_H
#define MENUART_H
#include "sdl.h"
#include <stdbool.h>
// A bundled logo or icon (res/menu/<file>) scaled to fit box_w x box_h, keeping its aspect.
// Cached (one surface per file+box); do not free. NULL when the file is missing.
SDL_Surface* MenuArt_get(const char* file, int box_w, int box_h);
// The same, uncached: the caller owns (and frees) the surface. For images too large to share MenuArt's slots. A 0 x 0
// box loads the image at its own size.
SDL_Surface* MenuArt_load(const char* file, int box_w, int box_h);
// MenuArt_get without the decode: cached, or decoded by the art loader (artloader.h) by now, else NULL with *pending set
// (and the decode queued first in line). For draws that must not wait on a PNG (the Consoles carousel).
SDL_Surface* MenuArt_peek(const char* file, int box_w, int box_h, bool* pending);
// res/menu/<file> at half its own size (a 2x2 alpha-weighted mean, no resampling): a GPU-scaled stand-in for the
// area-averaged MenuArt_get, much cheaper to make. Uncached, the caller owns it. MenuArt_halve: the same halving of an
// ARGB8888 surface (NULL for anything else or under 2x2).
SDL_Surface* MenuArt_loadHalf(const char* file);
SDL_Surface* MenuArt_halve(const SDL_Surface* src);
void MenuArt_quit(void); // free the cache
#endif
