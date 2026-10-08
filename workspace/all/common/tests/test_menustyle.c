// Main-menu tab and game list layout values: defaults, stored values and the main-menu Backdrop mapping.
// A settings value goes through CFG_set*Style (MenuStyle_store*) when the file is loaded and through
// CFG_get*Style (MenuStyle_mainMenu / MenuStyle_gameList) when read; a key the file lacks keeps the
// CFG_DEFAULT_* value from CFG_defaults.
#include "../config.h"
#include <assert.h>
#include <stdio.h>

static int loadMain(int stored) {
	return MenuStyle_mainMenu(MenuStyle_storeMainMenu(stored));
}

static int loadGameList(int stored) {
	return MenuStyle_gameList(MenuStyle_storeGameList(stored));
}

// Consoles and Collections start on Carousel, Tools on Grid.
static void unset_main_menu_defaults(void) {
	assert(CFG_DEFAULT_MENUSTYLE == MENU_STYLE_CAROUSEL);
	assert(CFG_DEFAULT_MENUSTYLE_FOR(MENU_CAT_CONSOLES) == MENU_STYLE_CAROUSEL);
	assert(CFG_DEFAULT_MENUSTYLE_FOR(MENU_CAT_COLLECTIONS) == MENU_STYLE_CAROUSEL);
	assert(CFG_DEFAULT_MENUSTYLE_FOR(MENU_CAT_TOOLS) == MENU_STYLE_GRID);
	for (int c = 0; c < MENU_CAT_COUNT; c++)
		assert(MenuStyle_mainMenu(CFG_DEFAULT_MENUSTYLE_FOR(c)) == CFG_DEFAULT_MENUSTYLE_FOR(c));
}

static void unset_game_list_is_carousel(void) {
	assert(CFG_DEFAULT_GAMELISTSTYLE == MENU_STYLE_CAROUSEL);
	assert(MenuStyle_gameList(CFG_DEFAULT_GAMELISTSTYLE) == MENU_STYLE_CAROUSEL);
}

static void stored_values_win(void) {
	assert(loadMain(MENU_STYLE_LIST) == MENU_STYLE_LIST);
	assert(loadMain(MENU_STYLE_GRID) == MENU_STYLE_GRID);
	assert(loadMain(MENU_STYLE_CAROUSEL) == MENU_STYLE_CAROUSEL);
	assert(loadGameList(MENU_STYLE_LIST) == MENU_STYLE_LIST);
	assert(loadGameList(MENU_STYLE_GRID) == MENU_STYLE_GRID);
	assert(loadGameList(MENU_STYLE_CAROUSEL) == MENU_STYLE_CAROUSEL);
}

static void main_menu_backdrop_reads_as_carousel(void) {
	// the stored value is left alone; only the getter maps it
	assert(MenuStyle_storeMainMenu(MENU_STYLE_BACKDROP) == MENU_STYLE_BACKDROP);
	assert(MenuStyle_mainMenu(MENU_STYLE_BACKDROP) == MENU_STYLE_CAROUSEL);
	assert(loadMain(MENU_STYLE_BACKDROP) == MENU_STYLE_CAROUSEL);
}

static void game_list_backdrop_stays(void) {
	assert(MenuStyle_storeGameList(MENU_STYLE_BACKDROP) == MENU_STYLE_BACKDROP);
	assert(loadGameList(MENU_STYLE_BACKDROP) == MENU_STYLE_BACKDROP);
}

static void out_of_range_is_the_default(void) {
	int bad[] = {-1, MENU_STYLE_COUNT, 99, -99};
	for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
		assert(MenuStyle_storeMainMenu(bad[i]) == MENU_STYLE_CAROUSEL);
		assert(MenuStyle_mainMenu(bad[i]) == MENU_STYLE_CAROUSEL);
		assert(loadMain(bad[i]) == MENU_STYLE_CAROUSEL);
		assert(MenuStyle_storeGameList(bad[i]) == MENU_STYLE_CAROUSEL);
		assert(MenuStyle_gameList(bad[i]) == MENU_STYLE_CAROUSEL);
		assert(loadGameList(bad[i]) == MENU_STYLE_CAROUSEL);
	}
}

static void choice_counts(void) {
	assert(MENUSTYLE_MAIN_COUNT == 3);
	assert(MENUSTYLE_GAMELIST_COUNT == 4);
}

// Orientation (Horizontal / Vertical). Only a Carousel main-menu tab, or a Carousel or Backdrop game list, has one;
// List and Grid always read Horizontal whatever is stored, so a stored Vertical never changes their navigation.
static void orientation_default_is_horizontal(void) {
	assert(MENU_ORIENT_HORIZONTAL == 0);
	assert(MENU_ORIENT_VERTICAL == 1);
	assert(CFG_DEFAULT_MENUORIENT == MENU_ORIENT_HORIZONTAL);
	assert(CFG_DEFAULT_GAMELISTORIENT == MENU_ORIENT_HORIZONTAL);
	// unset style + unset orientation reads Horizontal
	assert(MenuStyle_mainMenuOrient(CFG_DEFAULT_MENUSTYLE, CFG_DEFAULT_MENUORIENT) == MENU_ORIENT_HORIZONTAL);
	assert(MenuStyle_gameListOrient(CFG_DEFAULT_GAMELISTSTYLE, CFG_DEFAULT_GAMELISTORIENT) == MENU_ORIENT_HORIZONTAL);
}

static void orientation_store_and_out_of_range(void) {
	assert(MenuStyle_storeOrient(MENU_ORIENT_HORIZONTAL) == MENU_ORIENT_HORIZONTAL);
	assert(MenuStyle_storeOrient(MENU_ORIENT_VERTICAL) == MENU_ORIENT_VERTICAL);
	int bad[] = {-1, MENU_ORIENT_COUNT, 2, 99, -99};
	for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
		assert(MenuStyle_storeOrient(bad[i]) == MENU_ORIENT_HORIZONTAL);
		assert(MenuStyle_mainMenuOrient(MENU_STYLE_CAROUSEL, bad[i]) == MENU_ORIENT_HORIZONTAL);
		assert(MenuStyle_gameListOrient(MENU_STYLE_CAROUSEL, bad[i]) == MENU_ORIENT_HORIZONTAL);
		assert(MenuStyle_gameListOrient(MENU_STYLE_BACKDROP, bad[i]) == MENU_ORIENT_HORIZONTAL);
	}
}

static void main_menu_orientation_by_style(void) {
	assert(!MenuStyle_mainMenuHasOrient(MENU_STYLE_LIST));
	assert(!MenuStyle_mainMenuHasOrient(MENU_STYLE_GRID));
	assert(MenuStyle_mainMenuHasOrient(MENU_STYLE_CAROUSEL));
	assert(MenuStyle_mainMenuHasOrient(MENU_STYLE_BACKDROP)); // a stored main-menu Backdrop reads as Carousel
	assert(MenuStyle_mainMenuHasOrient(99));				  // out of range reads as Carousel (the default)
	assert(MenuStyle_mainMenuOrient(MENU_STYLE_LIST, MENU_ORIENT_VERTICAL) == MENU_ORIENT_HORIZONTAL);
	assert(MenuStyle_mainMenuOrient(MENU_STYLE_GRID, MENU_ORIENT_VERTICAL) == MENU_ORIENT_HORIZONTAL);
	assert(MenuStyle_mainMenuOrient(MENU_STYLE_CAROUSEL, MENU_ORIENT_VERTICAL) == MENU_ORIENT_VERTICAL);
	assert(MenuStyle_mainMenuOrient(MENU_STYLE_BACKDROP, MENU_ORIENT_VERTICAL) == MENU_ORIENT_VERTICAL);
	assert(MenuStyle_mainMenuOrient(MENU_STYLE_CAROUSEL, MENU_ORIENT_HORIZONTAL) == MENU_ORIENT_HORIZONTAL);
}

static void game_list_orientation_by_style(void) {
	assert(!MenuStyle_gameListHasOrient(MENU_STYLE_LIST));
	assert(!MenuStyle_gameListHasOrient(MENU_STYLE_GRID));
	assert(MenuStyle_gameListHasOrient(MENU_STYLE_CAROUSEL));
	assert(MenuStyle_gameListHasOrient(MENU_STYLE_BACKDROP));
	assert(MenuStyle_gameListHasOrient(99)); // out of range reads as Carousel (the default)
	assert(MenuStyle_gameListOrient(MENU_STYLE_LIST, MENU_ORIENT_VERTICAL) == MENU_ORIENT_HORIZONTAL);
	assert(MenuStyle_gameListOrient(MENU_STYLE_GRID, MENU_ORIENT_VERTICAL) == MENU_ORIENT_HORIZONTAL);
	assert(MenuStyle_gameListOrient(MENU_STYLE_CAROUSEL, MENU_ORIENT_VERTICAL) == MENU_ORIENT_VERTICAL);
	assert(MenuStyle_gameListOrient(MENU_STYLE_BACKDROP, MENU_ORIENT_VERTICAL) == MENU_ORIENT_VERTICAL);
	assert(MenuStyle_gameListOrient(-1, MENU_ORIENT_VERTICAL) == MENU_ORIENT_VERTICAL); // reads as Carousel
}

// The stored orientation is independent of the style: switching to List/Grid and back keeps Vertical.
static void orientation_kept_across_style_changes(void) {
	int orient = MenuStyle_storeOrient(MENU_ORIENT_VERTICAL);
	int styles[] = {MENU_STYLE_CAROUSEL, MENU_STYLE_LIST, MENU_STYLE_GRID, MENU_STYLE_CAROUSEL};
	int want_main[] = {MENU_ORIENT_VERTICAL, MENU_ORIENT_HORIZONTAL, MENU_ORIENT_HORIZONTAL, MENU_ORIENT_VERTICAL};
	for (int i = 0; i < 4; i++)
		assert(MenuStyle_mainMenuOrient(MenuStyle_storeMainMenu(styles[i]), orient) == want_main[i]);
	int gstyles[] = {MENU_STYLE_BACKDROP, MENU_STYLE_GRID, MENU_STYLE_CAROUSEL, MENU_STYLE_LIST, MENU_STYLE_BACKDROP};
	int want_game[] = {MENU_ORIENT_VERTICAL, MENU_ORIENT_HORIZONTAL, MENU_ORIENT_VERTICAL, MENU_ORIENT_HORIZONTAL,
					   MENU_ORIENT_VERTICAL};
	for (int i = 0; i < 5; i++)
		assert(MenuStyle_gameListOrient(MenuStyle_storeGameList(gstyles[i]), orient) == want_game[i]);
	assert(orient == MENU_ORIENT_VERTICAL); // never rewritten by a style change
}

// Vertical alignment: Left by default and for anything out of range; the stored values are fixed.
static void valign_store_and_out_of_range(void) {
	assert(MENU_VALIGN_LEFT == 0 && MENU_VALIGN_RIGHT == 1 && MENU_VALIGN_COUNT == 2);
	assert(MENUSTYLE_VALIGN_DEFAULT == MENU_VALIGN_LEFT);
	assert(MenuStyle_storeVAlign(MENU_VALIGN_LEFT) == MENU_VALIGN_LEFT);
	assert(MenuStyle_storeVAlign(MENU_VALIGN_RIGHT) == MENU_VALIGN_RIGHT);
	int bad[] = {-1, 2, 99};
	for (int i = 0; i < 3; i++)
		assert(MenuStyle_storeVAlign(bad[i]) == MENU_VALIGN_LEFT);
}

// Home layout: Grid by default and for anything out of range; the stored values are fixed.
static void home_style_store_and_out_of_range(void) {
	assert(HOME_STYLE_GRID == 0 && HOME_STYLE_CAROUSEL == 1 && HOME_STYLE_LIST == 2 && HOME_STYLE_COUNT == 3);
	assert(CFG_DEFAULT_HOMESTYLE == HOME_STYLE_GRID);
	assert(MenuStyle_storeHome(HOME_STYLE_GRID) == HOME_STYLE_GRID);
	assert(MenuStyle_storeHome(HOME_STYLE_CAROUSEL) == HOME_STYLE_CAROUSEL);
	assert(MenuStyle_storeHome(HOME_STYLE_LIST) == HOME_STYLE_LIST);
	int bad[] = {-1, 3, 99, -99};
	for (int i = 0; i < 4; i++)
		assert(MenuStyle_storeHome(bad[i]) == HOME_STYLE_GRID);
}

int main(void) {
	unset_main_menu_defaults();
	unset_game_list_is_carousel();
	stored_values_win();
	main_menu_backdrop_reads_as_carousel();
	game_list_backdrop_stays();
	out_of_range_is_the_default();
	choice_counts();
	orientation_default_is_horizontal();
	orientation_store_and_out_of_range();
	valign_store_and_out_of_range();
	home_style_store_and_out_of_range();
	main_menu_orientation_by_style();
	game_list_orientation_by_style();
	orientation_kept_across_style_changes();
	printf("test_menustyle: all passed\n");
	return 0;
}
