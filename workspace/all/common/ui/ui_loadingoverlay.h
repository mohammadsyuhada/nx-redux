#ifndef UI_LOADINGOVERLAY_H
#define UI_LOADINGOVERLAY_H

#include "sdl.h"

// Render a full-screen semi-transparent overlay with title/subtitle text.
// Used for blocking operations (e.g. WiFi/BT toggle) with a cancel hint.
void UI_renderLoadingOverlay(SDL_Surface* dst, const char* title, const char* subtitle);

#endif // UI_LOADINGOVERLAY_H
