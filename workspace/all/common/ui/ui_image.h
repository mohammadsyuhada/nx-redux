#ifndef UI_IMAGE_H
#define UI_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdl.h"

// Fit img_w x img_h into max_w x max_h preserving aspect ratio.
void UI_calcImageFit(int img_w, int img_h, int max_w, int max_h,
					 int* out_w, int* out_h);

// Convert surface to the screen's pixel format, freeing the original on
// success. Returns the converted surface (or the original on failure).
SDL_Surface* UI_convertSurface(SDL_Surface* surface, SDL_Surface* screen);

// True when data looks like a fully-downloaded image (JPEG FFD8..FFD9, PNG
// ..IEND); unknown formats are assumed complete.
bool UI_imageDataComplete(const uint8_t* data, size_t size);

// Read + validate + decode an image file (<= max_bytes). Deletes the file
// and returns NULL when it is truncated or undecodable, so callers re-fetch
// it. Returns the raw decoded surface, unscaled; caller frees.
SDL_Surface* UI_loadValidatedImage(const char* path, size_t max_bytes);

// Convert+scale `raw` (not freed) to a size x size ARGB8888 surface with a
// rounded-corner mask applied (radius px, clamped to size/2; 0 = square).
// Caller frees the result.
SDL_Surface* UI_roundedFromSurface(SDL_Surface* raw, int size, int radius);

// Convert+scale `raw` (not freed) to a size x size ARGB8888 surface with a
// circular mask applied. Caller frees the result.
SDL_Surface* UI_circleFromSurface(SDL_Surface* raw, int size);

// A rich row's round thumbnail: `raw` (not freed) centre-cropped to a square (aspect kept), scaled to size x size
// over an opaque `backdrop` disc (so art with transparent areas still fills the circle), then an anti-aliased
// circular mask on that final surface. ARGB8888; caller frees.
SDL_Surface* UI_circleThumbFromSurface(SDL_Surface* raw, int size, SDL_Color backdrop);

// UI_circleThumbFromSurface on an image file (NULL if it is missing or unreadable).
SDL_Surface* UI_loadCircleThumb(const char* path, int size, SDL_Color backdrop);

// Load an image, scale it to a size x size square, and round its corners
// (radius px, clamped to size/2; 0 = square). ARGB8888; caller frees.
// Returns NULL if the file is missing or unreadable.
SDL_Surface* UI_loadRoundedImage(const char* path, int size, int radius);

#endif // UI_IMAGE_H
