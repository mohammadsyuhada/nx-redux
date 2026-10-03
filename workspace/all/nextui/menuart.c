// Bundled main-menu art (res/menu/*.png: console logos, tool icons), loaded once and
// pre-scaled to the box it is drawn in, so the render loop only blits.

#include "menuart.h"
#include "area_scale.h"

#include "defines.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MENUART_SLOTS 40 // room for every bundled image (~37); the surfaces are small

typedef struct {
	char file[64];
	int box_w;
	int box_h;
	SDL_Surface* surface; // NULL = a remembered failed load
	unsigned int stamp;	  // last use; the lowest (least recently used) is evicted
	bool used;
} MenuArtSlot;

static MenuArtSlot slots[MENUART_SLOTS];
static unsigned int next_stamp = 0;

SDL_Surface* MenuArt_load(const char* file, int box_w, int box_h) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/menu/%s", RES_PATH, file);
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return NULL;
	SDL_Surface* src = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!src)
		return NULL;

	if (box_w <= 0 && box_h <= 0) { // its own size
		SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
		return src;
	}
	double sx = (double)box_w / src->w;
	double sy = (double)box_h / src->h;
	double scale = sx < sy ? sx : sy;
	int w = (int)(src->w * scale);
	int h = (int)(src->h * scale);
	if (w < 1 || h < 1) {
		SDL_FreeSurface(src);
		return NULL;
	}

	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (out) {
		// area-averaged, not SDL_BlitScaled's nearest pick, so the logo's anti-aliased edges survive the shrink
		if (AreaScale_argb(src->pixels, src->w, src->h, src->pitch / 4, out->pixels, w, h, out->pitch / 4) != 0) {
			SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
			SDL_BlitScaled(src, NULL, out, NULL);
		}
		SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
	}
	SDL_FreeSurface(src);
	return out;
}

SDL_Surface* MenuArt_get(const char* file, int box_w, int box_h) {
	if (!file || !file[0] || box_w < 1 || box_h < 1 || strlen(file) >= sizeof(slots[0].file))
		return NULL;

	for (int i = 0; i < MENUART_SLOTS; i++) {
		MenuArtSlot* s = &slots[i];
		if (s->used && s->box_w == box_w && s->box_h == box_h && strcmp(s->file, file) == 0) {
			s->stamp = ++next_stamp;
			return s->surface;
		}
	}

	// a free slot, else the least recently used
	MenuArtSlot* slot = &slots[0];
	for (int i = 0; i < MENUART_SLOTS; i++) {
		if (!slots[i].used) {
			slot = &slots[i];
			break;
		}
		if (slots[i].stamp < slot->stamp)
			slot = &slots[i];
	}
	if (slot->surface)
		SDL_FreeSurface(slot->surface);

	snprintf(slot->file, sizeof(slot->file), "%s", file);
	slot->box_w = box_w;
	slot->box_h = box_h;
	slot->surface = MenuArt_load(file, box_w, box_h);
	slot->stamp = ++next_stamp;
	slot->used = true;
	return slot->surface;
}

void MenuArt_quit(void) {
	for (int i = 0; i < MENUART_SLOTS; i++) {
		if (slots[i].surface)
			SDL_FreeSurface(slots[i].surface);
	}
	memset(slots, 0, sizeof(slots));
	next_stamp = 0;
}
