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

static void unset_main_menu_is_carousel(void) {
	assert(CFG_DEFAULT_MENUSTYLE == MENU_STYLE_CAROUSEL);
	assert(MenuStyle_mainMenu(CFG_DEFAULT_MENUSTYLE) == MENU_STYLE_CAROUSEL);
}

static void unset_game_list_is_grid(void) {
	assert(CFG_DEFAULT_GAMELISTSTYLE == MENU_STYLE_GRID);
	assert(MenuStyle_gameList(CFG_DEFAULT_GAMELISTSTYLE) == MENU_STYLE_GRID);
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
		assert(MenuStyle_storeGameList(bad[i]) == MENU_STYLE_GRID);
		assert(MenuStyle_gameList(bad[i]) == MENU_STYLE_GRID);
		assert(loadGameList(bad[i]) == MENU_STYLE_GRID);
	}
}

static void choice_counts(void) {
	assert(MENUSTYLE_MAIN_COUNT == 3);
	assert(MENUSTYLE_GAMELIST_COUNT == 4);
}

int main(void) {
	unset_main_menu_is_carousel();
	unset_game_list_is_grid();
	stored_values_win();
	main_menu_backdrop_reads_as_carousel();
	game_list_backdrop_stays();
	out_of_range_is_the_default();
	choice_counts();
	printf("test_menustyle: all passed\n");
	return 0;
}
