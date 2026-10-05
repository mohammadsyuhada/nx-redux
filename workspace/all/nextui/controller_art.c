// The Consoles tab's controller art: the pads' images, loaded through MenuArt_load and kept in caches of their own.

#include "artloader.h"
#include "controller_art.h"
#include "controller_art_model.h"
#include "menuart.h"

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
static PadSlot list_slot; // box_w x box_h = the screen
static int list_x, list_y;

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
		freeSurfTex(slot->surface);
	snprintf(slot->id, sizeof(slot->id), "%s", id);
	slot->box_w = box_w, slot->box_h = box_h;
	slot->surface = ready; // NULL: a remembered failed load
	slot->stamp = ++next_stamp;
	slot->used = true;
	return slot->surface;
}

SDL_Surface* ControllerArt_list(const char* id, int screen_w, int screen_h, int* x, int* y) {
	const char* suffix;
	if (!Pad_listPos(Pad_row(id), screen_w, screen_h, &suffix, &list_x, &list_y))
		return NULL;
	if (!(list_slot.used && list_slot.box_w == screen_w && list_slot.box_h == screen_h &&
		  strcmp(list_slot.id, id) == 0)) {
		if (list_slot.surface)
			freeSurfTex(list_slot.surface);
		char file[64];
		snprintf(file, sizeof(file), "menu_pad_%s_list_%s.png", id, suffix);
		snprintf(list_slot.id, sizeof(list_slot.id), "%s", id);
		list_slot.box_w = screen_w, list_slot.box_h = screen_h;
		list_slot.surface = MenuArt_load(file, 0, 0); // baked for this screen: at its own size
		list_slot.used = true;
	}
	*x = list_x, *y = list_y;
	return list_slot.surface;
}

void ControllerArt_quit(void) {
	for (int i = 0; i < PAD_SLOTS; i++)
		if (slots[i].surface)
			freeSurfTex(slots[i].surface);
	memset(slots, 0, sizeof(slots));
	next_stamp = 0;
	if (list_slot.surface)
		freeSurfTex(list_slot.surface);
	memset(&list_slot, 0, sizeof(list_slot));
}
