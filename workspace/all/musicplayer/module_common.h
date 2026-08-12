#ifndef __MODULE_COMMON_H__
#define __MODULE_COMMON_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "api.h"
#include "ui_contextmenu.h"

// Screen off hint duration (time hint is shown before screen turns off)
#define SCREEN_OFF_HINT_DURATION_MS 4000

// Module exit reasons
typedef enum {
	MODULE_EXIT_TO_MENU, // User pressed B, return to main menu
	MODULE_EXIT_QUIT	 // User confirmed quit, exit app entirely
} ModuleExitReason;

// Reserved context-menu item ids, handled centrally by
// ModuleCommon_handleGlobalInput. Per-page ids must stay below these.
#define CTX_ID_QUIT 900		// "Quit App" — reported back as should_quit
#define CTX_ID_CONTROLS 901 // "Playback Controls" — opens the controls-help modal

// Result from global input handling
typedef struct {
	bool input_consumed; // True if global input was handled (dialog shown, etc.)
	bool should_quit;	 // True if quit was confirmed
	bool dirty;			 // True if screen needs redraw
	int context_id;		 // Context-menu item id selected this frame (0 = none)
} GlobalInputResult;

// Initialize module common (call once at app startup)
void ModuleCommon_init(void);

// Handle global input (quit chord, MENU context menu / controls help, volume,
// power management). Call at the start of each module's input loop.
// Parameters:
//   screen - SDL surface for rendering dialogs
//   show_setting - pointer to show_setting flag (for power hints)
//   app_state - current app state (for controls help context)
//   menu_items/menu_count - the current page's context-menu items, opened on a
//     MENU tap (except on the music Now Playing page, where MENU keeps the
//     controls-help modal). Pass NULL/0 for pages without a context menu.
//     CTX_ID_QUIT and CTX_ID_CONTROLS are dispatched centrally; any other
//     selected id is returned in .context_id for the module to handle.
GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, IndicatorType* show_setting, int app_state,
												 const ContextMenuItem* menu_items, int menu_count);

// Append one item to a context-menu items array, bounds-checked against the
// array's real capacity. The macro derives the capacity from the passed array
// (which must be a real array in scope, not a pointer), so a caller can't add
// past the end of its own small stack array.
void ModuleCommon_ctxAdd_impl(ContextMenuItem* items, int* count, int cap, const char* label, int id);
#define ModuleCommon_ctxAdd(items, count, label, id) \
	ModuleCommon_ctxAdd_impl((items), (count), (int)(sizeof(items) / sizeof((items)[0])), (label), (id))

// Disable/enable autosleep (for modules with active playback)
void ModuleCommon_setAutosleepDisabled(bool disabled);

// Check if screen off hint is active
bool ModuleCommon_isScreenOffHintActive(void);

// Start screen off hint countdown
void ModuleCommon_startScreenOffHint(void);

// Reset (cancel) screen off hint
void ModuleCommon_resetScreenOffHint(void);

// Check screen off hint timeout using dual SDL tick + wallclock check.
// If timed out: deactivates hint and disables backlight. Returns true.
// If still counting down or hint not active: returns false.
bool ModuleCommon_processScreenOffHintTimeout(void);

// Record last input time (for auto screen-off timeout)
void ModuleCommon_recordInputTime(void);

// Check if auto screen-off timeout has elapsed since last input.
// If timed out: starts screen off hint and returns true.
// Caller is responsible for clearing GPU layers after this returns true.
bool ModuleCommon_checkAutoScreenOffTimeout(void);

// Check toast state: if active and not expired, sets dirty=1; if expired, clears message and sets dirty=1.
void ModuleCommon_tickToast(char* message, uint32_t toast_time, bool* dirty);

// Clean up module common resources (call at app exit)
void ModuleCommon_quit(void);

// PWR_update wrapper with overlay auto-hide on button release
// Call this instead of PWR_update directly in modules
void ModuleCommon_PWR_update(bool* dirty, IndicatorType* show_setting);

#endif
