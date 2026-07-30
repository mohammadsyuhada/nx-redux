#pragma once

#include "sdl.h"

// -----------------------------------------------------------------------
// Menu type system
// -----------------------------------------------------------------------

// Guarded like MENU_CALLBACK_CODES_DEFINED below: ma_menu.h forward-declares
// the same typedef, and C99 forbids repeating a typedef.
#ifndef MENULIST_TYPEDEF_DEFINED
#define MENULIST_TYPEDEF_DEFINED
typedef struct MenuList MenuList;
#endif
typedef struct MenuItem MenuItem;
// MENU_CALLBACK_* codes come from minarch.h (guarded there so the netplay
// module sees the same values without pulling in this file's statics).
#ifndef MENU_CALLBACK_CODES_DEFINED
#define MENU_CALLBACK_CODES_DEFINED
enum {
	MENU_CALLBACK_NOP,
	MENU_CALLBACK_EXIT,
	MENU_CALLBACK_NEXT_ITEM,
};
#endif
typedef int (*MenuList_callback_t)(MenuList* list, int i);
struct MenuItem {
	char* name;
	char* desc;
	char** values;
	char* key; // optional, used by options
	int id;	   // optional, used by bindings
	int value;
	MenuList* submenu;
	MenuList_callback_t on_confirm;
	MenuList_callback_t on_change;
};

enum {
	MENU_LIST,	// eg. save and main menu
	MENU_VAR,	// eg. frontend
	MENU_FIXED, // eg. emulator
	MENU_INPUT, // eg. renders like but MENU_VAR but handles input differently
};
struct MenuList {
	int type;
	int max_width; // cached on first draw
	char* desc;
	char* category; // currently displayed category
	MenuItem* items;
	MenuList_callback_t on_confirm;
	MenuList_callback_t on_change;
};

// Public API: functions defined in ma_frontend_opts.c
int Menu_message(char* message, char** pairs);
int Menu_messageWithFont(char* message, char** pairs, TTF_Font* f);
char* getSaveDesc(void);
int MenuList_freeItems(MenuList* list, int i);

int OptionFrontend_openMenu(MenuList* list, int i);
int OptionShaders_openMenu(MenuList* list, int i);
int OptionCheats_openMenu(MenuList* list, int i);
int OptionPragmas_openMenu(MenuList* list, int i);
int OptionControls_openMenu(MenuList* list, int i);
int OptionShortcuts_openMenu(MenuList* list, int i);
int OptionSaveChanges_openMenu(MenuList* list, int i);
int OptionQuicksave_onConfirm(MenuList* list, int i);
