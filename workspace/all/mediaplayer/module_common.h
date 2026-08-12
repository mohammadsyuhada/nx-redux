#ifndef __MODULE_COMMON_H__
#define __MODULE_COMMON_H__

#include "api.h"
#include "ui_contextmenu.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum {
	MODULE_EXIT_TO_MENU,
	MODULE_EXIT_QUIT
} ModuleExitReason;

// Reserved context-menu item ids, handled centrally by
// ModuleCommon_handleGlobalInput. Per-page ids must stay below these.
#define CTX_ID_QUIT 900 // "Quit App" — reported back as should_quit

typedef struct {
	bool input_consumed;
	bool should_quit;
	bool dirty;
	int context_id; // Context-menu item id selected this frame (0 = none)
} GlobalInputResult;

void ModuleCommon_init(void);

// Handle global input (quit chord, MENU context menu, volume, power).
// Call at the start of each module's input loop.
//   menu_items/menu_count - the current page's context-menu items, opened on a
//     MENU tap. Pass NULL/0 for pages without a context menu. CTX_ID_QUIT is
//     dispatched centrally; any other selected id is returned in .context_id
//     for the module to handle.
GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, IndicatorType* show_setting,
												 const ContextMenuItem* menu_items, int menu_count);

// Append one item to a context-menu items array (bounds-checked).
void ModuleCommon_ctxAdd(ContextMenuItem* items, int* count, const char* label, int id);

void ModuleCommon_setAutosleepDisabled(bool disabled);
void ModuleCommon_tickToast(char* message, uint32_t toast_time, bool* dirty);
void ModuleCommon_quit(void);
void ModuleCommon_PWR_update(bool* dirty, IndicatorType* show_setting);

#endif
