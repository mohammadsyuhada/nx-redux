#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_menu.h"
#include "ui_listview.h"
#include "ui_main.h"
#include "resume.h"
#include "background.h"

// Toast message state
static char menu_toast_message[128] = "";
static uint32_t menu_toast_time = 0;

// Context-menu item ids
#define MENU_CTX_CLEAR 1 // Stop Playback / Clear History (same as X on row 0)

// Stop background playback or clear the resume entry (X on row 0, and the
// matching context-menu item).
static void clear_first_item(int first_item_mode, bool* dirty) {
	if (first_item_mode == MENU_FIRST_NOW_PLAYING) {
		Background_stopAll();
		GFX_clearLayers(LAYER_SCROLLTEXT);
		*dirty = 1;
	} else if (first_item_mode == MENU_FIRST_RESUME) {
		Resume_clear();
		GFX_clearLayers(LAYER_SCROLLTEXT);
		*dirty = 1;
	}
}

int MenuModule_run(SDL_Surface* screen) {
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	// The view owns selection: reset on module entry so re-entering the menu
	// starts at row 0, matching the old fresh local. render_menu supplies the
	// real items pointer as list_id on the first frame.
	ListView* v = MusicMainMenu_view();
	int entry_count = (Background_isPlaying() || Resume_isAvailable()) ? 5 : 4;
	UI_listViewReset(v, entry_count, NULL);

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Handle background player updates (track advancement, resume saving)
		Background_tick();
		if (Background_isPlaying()) {
			ModuleCommon_setAutosleepDisabled(true);
		}

		// Determine first item: Now Playing (if BG active) > Resume > none
		int first_item_mode = MENU_FIRST_NONE;
		if (Background_isPlaying()) {
			first_item_mode = MENU_FIRST_NOW_PLAYING;
		} else if (Resume_isAvailable()) {
			first_item_mode = MENU_FIRST_RESUME;
		}
		bool has_first = (first_item_mode != MENU_FIRST_NONE);

		// Context menu for this page
		ContextMenuItem ctx_items[2];
		int ctx_count = 0;
		if (first_item_mode == MENU_FIRST_NOW_PLAYING)
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Stop Playback", MENU_CTX_CLEAR);
		else if (first_item_mode == MENU_FIRST_RESUME)
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Clear History", MENU_CTX_CLEAR);
		ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

		// Handle global input first (volume, START dialogs, power)
		GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 0, ctx_items, ctx_count);
		if (global.should_quit) {
			return MENU_QUIT;
		}
		if (global.context_id == MENU_CTX_CLEAR) {
			clear_first_item(first_item_mode, &dirty);
		}
		if (global.input_consumed) {
			if (global.dirty)
				dirty = 1;
			GFX_sync();
			continue;
		}

		// Menu input: the ListView owns navigation, the module switches on
		// the returned action. MENU is handled globally above - ignored here.
		ListViewAction act = UI_listViewHandleInput(v);
		switch (act.type) {
		case LISTVIEW_ACTIVATED: {
			GFX_clearLayers(LAYER_SCROLLTEXT);
			// Adjust selection to match MENU_* constants
			int selection = act.index;
			if (!has_first)
				selection += 1; // Skip first-item slot
			return selection;
		}
		case LISTVIEW_BACK:
			GFX_clearLayers(LAYER_SCROLLTEXT);
			// Exit app from main menu
			return MENU_QUIT;
		default:
			// Stop/clear moved to the context menu (MENU tap)
			break;
		}
		if (UI_listViewBusy(v))
			dirty = 1;

		// Handle power management
		ModuleCommon_PWR_update(&dirty, &show_setting);

		if (dirty) {
			render_menu(screen, show_setting, menu_toast_message,
						menu_toast_time, first_item_mode);

			GFX_flip(screen);
			dirty = 0;

			// Keep refreshing while toast is visible
			ModuleCommon_tickToast(menu_toast_message, menu_toast_time, &dirty);
		} else {
			UI_listViewTickIdle(v);
			GFX_sync();
		}
	}
}

// Set toast message (called by modules that return to menu with a message)
void MenuModule_setToast(const char* message) {
	snprintf(menu_toast_message, sizeof(menu_toast_message), "%s", message);
	menu_toast_time = SDL_GetTicks();
}
