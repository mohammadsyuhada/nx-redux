// placeholder_art.h — an abstract picture for a game without a screenshot. SDL-free.
//
// Seeded by the game's name, so each game gets its own and keeps it: a muted two-hue diagonal gradient, two soft
// glows, faint diagonal lines and a vignette. Dark enough that the white captions drawn over it read.
#ifndef PLACEHOLDER_ART_H
#define PLACEHOLDER_ART_H

#include <stdint.h>

// Fill w×h opaque ARGB8888 pixels (`pitch` pixels per row) for `seed` (any string; NULL = "").
void PlaceholderArt_render(uint32_t* px, int w, int h, int pitch, const char* seed);

#endif
