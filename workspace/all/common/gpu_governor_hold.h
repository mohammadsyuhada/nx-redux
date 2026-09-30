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

typedef struct {
	char saved[32];
	int held;
} GpuGovHold;

// Entering a menu speed. `current` is the governor as read from sysfs (may end in a
// newline, may be empty when unreadable). Remembers it the first time only, so
// menu -> menu idle -> menu keeps the pak's value. Returns the governor to write.
static inline const char* GpuGovHold_enterMenu(GpuGovHold* h, const char* current) {
	if (!h->held) {
		size_t n = strcspn(current ? current : "", "\r\n");
		if (n >= sizeof(h->saved))
			n = sizeof(h->saved) - 1;
		if (n > 0) {
			memcpy(h->saved, current, n);
			h->saved[n] = '\0';
			h->held = 1;
		}
	}
	return GPU_GOV_MENU;
}

// Back to a game speed: the governor to restore, or NULL when nothing is held.
// Releases the hold, so the next menu visit reads the governor afresh.
static inline const char* GpuGovHold_leaveMenu(GpuGovHold* h) {
	if (!h->held)
		return NULL;
	h->held = 0;
	return h->saved;
}

#endif
