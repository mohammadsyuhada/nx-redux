#ifndef CONTROLLER_ART_H
#define CONTROLLER_ART_H
// The Consoles tab's controller art (docs/controller-art.md; ids, boxes and opacity in controller_art_model.h). The
// images are big, so they keep their own small caches rather than MenuArt's slots (which hold every logo).
#include "sdl.h"

// The Carousel pad of console `id` (Pad_idForFolder) fitted in box_w x box_h; NULL when missing. Cached (the last
// few id+box pairs); do not free. Shared: restore any alpha mod after blitting it.
SDL_Surface* ControllerArt_carousel(const char* id, int box_w, int box_h);
// The last ControllerArt_carousel returned NULL because its image is still on the art loader's thread (not missing).
bool ControllerArt_pending(void);
// The List pad of console `id` for this screen, already placed: its top-left in *x, *y. NULL when there is no image
// for this screen size, or while it is still on the art loader's thread (ControllerArt_listPending): never decoded on
// the caller's thread. A few cached (the selection's and its neighbours'); do not free.
SDL_Surface* ControllerArt_list(const char* id, int screen_w, int screen_h, int* x, int* y);
// The last ControllerArt_list returned NULL because its image is still decoding (a redraw will pick it up).
bool ControllerArt_listPending(void);
// Ask for console `id`'s List pad ahead (a neighbour of the selection), nearer first by prio; a finished one goes into
// the cache and is returned. NULL with *pending set while it is still being decoded.
SDL_Surface* ControllerArt_listPrefetch(const char* id, int screen_w, int screen_h, int prio, bool* pending);
void ControllerArt_quit(void); // free the caches
#endif
