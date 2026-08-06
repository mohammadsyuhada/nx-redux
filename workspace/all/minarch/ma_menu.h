#pragma once

#include <stdbool.h>
#include <SDL2/SDL_ttf.h>

// Forward declaration (full def in ma_frontend_opts.h); guarded because C99
// forbids repeating a typedef and both headers may land in the same TU.
#ifndef MENULIST_TYPEDEF_DEFINED
#define MENULIST_TYPEDEF_DEFINED
typedef struct MenuList MenuList;
#endif

void MSG_init(void);
void MSG_quit(void);
void Menu_init(void);
void Menu_quit(void);
void Menu_beforeSleep(void);
void Menu_afterSleep(void);
int Menu_options(MenuList* list);
void Menu_screenshot(void);
void Menu_saveState(void);
void Menu_loadState(void);
void Menu_undoLoadState(void);
void Menu_initState(void);
void Menu_updateState(void);
void Menu_loop(void);
void Menu_setCoreVersionDesc(const char* version);
void Options_updateVisibility(void);
void OptionSaveChanges_updateDesc(void);
void OptionAchievements_updateDesc(void);
bool getAlias(char* path, char* alias);
int save_screenshot_thread(void* data);
