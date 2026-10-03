// The Consoles tab's controller art: the pads' images, loaded through MenuArt_load and kept in caches of their own.

#include "controller_art.h"
#include "controller_art_model.h"
#include "menuart.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PAD_SLOTS 6 // the Carousel: the selection, its neighbours and a few behind (a held D-pad's)

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

SDL_Surface* ControllerArt_carousel(const char* id, int box_w, int box_h) {
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
	for (int i = 0; i < PAD_SLOTS; i++) { // a free slot, else the least recently used
		if (!slots[i].used) {
			slot = &slots[i];
			break;
		}
		if (slots[i].stamp < slot->stamp)
			slot = &slots[i];
	}
	if (slot->surface)
		SDL_FreeSurface(slot->surface);
	char file[64];
	snprintf(file, sizeof(file), "menu_pad_%s.png", id);
	snprintf(slot->id, sizeof(slot->id), "%s", id);
	slot->box_w = box_w, slot->box_h = box_h;
	slot->surface = MenuArt_load(file, box_w, box_h);
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
			SDL_FreeSurface(list_slot.surface);
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
			SDL_FreeSurface(slots[i].surface);
	memset(slots, 0, sizeof(slots));
	next_stamp = 0;
	if (list_slot.surface)
		SDL_FreeSurface(list_slot.surface);
	memset(&list_slot, 0, sizeof(list_slot));
}
