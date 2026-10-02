#include "scraper_compositor.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

#define CANVAS_W 640
#define CANVAS_H 480

// Scale surface to fit within max_w x max_h, maintaining aspect ratio
static SDL_Surface* scaleSurface(SDL_Surface* src, int max_w, int max_h) {
	if (!src)
		return NULL;

	double scale_x = (double)max_w / src->w;
	double scale_y = (double)max_h / src->h;
	double scale = (scale_x < scale_y) ? scale_x : scale_y;

	int new_w = (int)(src->w * scale);
	int new_h = (int)(src->h * scale);
	if (new_w < 1)
		new_w = 1;
	if (new_h < 1)
		new_h = 1;

	SDL_Surface* dst = SDL_CreateRGBSurface(0, new_w, new_h, 32,
											0x00FF0000, 0x0000FF00,
											0x000000FF, 0xFF000000);
	if (!dst)
		return NULL;

	// Disable blend mode during scaling to avoid premultiplied alpha artifacts
	SDL_BlendMode prev;
	SDL_GetSurfaceBlendMode(src, &prev);
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_BlitScaled(src, NULL, dst, NULL);
	SDL_SetSurfaceBlendMode(src, prev);
	return dst;
}

SDL_Surface* Compositor_createSingle(const char* image_path) {
	if (!image_path)
		return NULL;

	SDL_Surface* raw = IMG_Load(image_path);
	if (!raw)
		return NULL;

	// Convert to ARGB32 so scaling and PNG saving behave the same for every
	// source (paletted PNGs otherwise lose alpha).
	SDL_Surface* img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!img)
		return NULL;

	// Fit within the 4:3 bound, keep aspect; the canvas is exactly the scaled
	// size — no padding, shadow, or transparent bars.
	SDL_Surface* canvas = scaleSurface(img, CANVAS_W, CANVAS_H);
	SDL_FreeSurface(img);
	return canvas;
}

bool Compositor_savePNG(SDL_Surface* surface, const char* path) {
	if (!surface || !path)
		return false;

	// Ensure parent directory exists
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char* last_slash = strrchr(dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		mkdir_p(dir);
	}

	SDL_RWops* rw = SDL_RWFromFile(path, "wb");
	if (!rw)
		return false;

	int ret = IMG_SavePNG_RW(surface, rw, 1);
	return ret == 0;
}
