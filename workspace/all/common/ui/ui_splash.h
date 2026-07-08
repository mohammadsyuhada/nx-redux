#ifndef UI_SPLASH_H
#define UI_SPLASH_H

#include "sdl.h"

// Render a splash screen with a title and "Loading..." subtitle, then flip.
// Call immediately after GFX_init() for instant visual feedback during app startup.
void UI_showSplashScreen(SDL_Surface* screen, const char* title);

#endif // UI_SPLASH_H
