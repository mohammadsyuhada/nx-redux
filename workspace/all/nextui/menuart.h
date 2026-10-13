#ifndef MENUART_H
#define MENUART_H
#include "sdl.h"
#include <stdbool.h>
// A bundled logo or icon (res/menu/<file>; a file starting with '/' is that path: a user's console logo) scaled to fit
// box_w x box_h, keeping its aspect.
// Cached (one surface per file+box); do not free. NULL when the file is missing (tried again after a few seconds).
SDL_Surface* MenuArt_get(const char* file, int box_w, int box_h);
// The same, uncached: the caller owns (and frees) the surface. For images too large to share MenuArt's slots. A 0 x 0
// box loads the image at its own size.
SDL_Surface* MenuArt_load(const char* file, int box_w, int box_h);
// MenuArt_get without the decode: cached, or decoded by the art loader (artloader.h) by now, else NULL with *pending set
// (and the decode queued first in line). For draws that must not wait on a PNG (the Consoles carousel).
SDL_Surface* MenuArt_peek(const char* file, int box_w, int box_h, bool* pending);
// file at box_w x box_h is already in MenuArt_get's cache (decoded, or a remembered failed load): a warm-up skips asking
// the art loader for it (MenuArt_get would never take that result). No decode, no LRU touch.
bool MenuArt_has(const char* file, int box_w, int box_h);
// res/menu/<file> at its own size, as the GPU's level: the console logos ship pre-baked at the largest size any view
// draws them (tools/console-logos/prebake_logos.py), so this is a GPU-scaled stand-in for the area-averaged MenuArt_get
// with no resampling at all (a user's logo is scaled down to the largest of those boxes it fits). Uncached, the caller
// owns it. MenuArt_halve: a 2x2 alpha-weighted halving of an ARGB8888 surface (NULL for anything else or under 2x2),
// for the smaller level.
SDL_Surface* MenuArt_loadLevel(const char* file);
SDL_Surface* MenuArt_halve(const SDL_Surface* src);
void MenuArt_quit(void); // free the cache
#endif
