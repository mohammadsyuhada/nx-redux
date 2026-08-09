#ifndef UI_MESSAGE_H
#define UI_MESSAGE_H

#include "sdl.h"

// Blit a message centered on the full surface.
void UI_renderCenteredMessage(SDL_Surface* dst, const char* message);

// Clear, render a centered message, flip, and hold for hold_ms (blocking).
void UI_showMessage(SDL_Surface* screen, const char* message, int hold_ms);

#endif // UI_MESSAGE_H
