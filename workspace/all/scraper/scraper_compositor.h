#ifndef SCRAPER_COMPOSITOR_H
#define SCRAPER_COMPOSITOR_H

#include <stdbool.h>
#include "sdl.h"

// Load a single image, scale it to FIT within 640x480 keeping aspect ratio,
// and return a canvas sized exactly to the scaled image (no 4:3 padding, no
// shadow, no transparent bars): the screenshot and box art the scraper saves. Returns NULL on failure (caller must free on success).
SDL_Surface* Compositor_createSingle(const char* image_path);

// Save a surface as a 256-colour (indexed) PNG to the given path, same size
// (falls back to a 32-bit PNG if that fails). Creates parent directories if
// needed. Returns true on success
bool Compositor_savePNG(SDL_Surface* surface, const char* path);

#endif // SCRAPER_COMPOSITOR_H
