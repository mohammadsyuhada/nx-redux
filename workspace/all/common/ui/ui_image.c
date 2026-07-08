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
