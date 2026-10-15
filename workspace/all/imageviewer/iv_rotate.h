#ifndef IV_ROTATE_H
#define IV_ROTATE_H
#include <stdbool.h>
#include "sdl.h"

// New surface holding `src` turned `turns` quarter turns clockwise (1..3), in
// the same pixel format. NULL on allocation failure or a depth other than
// 2/3/4 bytes per pixel.
SDL_Surface* IvRotate_surface(SDL_Surface* src, int turns);

// Rewrite the image file at `path` turned `turns` quarter turns clockwise.
// PNG goes through SDL (lossless; the device ffmpeg has no PNG codec), every
// other format through ffmpeg's transpose filter (JPG is re-encoded at the
// screenshot tool's quality). The result is written to a hidden temp file
// beside the original and renamed over it, so a failure leaves the file as
// it was. Returns false on any failure.
bool IvRotate_file(const char* path, int turns);
#endif
