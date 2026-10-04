#ifndef VIEW_COMMON_H
#define VIEW_COMMON_H

// The timing, units and small entry helpers the main-menu views share: Grid (gridview.c), Carousel and Backdrop
// (rowview.c, and through rowview_shared.h the Vertical stack, stackview.c) and Home (home.c). Header-only. Not for
// any other module.

#include "config.h"
#include "defines.h"
#include "launcher.h"
#include "sdl.h"
#include "types.h"
#include "utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

///////////////////////////////////////
// Timing

typedef struct {
	bool active;
	Uint32 start;
} Tween;

static inline bool animationsOn(void) {
	return CFG_getMenuAnimations();
}

static inline float tweenProgress(const Tween* t, Uint32 ms) {
	if (!t->active)
		return 1.0f;
	Uint32 elapsed = SDL_GetTicks() - t->start;
	return elapsed >= ms ? 1.0f : (float)elapsed / (float)ms;
}

static inline void tweenStart(Tween* t) {
	t->active = animationsOn();
	t->start = SDL_GetTicks();
}

// One settled frame: a finished tween reports true once more as it clears.
static inline bool tweenTick(Tween* t, Uint32 ms) {
	if (!t->active)
		return false;
	if (SDL_GetTicks() - t->start >= ms)
		t->active = false;
	return true;
}

///////////////////////////////////////
// Units

// the header and the hint bar: 28 logical each at the default scale whatever the UI scale (BAR_HEIGHT), in dp
#define BAR_DP (28.0f * 42.0f / 30.0f * NATIVE_SCALE / FIXED_SCALE)

static inline float pxPerDp(void) {
	return FIXED_SCALE * 30.0f / 42.0f;
}

static inline float pxPerSp(void) {
	return FIXED_SCALE * 12.0f / 14.0f;
}

// The tab row's (or the game list's title's) and the hint bar's height: 28 logical each at the default scale.
static inline int barPx(void) {
	return BAR_HEIGHT;
}

///////////////////////////////////////
// Entries

// An entry's name as shown: its unique name when it has one, less the sorting prefix.
static inline char* View_displayName(Entry* e) {
	char* name = e->unique ? e->unique : e->name;
	trimSortingMeta(&name);
	return name;
}

// top's selection clamped into a list of n (0 when n is 0).
static inline int View_selectedIndex(int n) {
	int s = top->selected;
	if (s >= n)
		s = n - 1;
	return s < 0 ? 0 : s;
}

///////////////////////////////////////
// Cache stamps (FNV-1a, 32-bit; in-memory keys only, never persisted)

static inline Uint32 View_fnv(Uint32 h, const void* data, size_t n) {
	const unsigned char* p = data;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static inline Uint32 View_fnvStr(Uint32 h, const char* s) {
	return s ? View_fnv(h, s, strlen(s) + 1) : View_fnv(h, "\xff", 1); // NULL (no name while loading) differs from ""
}

#endif
