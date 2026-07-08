#ifndef UI_MESSAGE_H
#define UI_MESSAGE_H

#include "sdl.h"

// Blit a message centered on the full surface.
void UI_renderCenteredMessage(SDL_Surface* dst, const char* message);

#endif // UI_MESSAGE_H
