#ifndef HOMEART_H
#define HOMEART_H

#include "sdl.h"
#include "types.h"
#include <stdbool.h>

// Home tab and Backdrop pictures, loaded off the UI thread into small LRU caches keyed by kind, paths and size:
// 24 entries for Continue/pin pictures, 24 for box arts, 3 for full-screen Backdrop pictures.
// A miss is cached as HOMEART_NONE so a missing picture is not retried every frame. Returned surfaces are ARGB8888,
// owned by the cache, and stay valid until a later HomeArt_* call evicts them: use them within the frame.

typedef enum { HOMEART_NONE,
			   HOMEART_LOADING,
			   HOMEART_READY } HomeArtState;

// Continue's picture: resume frame (trimmed) → screenshot → none. Cropped to fill w×h px, framed at 40% from the
// top, zoomed 1.15x about that line, corners rounded to radius_px. Async; the returned surface is owned by the cache.
HomeArtState HomeArt_continue(const char* rom_path, const char* preview_path, int w, int h, int radius_px,
							  SDL_Surface** out);
// A pin's screenshot, cropped to fill w×h, corners rounded. Same ownership.
HomeArtState HomeArt_pin(const char* rom_path, int w, int h, int radius_px, SDL_Surface** out);
// Box art (.media/boxart) fitted (contain, aspect kept) into w×h px with its soft shadow baked in. The returned surface
// is the fitted art plus the same padding `pad` on every side (room for the 4 dp offset and the 8 dp blur), and
// *ox = *oy = pad is where the art's own top-left sits: art size = (surface->w - 2 * *ox) × (surface->h - 2 * *oy).
// NONE when the game has no box art. ox/oy may be NULL; they are 0 unless READY.
HomeArtState HomeArt_boxart(const char* rom_path, int w, int h, SDL_Surface** out, int* ox, int* oy);
// The full-screen Backdrop picture: the screenshot cropped (centred) to fill screen_w×screen_h, a 50% black dim and
// the Row_shade curve baked in per row. Opaque (blend mode NONE). NONE when the game has no screenshot.
HomeArtState HomeArt_backdrop(const char* rom_path, int screen_w, int screen_h, SDL_Surface** out);
// True once after any load finished since the last call (the caller redraws).
bool HomeArt_checkAsyncLoaded(void);
// Stop the worker and free the cache. Safe to call when nothing was loaded.
void HomeArt_quit(void);

// Continue's entry: the first ROM in recents as a fresh Entry (the caller Entry_free()s it), or NULL.
Entry* Home_continueEntry(void);

#endif // HOMEART_H
