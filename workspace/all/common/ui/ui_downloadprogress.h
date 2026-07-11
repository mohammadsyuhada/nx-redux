#ifndef UI_DOWNLOADPROGRESS_H
#define UI_DOWNLOADPROGRESS_H

#include <stdbool.h>
#include "sdl.h"

// Download progress bar with centered layout, green fill, percentage inside bar,
// detail text below, and status message above. Reusable across apps.
typedef struct {
	const char* title;	// Menu bar title (e.g., "PortMaster")
	const char* status; // Status message above bar (e.g., "Downloading binaries...")
	const char* detail; // Detail text below bar (e.g., "15.2MB / 56.8MB")
	int progress;		// Progress percentage (0-100)
	bool show_bar;		// Whether to show the progress bar
} UIDownloadProgress;

void UI_renderDownloadProgress(SDL_Surface* screen, const UIDownloadProgress* info);

#endif // UI_DOWNLOADPROGRESS_H
