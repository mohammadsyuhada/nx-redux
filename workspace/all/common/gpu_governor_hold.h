#ifndef GPU_GOVERNOR_HOLD_H
#define GPU_GOVERNOR_HOLD_H
// Holds the GPU devfreq governor a pak chose while a menu speed overrides it, kept
// pure so it can be host-tested. The menu speeds switch the GPU to simple_ondemand;
// the game speeds must put the pak's governor back (DC/PS/PSP launch.sh set
// performance), or one visit to minarch's in-game menu leaves the GPU throttled for
// the rest of the game: on the Smart Pro S simple_ondemand stayed at 150 MHz under
// PPSSPP and Tekken 6 at 2x dropped to 88% speed (2026-09-30).
#include <stddef.h>
#include <string.h>

#define GPU_GOV_MENU "simple_ondemand"
#define GPU_GOV_BOOST "performance"

// A pak can also opt in to a shader boost (shared/bin/gpu-shader-boost.sh): while a shader is
// on, the game speeds use GPU_GOV_BOOST, and with it off they go back to `base`, the governor
// the GPU had before the pak's launch.sh boosted it (`saved` may hold the boost by then).
typedef struct {
	char saved[32];
	int held;
	int boost;
	char base[32];
} GpuGovHold;

static inline void GpuGovHold_copy(char* dst, size_t size, const char* src) {
	size_t n = strcspn(src ? src : "", "\r\n");
	if (n >= size)
		n = size - 1;
	memcpy(dst, src ? src : "", n);
	dst[n] = '\0';
}

// Entering a menu speed. `current` is the governor as read from sysfs (may end in a
// newline, may be empty when unreadable). Remembers it the first time only, so
// menu -> menu idle -> menu keeps the pak's value. Returns the governor to write.
static inline const char* GpuGovHold_enterMenu(GpuGovHold* h, const char* current) {
	if (!h->held) {
		GpuGovHold_copy(h->saved, sizeof(h->saved), current);
		if (h->saved[0])
			h->held = 1;
	}
	return GPU_GOV_MENU;
}

// The game's governor once the menu is left, or NULL for nothing to write.
static inline const char* GpuGovHold_gameGovernor(const GpuGovHold* h, const char* fallback) {
	if (h->boost)
		return GPU_GOV_BOOST;
	if (h->base[0])
		return h->base;
	return fallback;
}

// Back to a game speed: the governor to restore, or NULL when nothing is held.
// Releases the hold, so the next menu visit reads the governor afresh.
static inline const char* GpuGovHold_leaveMenu(GpuGovHold* h) {
	if (!h->held)
		return NULL;
	h->held = 0;
	return GpuGovHold_gameGovernor(h, h->saved);
}

// The shader count changed (minarch, opted-in paks only): `on` while any shader is active, `base`
// the governor from before the pak's boost. Returns the governor to write now, or NULL: inside a
// menu speed nothing is written, the game governor follows when the menu is left.
static inline const char* GpuGovHold_setBoost(GpuGovHold* h, int on, const char* base) {
	if (base && base[0])
		GpuGovHold_copy(h->base, sizeof(h->base), base);
	h->boost = on ? 1 : 0;
	if (h->held)
		return NULL;
	return GpuGovHold_gameGovernor(h, NULL);
}

#endif
