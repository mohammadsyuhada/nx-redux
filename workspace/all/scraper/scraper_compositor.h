#ifndef SCRAPER_COMPOSITOR_H
#define SCRAPER_COMPOSITOR_H

#include <stdbool.h>
#include "sdl.h"

// Composite artwork from individual images into a single thumbnail
// Any image path can be NULL if that layer is unavailable
// Returns the composited surface (caller must free), or NULL on failure
SDL_Surface* Compositor_create(const char* screenshot_path,
							   const char* boxart_path,
							   const char* wheel_path);

// Save a surface as PNG to the given path
// Creates parent directories if needed
// Returns true on success
bool Compositor_savePNG(SDL_Surface* surface, const char* path);

#endif // SCRAPER_COMPOSITOR_H
