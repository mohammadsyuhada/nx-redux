#ifndef UI_BUTTONHINTBAR_H
#define UI_BUTTONHINTBAR_H

#include "sdl.h"

// Render the bottom button hint bar: full-width semi-transparent background,
// hardware hints first (priority), then the caller's NULL-terminated
// {button, hint} pairs, up to 4 hints total. Returns the total hint width.
int UI_renderButtonHintBar(SDL_Surface* dst, char** pairs);

#endif // UI_BUTTONHINTBAR_H
