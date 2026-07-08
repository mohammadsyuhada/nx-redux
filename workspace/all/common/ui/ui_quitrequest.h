#ifndef UI_QUITREQUEST_H
#define UI_QUITREQUEST_H

#include <stdbool.h>
#include "sdl.h"

// Handle long-press BTN_START to quit with a confirmation dialog.
// Call every frame after PAD_poll(). When the long-press threshold is reached,
// shows a blocking confirmation dialog. Sets *quit = true on confirm, *dirty = 1 on return.
void UI_handleQuitRequest(SDL_Surface* screen, bool* quit, bool* dirty,
						  const char* title, const char* subtitle);

#endif // UI_QUITREQUEST_H
