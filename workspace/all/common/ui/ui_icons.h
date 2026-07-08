#ifndef UI_ICONS_H
#define UI_ICONS_H

#include <stdbool.h>
#include "sdl.h"

// Empty icon (shared between apps)
void UI_initEmptyIcon(void);
void UI_quitEmptyIcon(void);
SDL_Surface* Icons_getEmpty(bool selected);

#endif // UI_ICONS_H
