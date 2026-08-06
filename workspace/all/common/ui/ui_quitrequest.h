#ifndef UI_QUITREQUEST_H
#define UI_QUITREQUEST_H

#include <stdbool.h>
#include "sdl.h"

// Handle the MENU + SELECT combo to quit with a confirmation dialog.
// Call every frame after PAD_poll(). When the combo is pressed, shows a blocking
// confirmation dialog. Sets *quit = true on confirm, *dirty = 1 on return.
void UI_handleQuitRequest(SDL_Surface* screen, bool* quit, bool* dirty,
						  const char* title, const char* subtitle);

#endif // UI_QUITREQUEST_H
