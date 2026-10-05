// Bundled main-menu art (res/menu/*.png: console logos, tool icons), loaded once and
// pre-scaled to the box it is drawn in, so the render loop only blits.

#include "menuart.h"
#include "area_scale.h"
#include "artloader.h"

#include "defines.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "api.h"

// a surface out with its GPU texture, if it ever got one (the Carousel's sprite mode)
static void freeSurfTex(SDL_Surface* s) {
	if (!s)
		return;
	PLAT_freeSurfaceTexture(s);
	SDL_FreeSurface(s);
}

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
	freeSurfTex(raw);
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
		freeSurfTex(src);
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
	freeSurfTex(src);
	return out;
}

SDL_Surface* MenuArt_halve(const SDL_Surface* src) {
	if (!src || src->format->format != SDL_PIXELFORMAT_ARGB8888 || src->w < 2 || src->h < 2)
		return NULL;
	int w = src->w / 2, h = src->h / 2;
	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!out)
		return NULL;
	// each output pixel the alpha-weighted mean of its 2x2 block (straight alpha in and out), so the clear pixels
	// around a logo don't darken its edges
	for (int y = 0; y < h; y++) {
		const Uint32* r0 = (const Uint32*)((const Uint8*)src->pixels + (2 * y) * src->pitch);
		const Uint32* r1 = (const Uint32*)((const Uint8*)src->pixels + (2 * y + 1) * src->pitch);
		Uint32* d = (Uint32*)((Uint8*)out->pixels + y * out->pitch);
		for (int x = 0; x < w; x++) {
			Uint32 p[4] = {r0[2 * x], r0[2 * x + 1], r1[2 * x], r1[2 * x + 1]};
			Uint32 a = 0, r = 0, g = 0, b = 0;
			for (int i = 0; i < 4; i++) {
				Uint32 pa = p[i] >> 24;
				a += pa;
				r += ((p[i] >> 16) & 0xFF) * pa;
				g += ((p[i] >> 8) & 0xFF) * pa;
				b += (p[i] & 0xFF) * pa;
			}
			d[x] = a ? ((a + 2) / 4) << 24 | (r / a) << 16 | (g / a) << 8 | (b / a) : 0;
		}
	}
	SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
	return out;
}

SDL_Surface* MenuArt_loadHalf(const char* file) {
	SDL_Surface* full = MenuArt_load(file, 0, 0);
	SDL_Surface* half = MenuArt_halve(full);
	freeSurfTex(full);
	return half;
}

static SDL_Surface* store(const char* file, int box_w, int box_h, SDL_Surface* surface);

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

	// decoded ahead on the art loader's thread (a Carousel's warm-up), else here
	bool failed = false;
	SDL_Surface* ready = ArtLoader_take(file, box_w, box_h, &failed);
	return store(file, box_w, box_h, ready || failed ? ready : MenuArt_load(file, box_w, box_h));
}

SDL_Surface* MenuArt_peek(const char* file, int box_w, int box_h, bool* pending) {
	if (pending)
		*pending = false;
	if (!file || !file[0] || box_w < 1 || box_h < 1 || strlen(file) >= sizeof(slots[0].file))
		return NULL;
	for (int i = 0; i < MENUART_SLOTS; i++) {
		MenuArtSlot* s = &slots[i];
		if (s->used && s->box_w == box_w && s->box_h == box_h && strcmp(s->file, file) == 0) {
			s->stamp = ++next_stamp;
			return s->surface;
		}
	}
	bool failed = false;
	SDL_Surface* ready = ArtLoader_take(file, box_w, box_h, &failed);
	if (ready || failed)
		return store(file, box_w, box_h, ready);
	ArtLoader_request(file, box_w, box_h, 0); // the nearest there is: it is wanted on screen now
	if (pending)
		*pending = true;
	return NULL;
}

// file at box_w x box_h into the cache (NULL: a remembered failed load), in a free slot or the least recently used
static SDL_Surface* store(const char* file, int box_w, int box_h, SDL_Surface* surface) {
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
		freeSurfTex(slot->surface);

	snprintf(slot->file, sizeof(slot->file), "%s", file);
	slot->box_w = box_w;
	slot->box_h = box_h;
	slot->surface = surface;
	slot->stamp = ++next_stamp;
	slot->used = true;
	return slot->surface;
}

void MenuArt_quit(void) {
	for (int i = 0; i < MENUART_SLOTS; i++) {
		if (slots[i].surface)
			freeSurfTex(slots[i].surface);
	}
	memset(slots, 0, sizeof(slots));
	next_stamp = 0;
}
