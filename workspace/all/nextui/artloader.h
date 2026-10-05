#ifndef ARTLOADER_H
#define ARTLOADER_H

#include "sdl.h"
#include <stdbool.h>

// Background decode of bundled menu art (MenuArt_load: res/menu/<file> fitted to box_w x box_h), so a Carousel slide
// never waits on a PNG decode and its area-average scale. One worker thread; the main thread asks ahead for what the
// row will draw (nearest first: the lower prio, the sooner) and takes each surface once it is ready.

// Queue file at box_w x box_h (no-op when it is already queued, decoding or ready; a lower prio moves it up). A 0 x 0
// box asks for MenuArt_loadHalf's half-size image instead.
void ArtLoader_request(const char* file, int box_w, int box_h, int prio);
// The decoded surface, handed over (the caller owns it from here), or NULL when it is not ready (queued, decoding, or
// never asked for). A failed decode is reported as ready with *failed set (NULL surface), so it is not asked again.
SDL_Surface* ArtLoader_take(const char* file, int box_w, int box_h, bool* failed);
// Drop every queued job and every ready surface not taken yet (a decode in progress finishes and is dropped).
void ArtLoader_clear(void);
void ArtLoader_quit(void);

#endif
