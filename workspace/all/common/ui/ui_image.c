#include "ui_image.h"

void UI_calcImageFit(int img_w, int img_h, int max_w, int max_h,
					 int* out_w, int* out_h) {
	double aspect_ratio = (double)img_h / img_w;
	int new_w = max_w;
	int new_h = (int)(new_w * aspect_ratio);

	if (new_h > max_h) {
		new_h = max_h;
		new_w = (int)(new_h / aspect_ratio);
	}

	*out_w = new_w;
	*out_h = new_h;
}

SDL_Surface* UI_convertSurface(SDL_Surface* surface, SDL_Surface* screen) {
	SDL_Surface* converted =
		SDL_ConvertSurfaceFormat(surface, screen->format->format, 0);
	if (converted) {
		SDL_FreeSurface(surface);
		return converted;
	}
	return surface;
}

SDL_Surface* UI_loadRoundedImage(const char* path, int size, int radius) {
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return NULL;

	SDL_Surface* converted = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!converted)
		return NULL;

	SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!scaled) {
		SDL_FreeSurface(converted);
		return NULL;
	}
	SDL_Rect src = {0, 0, converted->w, converted->h};
	SDL_Rect dst = {0, 0, size, size};
	SDL_BlitScaled(converted, &src, scaled, &dst);
	SDL_FreeSurface(converted);

	// Rounded-corner alpha mask (same approach as the podcast thumbnails)
	if (radius > 0) {
		if (radius > size / 2)
			radius = size / 2;
		uint32_t* pixels = (uint32_t*)scaled->pixels;
		int pitch_px = scaled->pitch / 4;
		for (int py = 0; py < size; py++) {
			for (int px = 0; px < size; px++) {
				int cx = -1, cy = -1;
				if (px < radius && py < radius) {
					cx = radius;
					cy = radius;
				} else if (px >= size - radius && py < radius) {
					cx = size - 1 - radius;
					cy = radius;
				} else if (px < radius && py >= size - radius) {
					cx = radius;
					cy = size - 1 - radius;
				} else if (px >= size - radius && py >= size - radius) {
					cx = size - 1 - radius;
					cy = size - 1 - radius;
				}
				if (cx >= 0 && (px - cx) * (px - cx) + (py - cy) * (py - cy) > radius * radius) {
					pixels[py * pitch_px + px] = 0; // fully transparent
				}
			}
		}
	}

	return scaled;
}
