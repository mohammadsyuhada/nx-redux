#include <stdio.h>
#include <stdlib.h>

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

// JPEG: ends with FF D9, PNG: ends with IEND chunk
bool UI_imageDataComplete(const uint8_t* data, size_t size) {
	if (size < 4)
		return false;
	// JPEG: starts with FF D8, ends with FF D9
	if (data[0] == 0xFF && data[1] == 0xD8) {
		return (data[size - 2] == 0xFF && data[size - 1] == 0xD9);
	}
	// PNG: starts with 89 50 4E 47, ends with IEND chunk (AE 42 60 82)
	if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
		return (size >= 8 &&
				data[size - 4] == 0xAE && data[size - 3] == 0x42 &&
				data[size - 2] == 0x60 && data[size - 1] == 0x82);
	}
	// Unknown format — assume complete
	return true;
}

SDL_Surface* UI_loadValidatedImage(const char* path, size_t max_bytes) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize <= 0 || (size_t)fsize > max_bytes) {
		fclose(f);
		return NULL;
	}

	uint8_t* data = (uint8_t*)malloc(fsize);
	if (!data) {
		fclose(f);
		return NULL;
	}
	if ((long)fread(data, 1, fsize, f) != fsize) {
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);

	if (!UI_imageDataComplete(data, fsize)) {
		free(data);
		remove(path); // Corrupt/incomplete — delete so it gets re-fetched
		return NULL;
	}

	SDL_RWops* rw = SDL_RWFromConstMem(data, fsize);
	SDL_Surface* raw = NULL;
	if (rw)
		raw = IMG_Load_RW(rw, 1);
	free(data);
	if (!raw) {
		remove(path);
		return NULL;
	}

	return raw;
}

// Convert+scale raw to size x size ARGB8888, then punch out either a
// circular mask or a per-corner rounded-rect mask.
static SDL_Surface* maskedFromSurface(SDL_Surface* raw, int size, int radius, bool circle) {
	if (!raw)
		return NULL;

	SDL_Surface* converted = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
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

	uint32_t* pixels = (uint32_t*)scaled->pixels;
	int pitch = scaled->pitch / 4;

	if (circle) {
		int r = size / 2;
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				int dx = x - r;
				int dy = y - r;
				if (dx * dx + dy * dy > r * r) {
					pixels[y * pitch + x] = 0; // Fully transparent
				}
			}
		}
	} else if (radius > 0) {
		if (radius > size / 2)
			radius = size / 2;
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
					pixels[py * pitch + px] = 0; // fully transparent
				}
			}
		}
	}

	return scaled;
}

SDL_Surface* UI_roundedFromSurface(SDL_Surface* raw, int size, int radius) {
	return maskedFromSurface(raw, size, radius, false);
}

SDL_Surface* UI_circleFromSurface(SDL_Surface* raw, int size) {
	return maskedFromSurface(raw, size, 0, true);
}

SDL_Surface* UI_circleThumbFromSurface(SDL_Surface* raw, int size, SDL_Color backdrop) {
	if (!raw || size <= 0)
		return NULL;
	SDL_Surface* converted = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	if (!converted)
		return NULL;
	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!out) {
		SDL_FreeSurface(converted);
		return NULL;
	}

	// The opaque disc first, so art with transparent areas (a "mix": screenshot + box) still reads as a whole
	// circle; then the art centre-cropped to a square (aspect kept) and scaled over it.
	SDL_FillRect(out, NULL, SDL_MapRGBA(out->format, backdrop.r, backdrop.g, backdrop.b, 255));
	int side = converted->w < converted->h ? converted->w : converted->h;
	SDL_Rect src = {(converted->w - side) / 2, (converted->h - side) / 2, side, side};
	SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_BLEND);
	SDL_BlitScaled(converted, &src, out, &(SDL_Rect){0, 0, size, size});
	SDL_FreeSurface(converted);

	// Anti-aliased circular mask on the final surface: each edge pixel's alpha is its coverage, from a 4 x 4
	// sample grid (no libm). Coordinates are in quarter pixels; the circle fills the square.
	if (SDL_MUSTLOCK(out))
		SDL_LockSurface(out);
	uint32_t* pixels = (uint32_t*)out->pixels;
	int pitch = out->pitch / 4;
	long long c = 2LL * size;									 // centre and radius, quarter px
	long long in2 = (c - 3) * (c - 3), out2 = (c + 3) * (c + 3); // a pixel reaches < 3 quarter px from its centre
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			long long dx = 4LL * x + 2 - c, dy = 4LL * y + 2 - c;
			long long d2 = dx * dx + dy * dy;
			if (d2 <= in2)
				continue; // fully inside
			if (d2 >= out2) {
				pixels[y * pitch + x] = 0;
				continue;
			}
			// samples at quarter-pixel centres (doubled coordinates keep them integral)
			int cover = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++) {
					long long ex = 2 * (4LL * x + sx - c) + 1, ey = 2 * (4LL * y + sy - c) + 1;
					if (ex * ex + ey * ey <= 4 * c * c)
						cover++;
				}
			uint32_t p = pixels[y * pitch + x];
			uint32_t a = (p >> 24) * (uint32_t)cover / 16;
			pixels[y * pitch + x] = (p & 0x00FFFFFF) | (a << 24);
		}
	}
	if (SDL_MUSTLOCK(out))
		SDL_UnlockSurface(out);
	SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
	return out;
}

SDL_Surface* UI_loadCircleThumb(const char* path, int size, SDL_Color backdrop) {
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return NULL;
	SDL_Surface* result = UI_circleThumbFromSurface(raw, size, backdrop);
	SDL_FreeSurface(raw);
	return result;
}

SDL_Surface* UI_loadRoundedImage(const char* path, int size, int radius) {
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return NULL;

	SDL_Surface* result = UI_roundedFromSurface(raw, size, radius);
	SDL_FreeSurface(raw);
	return result;
}
