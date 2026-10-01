#ifndef UI_FADE_H
#define UI_FADE_H

#include <stdbool.h>
#include "sdl.h"

// Cached black ARGB8888 gradient surfaces (blend mode BLEND) for scrims and fades.
// The cache owns every surface: callers never free them. 6 LRU slots shared by both kinds.

// Per-row alpha UI_easedFadeAlpha(edge, dist / (h - 1), power), dist = rows from the dark edge.
SDL_Surface* UI_easedFadeSurface(int w, int h, float edge, float power, bool dark_at_top);

// Info band: dark at the bottom, per-row alpha UI_linearHoldFadeAlpha(edge, h - 1 - row, h - 1, hold).
SDL_Surface* UI_bandFadeSurface(int w, int h, float edge, int hold);

// Blend (part of) one of these black gradients onto an ARGB8888 dst at (x, y), inside dst's clip rect. Matches
// SDL_BlitSurface's BLEND to ±1 at a fraction of its cost (a fade's rows are uniform, so each row is one integer
// scale of the pixels under it); ignores the fade surface's colour/alpha mods and blend mode. Any other format goes
// through SDL_BlitSurface.
void UI_blitFade(SDL_Surface* fade, const SDL_Rect* src, SDL_Surface* dst, int x, int y);

// Black at a per-column alpha (alpha[i] for column x + i, w ≤ 4096) over dst's rect (x, y, w, h), inside dst's clip
// rect: exact /255, NEON, one pass; clear columns at the ends cost nothing. Several dark layers over the same pixels
// (an edge fade and a shade) combine into one alpha first: 255 − (255 − a1)(255 − a2) / 255.
void UI_darkenColumns(SDL_Surface* dst, int x, int y, int w, int h, const Uint8* alpha);

// Black at `alpha` over dst's rect r (NULL = all of dst), inside dst's clip rect: SDL's BLEND result at a fraction
// of its cost (a fill at 255).
void UI_dimRect(SDL_Surface* dst, const SDL_Rect* r, Uint8 alpha);

// The part (sx, sy, w, h) of an opaque src onto dst at (dx, dy), inside dst's clip rect: a row copy at full alpha,
// else the exact lerp toward src that a BLEND blit gives over opaque pixels (NEON). Every pixel of src (and of under,
// and of dst under the rect when under is NULL) must be opaque. under (optional, same size as src): the lerp runs
// from under's pixels instead of dst's, so a crossfade of two prepared surfaces is one pass straight into dst.
// Not ARGB8888 (or a locked surface): SDL blits.
void UI_blitOpaque(SDL_Surface* src, SDL_Surface* under, int sx, int sy, int w, int h, SDL_Surface* dst, int dx,
				   int dy, int alpha);

// Straight-alpha src (part srect, NULL = all) over an opaque dst at (dx, dy) at a global alpha, inside dst's clip
// rect: every pixel lerps toward src by A·alpha/255 (exact /255, NEON; clear pixels skipped, opaque ones copied) and
// stays opaque. SDL's BLEND result over opaque pixels at a fraction of its cost; src's colour mod is ignored. Not
// ARGB8888 (or a locked surface): an SDL blit at that alpha mod.
void UI_blitBlendOpaque(SDL_Surface* src, const SDL_Rect* srect, SDL_Surface* dst, int dx, int dy, int alpha);

// Free every cached surface (call on quit).
void UI_fadeCacheClear(void);

#endif // UI_FADE_H
