#include <stdio.h>
#include <string.h>

#include "api.h"
#include "module_common.h"
#include "module_menu.h"
#include "ui_main.h"
#include "ui_listview.h"

// Toast message state
static char menu_toast_message[128] = "";
static uint32_t menu_toast_time = 0;

// Menu items: Local, IPTV
#define MENU_ITEM_COUNT 2

// Map visual index -> logical menu item
static int get_menu_item_id(int visual_index) {
	switch (visual_index) {
	case 0:
		return MENU_LOCAL;
	case 1:
		return MENU_IPTV;
	default:
		return MENU_LOCAL;
	}
}

int MenuModule_run(SDL_Surface* screen) {
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	// The view owns selection: reset on module entry so re-entering the menu
	// starts at row 0, matching the old fresh local. render_menu supplies the
	// real items pointer as list_id on the first frame.
	ListView* v = MediaMainMenu_view();
	UI_listViewReset(v, MENU_ITEM_COUNT, NULL);

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Context menu for this page (MENU tap)
		ContextMenuItem ctx_items[1];
		int ctx_count = 0;
		ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

		// Handle global input first (START dialogs, power)
		GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, ctx_items, ctx_count);
		if (global.should_quit) {
			return MENU_QUIT;
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
		case LISTVIEW_ACTIVATED:
			GFX_clearLayers(LAYER_SCROLLTEXT);
			return get_menu_item_id(act.index);
		case LISTVIEW_BACK:
			GFX_clearLayers(LAYER_SCROLLTEXT);
			return MENU_QUIT;
		default:
			break;
		}
		if (UI_listViewBusy(v))
			dirty = 1;

		// Handle power management
		ModuleCommon_PWR_update(&dirty, &show_setting);

		if (dirty) {
			render_menu(screen, show_setting, menu_toast_message,
						menu_toast_time);

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
