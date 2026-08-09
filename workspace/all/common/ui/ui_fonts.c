#include "ui_fonts.h"
#include "ui_list.h"

// Thin compatibility wrappers. The implementations live in ui_list.c
// (UI_getListTextColor / UI_drawListItemBg); these Fonts_* names are kept
// because musicplayer/mediaplayer call them. (Fonts_calcListPillWidth was
// unused and has been removed — use UI_calcListPillWidth / UI_getListItemLayout.)

SDL_Color Fonts_getListTextColor(bool selected) {
	return UI_getListTextColor(selected);
}

void Fonts_drawListItemBg(SDL_Surface* screen, SDL_Rect* rect, bool selected) {
	UI_drawListItemBg(screen, rect, selected);
}
