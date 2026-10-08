#ifndef SCRAPER_COMPOSITOR_H
#define SCRAPER_COMPOSITOR_H

#include <stdbool.h>
#include "sdl.h"

// The List's fitted art (nextui artbg.h): a box ARTBG_FIT_BOX_W (384) px wide
// by ARTBG_FIT_BOX_H_FRAC (75%) of the screen height, 576 on the taller
// 1024x768 screen. The mix is stored no larger than it is shown there: a 4:3
// canvas the box's width. The 2D box art and wheel are also drawn in the
// Backdrop game list, whose item slot is ~474 px wide on the 1024x768 screen,
// so they are fitted within 480x576 (downscale only): wide enough for that
// slot without upscaling and still covering the List's 384-wide box (the
// 1280x720 screen's 384x540 box takes a small downscale).
#define COMPOSITOR_MIX_W 384
#define COMPOSITOR_MIX_H 288
#define COMPOSITOR_LIST_ART_MAX_W 480
#define COMPOSITOR_LIST_ART_MAX_H 576

// The mix: screenshot inset, 3D box art bottom-left, wheel bottom-right;
// transparent background, COMPOSITOR_MIX_W x COMPOSITOR_MIX_H. Any path can be NULL when that layer is
// unavailable; NULL when all three are (or on failure). Caller frees.
SDL_Surface* Compositor_create(const char* screenshot_path,
							   const char* boxart_path,
							   const char* wheel_path);

// Build the mix (Compositor_create) and save it full colour to
// .media/mix/<name>.png next to `out_png` (the .media/<name>.png base path).
// True when saved.
bool Compositor_saveMix(const char* screenshot_path, const char* boxart_path,
						const char* wheel_path, const char* out_png);

// Load a single image, scale it to FIT within 640x480 keeping aspect ratio,
// and return a canvas sized exactly to the scaled image (no 4:3 padding, no
// shadow, no transparent bars): the screenshot and 3D box art the scraper
// saves. Returns NULL on failure (caller must free on success).
SDL_Surface* Compositor_createSingle(const char* image_path);

// Load a single image as ARGB8888, shrunk with an area filter to fit within
// max_w x max_h when it is larger (never grown): the full-colour 2D box art
// and wheel. Returns NULL on failure (caller must free on success).
SDL_Surface* Compositor_createUpTo(const char* image_path, int max_w, int max_h);

// Save a surface as a 256-colour (indexed) PNG to the given path, same size
// (falls back to a 32-bit PNG if that fails). Creates parent directories if
// needed. Returns true on success
bool Compositor_savePNG(SDL_Surface* surface, const char* path);

// Save a surface as a full-colour 32-bit RGBA PNG (no palette), same size,
// via a .tmp file renamed into place. Creates parent directories if needed.
// Returns true on success
bool Compositor_savePNGRGBA(SDL_Surface* surface, const char* path);

#endif // SCRAPER_COMPOSITOR_H
