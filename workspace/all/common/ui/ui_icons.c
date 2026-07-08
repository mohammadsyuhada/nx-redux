#include "ui_icons.h"
#include "api.h"
#include "defines.h"
#include <SDL2/SDL_image.h>

#define ICON_EMPTY_PATH RES_PATH "/icon-empty.png"

static SDL_Surface* empty_icon = NULL;
static SDL_Surface* empty_icon_inv = NULL;
static bool empty_icon_loaded = false;

SDL_Surface* UI_invertIconSurface(SDL_Surface* src) {
	if (!src)
		return NULL;
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(
		0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!dst)
		return NULL;
	SDL_LockSurface(src);
	SDL_LockSurface(dst);
	Uint32* src_pixels = (Uint32*)src->pixels;
	Uint32* dst_pixels = (Uint32*)dst->pixels;
	int pixel_count = src->w * src->h;
	for (int i = 0; i < pixel_count; i++) {
		Uint8 r, g, b, a;
		SDL_GetRGBA(src_pixels[i], src->format, &r, &g, &b, &a);
		r = 255 - r;
		g = 255 - g;
		b = 255 - b;
		dst_pixels[i] = SDL_MapRGBA(dst->format, r, g, b, a);
	}
	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
	return dst;
}

void UI_loadIconPair(const char* path, SDL_Surface** original, SDL_Surface** inverted) {
	*original = IMG_Load(path);
	if (*original) {
		// Convert to RGBA32 for consistent pixel access
		SDL_Surface* converted = SDL_ConvertSurfaceFormat(*original, SDL_PIXELFORMAT_RGBA32, 0);
		if (converted) {
			SDL_FreeSurface(*original);
			*original = converted;
		}
		*inverted = UI_invertIconSurface(*original);
	} else {
		*inverted = NULL;
	}
}

void UI_freeIconPair(SDL_Surface** original, SDL_Surface** inverted) {
	if (*original) {
		SDL_FreeSurface(*original);
		*original = NULL;
	}
	if (*inverted) {
		SDL_FreeSurface(*inverted);
		*inverted = NULL;
	}
}

void UI_initEmptyIcon(void) {
	if (empty_icon_loaded)
		return;
	UI_loadIconPair(ICON_EMPTY_PATH, &empty_icon, &empty_icon_inv);
	empty_icon_loaded = true;
}

void UI_quitEmptyIcon(void) {
	UI_freeIconPair(&empty_icon, &empty_icon_inv);
	empty_icon_loaded = false;
}

SDL_Surface* Icons_getEmpty(bool selected) {
	if (!empty_icon_loaded)
		UI_initEmptyIcon();
	return selected ? empty_icon : empty_icon_inv;
}
