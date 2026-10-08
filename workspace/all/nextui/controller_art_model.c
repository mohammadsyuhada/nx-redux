// The Consoles tab's controller art: lookup and geometry. SDL-free, host-tested by
// common/tests/test_controller_art_model.c.

#include "controller_art_model.h"

#include <math.h>
#include <string.h>

#include "controller_art_table.h"
#include "menulogo.h"

// consoles drawn with another console's pad
static const struct {
	const char* id;
	const char* pad;
} reuse[] = {
	{"sgb", "sfc"},
	{"segacd", "md"},
	{"s32x", "md"},
	{"ngp", "ngpc"},
	{"c128", "c64"}, // the Competition Pro joystick
	{"plus4", "c64"},
	{"amiga", "c64"},
	{"fbn", "c64"}, // Arcade: the Competition Pro is an arcade stick
	{"dcx", "dc"},	// Dreamcast Lite
};

const PadTableRow* Pad_row(const char* id) {
	if (!id)
		return NULL;
	for (size_t i = 0; i < sizeof(pad_table) / sizeof(pad_table[0]); i++)
		if (strcmp(pad_table[i].id, id) == 0)
			return &pad_table[i];
	return NULL;
}

const char* Pad_idForFolder(const char* folder_name) {
	const char* id = MenuLogo_idForFolder(folder_name);
	if (!id)
		return NULL;
	for (size_t i = 0; i < sizeof(reuse) / sizeof(reuse[0]); i++)
		if (strcmp(reuse[i].id, id) == 0)
			id = reuse[i].pad;
	const PadTableRow* row = Pad_row(id);
	return row ? row->id : NULL;
}

PadSize Pad_fit(float aspect, float box_w, float box_h) {
	PadSize s = {0, 0};
	if (!(aspect > 0) || !(box_w > 0) || !(box_h > 0))
		return s;
	if (box_w / box_h > aspect)
		s.w = box_h * aspect, s.h = box_h;
	else
		s.w = box_w, s.h = box_w / aspect;
	return s;
}

PadSize Pad_rowBox(float h) {
	return (PadSize){3.0f * h, 1.95f * h};
}

PadSize Pad_stackBox(float h) {
	return (PadSize){2.8f * h, 1.85f * h};
}

float Pad_alpha(float d) {
	float t = 1 - fabsf(d);
	return 0.6f * (t > 0 ? t : 0);
}

bool Pad_listPos(const PadTableRow* row, int screen_w, int screen_h, const char** suffix, int* x, int* y) {
	if (!row)
		return false;
	if (screen_w == 1024 && screen_h == 768) {
		*suffix = "1024x768", *x = row->x1024, *y = row->y1024;
		return true;
	}
	if (screen_w == 1280 && screen_h == 720) {
		*suffix = "1280x720", *x = row->x1280, *y = row->y1280;
		return true;
	}
	return false;
}
