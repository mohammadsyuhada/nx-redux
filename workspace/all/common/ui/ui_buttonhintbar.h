#ifndef UI_BUTTONHINTBAR_H
#define UI_BUTTONHINTBAR_H

#include "sdl.h"

// Render the bottom button hint bar: full-width semi-transparent background,
// hardware hints first (priority), then the caller's NULL-terminated
// {button, hint} pairs, up to 4 hints total. Returns the total hint width.
// The scrim is black at 80% (alpha 204).
int UI_renderButtonHintBar(SDL_Surface* dst, char** pairs);
// The same with a chosen scrim alpha (the Game Switcher uses 230).
int UI_renderButtonHintBarEx(SDL_Surface* dst, char** pairs, Uint8 scrim_alpha);

// y of the hint icons' top in a screen of screen_h rows (the bar is SCALE1(BUTTON_SIZE + 2 x BUTTON_MARGIN)
// tall at the bottom, its icons centred in it): where a list's band ends (LIST-LAYOUT §10.2).
int UI_buttonHintIconTop(int screen_h);
// y of the hint bar's top (its scrim) in a screen of screen_h rows.
int UI_buttonHintBarTop(int screen_h);

#endif // UI_BUTTONHINTBAR_H
