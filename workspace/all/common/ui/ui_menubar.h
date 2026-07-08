#ifndef UI_MENUBAR_H
#define UI_MENUBAR_H

#include "sdl.h"

// Render the top menu bar: semi-transparent background, title text (left),
// hardware group (right). Returns the width of the hardware group (ow) for
// callers that need it.
int UI_renderMenuBar(SDL_Surface* screen, const char* title);

// Snapshot the just-rendered top menu bar into a new surface (caller frees).
// Used to overlay a fixed menu bar during slide transitions. Returns NULL on failure.
SDL_Surface* UI_captureMenuBar(SDL_Surface* screen);

// Returns 1 when the wifi/bluetooth status shown in the bar changed since the
// last call (used to trigger a redraw).
int UI_statusBarChanged(void);

#endif // UI_MENUBAR_H
