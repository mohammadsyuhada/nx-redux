#ifndef UI_ICONS_H
#define UI_ICONS_H

#include <stdbool.h>
#include "sdl.h"

// Empty icon (shared between apps)
void UI_initEmptyIcon(void);
void UI_quitEmptyIcon(void);
SDL_Surface* Icons_getEmpty(bool selected);

// Create a color-inverted copy of an icon surface (black <-> white, alpha preserved)
SDL_Surface* UI_invertIconSurface(SDL_Surface* src);

// Load an icon from path (converted to RGBA32) and create its inverted version.
// On failure both pointers are set to NULL.
void UI_loadIconPair(const char* path, SDL_Surface** original, SDL_Surface** inverted);

// Free an original/inverted icon pair and NULL the pointers
void UI_freeIconPair(SDL_Surface** original, SDL_Surface** inverted);

#endif // UI_ICONS_H
