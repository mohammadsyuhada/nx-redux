#ifndef __BUTTON_LAYOUT_H__
#define __BUTTON_LAYOUT_H__

// Pure logic behind the "Button layout" (Nintendo / Xbox) and "Hint labels"
// settings. Header-only and SDL-free so it can be unit-tested on the host
// (tests/test_button_layout.c). api.c bridges BTN_ID_* to these faces.
//
// Physical layout on every supported device is Nintendo: A right, B bottom,
// X top, Y left. "Xbox" means the bottom button becomes A and the right one
// B (and X/Y swap the same way), so a user who thinks in Xbox terms gets
// confirm at the bottom everywhere.

#include <stddef.h>
#include <string.h>

typedef enum {
	BL_FACE_NONE = 0,
	BL_FACE_A,
	BL_FACE_B,
	BL_FACE_X,
	BL_FACE_Y,
} ButtonLayoutFace;

// Logical face a raw press should become. Identity unless xbox is set.
static inline ButtonLayoutFace ButtonLayout_swapFace(ButtonLayoutFace f, int xbox) {
	if (!xbox)
		return f;
	switch (f) {
	case BL_FACE_A:
		return BL_FACE_B;
	case BL_FACE_B:
		return BL_FACE_A;
	case BL_FACE_X:
		return BL_FACE_Y;
	case BL_FACE_Y:
		return BL_FACE_X;
	default:
		return f;
	}
}

// Label to DISPLAY for a logical hint/binding label. With "physical" the
// letter printed on the cap you actually press is shown (Xbox layout on,
// Hint labels = printed caps): the four face letters swap, their MENU+
// chord forms too. Any other label, NULL, or physical == 0 returns the
// input pointer unchanged (callers compare pointers, so never copy).
static inline const char* ButtonLayout_displayLabel(const char* logical, int physical) {
	if (!physical || !logical)
		return logical;
	static const char* const pairs[][2] = {
		{"A", "B"},
		{"B", "A"},
		{"X", "Y"},
		{"Y", "X"},
		{"MENU+A", "MENU+B"},
		{"MENU+B", "MENU+A"},
		{"MENU+X", "MENU+Y"},
		{"MENU+Y", "MENU+X"},
	};
	for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
		if (strcmp(logical, pairs[i][0]) == 0)
			return pairs[i][1];
	}
	return logical;
}

#endif
