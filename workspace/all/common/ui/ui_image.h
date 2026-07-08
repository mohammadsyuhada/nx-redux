#ifndef UI_IMAGE_H
#define UI_IMAGE_H

#include "sdl.h"

// Fit img_w x img_h into max_w x max_h preserving aspect ratio.
void UI_calcImageFit(int img_w, int img_h, int max_w, int max_h,
					 int* out_w, int* out_h);

// Convert surface to the screen's pixel format, freeing the original on
// success. Returns the converted surface (or the original on failure).
SDL_Surface* UI_convertSurface(SDL_Surface* surface, SDL_Surface* screen);

#endif // UI_IMAGE_H
