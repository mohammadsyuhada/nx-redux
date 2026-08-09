#ifndef UI_FONTS_H
#define UI_FONTS_H

#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

// Theme color helpers for list items (follows system appearance).
// Thin wrappers over the UI_* equivalents in ui_list.c (kept for musicplayer/mediaplayer).
SDL_Color Fonts_getListTextColor(bool selected);
void Fonts_drawListItemBg(SDL_Surface* screen, SDL_Rect* rect, bool selected);

#endif // UI_FONTS_H
