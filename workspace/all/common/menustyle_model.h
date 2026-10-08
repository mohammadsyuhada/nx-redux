#ifndef MENUSTYLE_MODEL_H
#define MENUSTYLE_MODEL_H

// Main menu tab and game list layout styles, their defaults and how a stored value reads.
// Also the Carousel / Backdrop orientation (Horizontal or Vertical) and its effective value per style.
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
#define MENUSTYLE_GAMELIST_DEFAULT MENU_STYLE_CAROUSEL

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

// Orientation of a Carousel (main-menu tab) or a Carousel / Backdrop (game lists). Stored in minuisettings.txt, so
// never renumber. Stored per main-menu tab and once for game lists, independent of the style, so it is kept when the
// style changes away and back.
enum { MENU_ORIENT_HORIZONTAL = 0,
	   MENU_ORIENT_VERTICAL,
	   MENU_ORIENT_COUNT };
#define MENUSTYLE_ORIENT_DEFAULT MENU_ORIENT_HORIZONTAL

// The value to store (and how a stored value reads): an out-of-range value becomes Horizontal.
static inline int MenuStyle_storeOrient(int orient) {
	if (orient < MENU_ORIENT_HORIZONTAL || orient >= MENU_ORIENT_COUNT)
		return MENUSTYLE_ORIENT_DEFAULT;
	return orient;
}

// The game lists' Vertical alignment (Carousel and Backdrop, Vertical): Left, the stack left of centre with the caption
// on its right; Right, the same mirrored about the screen's centre (the caption's text still left-aligned). Stored values
// (minuisettings.txt): never renumber. Kept whatever the style and orientation.
enum { MENU_VALIGN_LEFT = 0,
	   MENU_VALIGN_RIGHT,
	   MENU_VALIGN_COUNT };
#define MENUSTYLE_VALIGN_DEFAULT MENU_VALIGN_LEFT

// The value to store (and how a stored value reads): an out-of-range value becomes Left.
static inline int MenuStyle_storeVAlign(int valign) {
	if (valign < MENU_VALIGN_LEFT || valign >= MENU_VALIGN_COUNT)
		return MENUSTYLE_VALIGN_DEFAULT;
	return valign;
}

// The Home tab's layout (Layouts > Home layout): Grid, Continue beside the tool squares with the pinned games below;
// Carousel, Continue first in a row of the pinned games, the tools in a dock under it; or List, one pill list of
// Continue, the pinned games and the pinned tools. Stored values (minuisettings.txt): never renumber.
enum { HOME_STYLE_GRID = 0,
	   HOME_STYLE_CAROUSEL,
	   HOME_STYLE_LIST,
	   HOME_STYLE_COUNT };
#define HOMESTYLE_DEFAULT HOME_STYLE_GRID

// The value to store (and how a stored value reads): an out-of-range value becomes Grid.
static inline int MenuStyle_storeHome(int style) {
	if (style < HOME_STYLE_GRID || style >= HOME_STYLE_COUNT)
		return HOMESTYLE_DEFAULT;
	return style;
}

// Whether a main-menu tab's stored style has an orientation: only Carousel (a stored Backdrop reads as Carousel).
static inline int MenuStyle_mainMenuHasOrient(int stored_style) {
	return MenuStyle_mainMenu(stored_style) == MENU_STYLE_CAROUSEL;
}

// Whether the stored game-list style has an orientation: Carousel or Backdrop.
static inline int MenuStyle_gameListHasOrient(int stored_style) {
	int style = MenuStyle_gameList(stored_style);
	return style == MENU_STYLE_CAROUSEL || style == MENU_STYLE_BACKDROP;
}

// The effective orientation of a main-menu tab: List and Grid always read Horizontal, whatever is stored.
static inline int MenuStyle_mainMenuOrient(int stored_style, int stored_orient) {
	return MenuStyle_mainMenuHasOrient(stored_style) ? MenuStyle_storeOrient(stored_orient) : MENU_ORIENT_HORIZONTAL;
}

// The effective orientation of the game lists: List and Grid always read Horizontal, whatever is stored.
static inline int MenuStyle_gameListOrient(int stored_style, int stored_orient) {
	return MenuStyle_gameListHasOrient(stored_style) ? MenuStyle_storeOrient(stored_orient) : MENU_ORIENT_HORIZONTAL;
}

#endif
