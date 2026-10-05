// The Consoles tab's controller art (docs/controller-art.md): which console has a pad, and where and how strongly it
// draws. SDL-free, host-tested by common/tests/test_controller_art_model.c. The looks (colour, the List's rotation and
// placement) are baked into res/menu/menu_pad_*.png by tools/controller-art/make_pads.py, which also writes the
// table of PadTableRow (controller_art_table.h).
#ifndef CONTROLLER_ART_MODEL_H
#define CONTROLLER_ART_MODEL_H

#include <stdbool.h>

#define PAD_CLEAR_DP 10.0f // the vertical stack: the selected console's pad keeps this clear of its neighbours

// A console's pad: its id, brightness factor k (baked in), the Carousel image's aspect (w/h) and the List image's
// top-left on a 1024x768 and a 1280x720 screen.
typedef struct {
	const char* id;
	float k, aspect;
	short x1024, y1024, x1280, y1280;
} PadTableRow;

// The pad id (the <id> in menu_pad_<id>.png) for a console folder name such as "Super Nintendo (SUPA)": its logo id,
// with Super Game Boy on the SNES pad, Sega CD and 32X on the Mega Drive's, Neo Geo Pocket on the Color's and the C128,
// Plus/4, Amiga and Arcade on the C64's Competition Pro. NULL for a console without hardware of its own (Pico-8, Doom,
// Ports) or without an image (the PET, the other computers).
const char* Pad_idForFolder(const char* folder_name);
const PadTableRow* Pad_row(const char* id);

typedef struct {
	float w, h;
} PadSize;
// An image of aspect w/h fitted in box_w x box_h (keeping its aspect).
PadSize Pad_fit(float aspect, float box_w, float box_h);
// The pad's box behind a logo slot of height h: the horizontal Carousel's 3.0 h x 1.95 h, the vertical's 2.8 h x 1.85 h.
PadSize Pad_rowBox(float h);
PadSize Pad_stackBox(float h);
// The pad's opacity at distance d from the selection: 0.6 at the focused item, gone one step away.
float Pad_alpha(float d);

// The List's pad for a screen_w x screen_h screen: the baked image's suffix ("1024x768", "1280x720") and its top-left.
// False for any other screen (no image baked for it).
bool Pad_listPos(const PadTableRow* row, int screen_w, int screen_h, const char** suffix, int* x, int* y);

#endif
