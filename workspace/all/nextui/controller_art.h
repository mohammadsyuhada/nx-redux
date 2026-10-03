#ifndef CONTROLLER_ART_H
#define CONTROLLER_ART_H
// The Consoles tab's controller art (docs/controller-art.md; ids, boxes and opacity in controller_art_model.h). The
// images are big, so they keep their own small caches rather than MenuArt's slots (which hold every logo).
#include "sdl.h"

// The Carousel pad of console `id` (Pad_idForFolder) fitted in box_w x box_h; NULL when missing. Cached (the last
// few id+box pairs); do not free. Shared: restore any alpha mod after blitting it.
SDL_Surface* ControllerArt_carousel(const char* id, int box_w, int box_h);
// The List pad of console `id` for this screen, already placed: its top-left in *x, *y. NULL when there is no image
// for this screen size. One cached (the selected console's); do not free.
SDL_Surface* ControllerArt_list(const char* id, int screen_w, int screen_h, int* x, int* y);
void ControllerArt_quit(void); // free the caches
#endif
