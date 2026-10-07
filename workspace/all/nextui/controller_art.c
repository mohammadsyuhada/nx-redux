// The Consoles tab's controller art: the pads' images, loaded through MenuArt_load and kept in caches of their own.

#include "artloader.h"
#include "controller_art.h"
#include "controller_art_model.h"
#include "menuart.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "api.h"

#define PAD_SLOTS 16 // the Carousel: the selection, its neighbours and those behind (a held D-pad's)

typedef struct {
	char id[16];
	int box_w, box_h;
	SDL_Surface* surface; // NULL = a remembered failed load
	unsigned int stamp;
	bool used;
} PadSlot;

static PadSlot slots[PAD_SLOTS];
static unsigned int next_stamp = 0;
// The List's pads (box_w x box_h = the screen): the selection's and the neighbours a held D-pad reaches next
#define LIST_SLOTS 6
static PadSlot list_slots[LIST_SLOTS];
static int list_x, list_y;
static bool list_pending = false;

static bool last_pending = false; // the last ControllerArt_carousel's NULL was "still decoding"

bool ControllerArt_pending(void) {
	return last_pending;
}

SDL_Surface* ControllerArt_carousel(const char* id, int box_w, int box_h) {
	last_pending = false;
	if (!id || !id[0] || strlen(id) >= sizeof(slots[0].id) || box_w < 1 || box_h < 1)
		return NULL;
	PadSlot* slot = &slots[0];
	for (int i = 0; i < PAD_SLOTS; i++) {
		PadSlot* s = &slots[i];
		if (s->used && s->box_w == box_w && s->box_h == box_h && strcmp(s->id, id) == 0) {
			s->stamp = ++next_stamp;
			return s->surface;
		}
	}
	// never decoded here: the art loader's thread does it (a slide must not wait on a PNG). Not ready yet: ask for it
	// and draw no pad this frame
	char file[64];
	snprintf(file, sizeof(file), "menu_pad_%s.png", id);
	bool failed = false;
	SDL_Surface* ready = ArtLoader_take(file, box_w, box_h, &failed);
	if (!ready && !failed) {
		ArtLoader_request(file, box_w, box_h, 0);
		last_pending = true;
		return NULL;
	}
	for (int i = 0; i < PAD_SLOTS; i++) { // a free slot, else the least recently used
		if (!slots[i].used) {
			slot = &slots[i];
			break;
		}
		if (slots[i].stamp < slot->stamp)
			slot = &slots[i];
	}
	if (slot->surface)
		GFX_freeSurfaceAndTexture(slot->surface);
	snprintf(slot->id, sizeof(slot->id), "%s", id);
	slot->box_w = box_w, slot->box_h = box_h;
	slot->surface = ready; // NULL: a remembered failed load
	slot->stamp = ++next_stamp;
	slot->used = true;
	return slot->surface;
}

// The cached List pad of id for this screen size (*hit false: not cached; a cached NULL is a remembered failed load)
static PadSlot* listFind(const char* id, int screen_w, int screen_h) {
	for (int i = 0; i < LIST_SLOTS; i++) {
		PadSlot* s = &list_slots[i];
		if (s->used && s->box_w == screen_w && s->box_h == screen_h && strcmp(s->id, id) == 0) {
			s->stamp = ++next_stamp;
			return s;
		}
	}
	return NULL;
}

static void listStore(const char* id, int screen_w, int screen_h, SDL_Surface* surface) {
	PadSlot* slot = &list_slots[0];
	for (int i = 0; i < LIST_SLOTS; i++) { // a free slot, else the least recently used
		if (!list_slots[i].used) {
			slot = &list_slots[i];
			break;
		}
		if (list_slots[i].stamp < slot->stamp)
			slot = &list_slots[i];
	}
	if (slot->surface)
		GFX_freeSurfaceAndTexture(slot->surface);
	snprintf(slot->id, sizeof(slot->id), "%s", id);
	slot->box_w = screen_w, slot->box_h = screen_h;
	slot->surface = surface;
	slot->stamp = ++next_stamp;
	slot->used = true;
}

// Cached, or taken from the art loader once decoded (asked for at prio while not). *pending: still decoding.
static SDL_Surface* listGet(const char* id, int screen_w, int screen_h, int prio, bool* pending) {
	*pending = false;
	const char* suffix;
	int x, y;
	if (!id || !id[0] || strlen(id) >= sizeof(list_slots[0].id) ||
		!Pad_listPos(Pad_row(id), screen_w, screen_h, &suffix, &x, &y))
		return NULL;
	PadSlot* hit = listFind(id, screen_w, screen_h);
	if (hit)
		return hit->surface;
	char file[64];
	snprintf(file, sizeof(file), "menu_pad_%s_list_%s.png", id, suffix);
	bool failed = false;
	SDL_Surface* ready = ArtLoader_take(file, 0, 0, &failed); // baked for this screen: at its own size
	if (ready || failed) {
		listStore(id, screen_w, screen_h, ready);
		return ready;
	}
	ArtLoader_request(file, 0, 0, prio);
	*pending = true;
	return NULL;
}

SDL_Surface* ControllerArt_list(const char* id, int screen_w, int screen_h, int* x, int* y) {
	const char* suffix;
	if (!Pad_listPos(Pad_row(id), screen_w, screen_h, &suffix, &list_x, &list_y)) {
		list_pending = false;
		return NULL;
	}
	*x = list_x, *y = list_y;
	return listGet(id, screen_w, screen_h, 0, &list_pending);
}

bool ControllerArt_listPending(void) {
	return list_pending;
}

SDL_Surface* ControllerArt_listPrefetch(const char* id, int screen_w, int screen_h, int prio, bool* pending) {
	return listGet(id, screen_w, screen_h, prio, pending);
}

void ControllerArt_quit(void) {
	for (int i = 0; i < PAD_SLOTS; i++)
		if (slots[i].surface)
			GFX_freeSurfaceAndTexture(slots[i].surface);
	memset(slots, 0, sizeof(slots));
	next_stamp = 0;
	for (int i = 0; i < LIST_SLOTS; i++)
		if (list_slots[i].surface)
			GFX_freeSurfaceAndTexture(list_slots[i].surface);
	memset(list_slots, 0, sizeof(list_slots));
	list_pending = false;
}
