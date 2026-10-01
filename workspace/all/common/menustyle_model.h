#ifndef MENUSTYLE_MODEL_H
#define MENUSTYLE_MODEL_H

// Main menu tab and game list layout styles, their defaults and how a stored value reads.
// Pure (no SDL, no settings file) so the mapping is host-tested (tests/test_menustyle.c).

// Main menu tab and game list layout styles. The values are stored in minuisettings.txt, so never renumber.
enum { MENU_STYLE_LIST = 0,
	   MENU_STYLE_GRID,
	   MENU_STYLE_CAROUSEL,
	   MENU_STYLE_BACKDROP, // game lists only; a stored main-menu Backdrop reads as Carousel
	   MENU_STYLE_COUNT };

// Main-menu tabs (Consoles, Collections, Tools) choose List, Grid or Carousel.
#define MENUSTYLE_MAIN_COUNT 3
#define MENUSTYLE_MAIN_DEFAULT MENU_STYLE_CAROUSEL
// Game lists choose List, Grid, Carousel or Backdrop.
#define MENUSTYLE_GAMELIST_COUNT MENU_STYLE_COUNT
#define MENUSTYLE_GAMELIST_DEFAULT MENU_STYLE_GRID

// The value to store for a main-menu tab: an out-of-range value becomes the default. A Backdrop (3) is kept as
// written, so a file from an older build is left alone; MenuStyle_mainMenu reads it as Carousel.
static inline int MenuStyle_storeMainMenu(int style) {
	if (style < MENU_STYLE_LIST || style >= MENU_STYLE_COUNT)
		return MENUSTYLE_MAIN_DEFAULT;
	return style;
}

// How a stored main-menu tab value reads: Backdrop and out-of-range values read as Carousel (the default).
static inline int MenuStyle_mainMenu(int stored) {
	if (stored < MENU_STYLE_LIST || stored >= MENUSTYLE_MAIN_COUNT)
		return MENUSTYLE_MAIN_DEFAULT;
	return stored;
}

// The value to store for game lists: an out-of-range value becomes the default.
static inline int MenuStyle_storeGameList(int style) {
	if (style < MENU_STYLE_LIST || style >= MENUSTYLE_GAMELIST_COUNT)
		return MENUSTYLE_GAMELIST_DEFAULT;
	return style;
}

// How a stored game list value reads: all four styles stay, an out-of-range value reads as the default.
static inline int MenuStyle_gameList(int stored) {
	return MenuStyle_storeGameList(stored);
}

#endif
