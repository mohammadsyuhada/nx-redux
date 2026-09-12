#ifndef ARTBG_H
#define ARTBG_H

#include <SDL2/SDL.h>

// Compose game art into a right-aligned, full-height "background" surface whose
// left side fades diagonally into transparency so the list can sit over it.
//
// Pure function: no globals, no GFX calls, no api.h/config.h — safe to call
// from the thumbnail worker thread. `pixel_format` is the caller's screen
// format (the worker's cachedScreenFormat). `art` is left untouched; its real
// alpha is kept straight (not premultiplied) and multiplied by the fade.
//
// Returns a NEW surface the caller owns, sized (screen_w - ArtBg_originX) x
// screen_h, representing screen columns ArtBg_originX(screen_w)..screen_w-1
// (everything further left is fully transparent by construction, so it is not
// materialised). Returns NULL on failure.
// The geometry is fixed (see the tunables in artbg.c): the diagonal runs from
// 40% in from the right edge at the top row to 60% in at the bottom (30% and
// 40% on a wide 16:9 panel), and the
// art is scaled to cover that strip plus a 30% overflow off the right edge,
// which brings more of a screenshot's centre into view. "Game art width" is
// deliberately NOT an input here — it sizes the thumbnail style only.
SDL_Surface* ArtBg_compose(SDL_Surface* art, int screen_w, int screen_h, Uint32 pixel_format);

// Fraction of the screen width a game title may use in the background style.
// The art is behind the list rather than beside it, so there is no reserved
// column ("Game art width" sizes the thumbnail style only) — but a title that
// ran to the very edge would sit on the brightest part of the image.
#define ART_BG_TEXT_WIDTH 0.85f

// Screen x of the leftmost column the composed surface represents, i.e. where
// the caller blits it. Depends only on the screen size (a wide 16:9 panel uses
// a narrower strip), so the renderer can recompute it without keeping any
// state from ArtBg_compose.
int ArtBg_originX(int screen_w, int screen_h);

#endif // ARTBG_H
