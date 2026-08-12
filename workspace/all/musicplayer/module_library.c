#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_library.h"
#include "module_player.h"
#include "module_playlist.h"
#include "ui_listview.h"
#include "display_helper.h"

// Library submenu items
#define LIBRARY_FILES 0
#define LIBRARY_PLAYLISTS 1
#define LIBRARY_ITEM_COUNT 2

// Help state for controls dialog
#define LIBRARY_MENU_HELP_STATE 55

static const char* library_items[] = {"Files", "Playlists"};

// Library menu ListView (full mode: the widget owns selection and input)
static ListView library_view;

static void library_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	const char** items = ctx;
	out->label = items[i];
	(void)selected;
}

static void render_library_menu(SDL_Surface* screen) {
	GFX_clear(screen);
	ListView* v = &library_view;
	v->title = "Library";
	v->count = LIBRARY_ITEM_COUNT;
	v->get_row = library_get_row;
	v->ctx = (void*)library_items;
	v->list_id = (const void*)library_items;
	v->hint_pairs = (char*[]){"B", "BACK", "A", "OPEN", NULL};
	UI_listViewRender(v, screen);
}

ModuleExitReason LibraryModule_run(SDL_Surface* screen) {
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	// The view owns selection: reset on module entry so the menu starts at
	// row 0, matching the old fresh local.
	ListView* v = &library_view;
	UI_listViewReset(v, LIBRARY_ITEM_COUNT, library_items);

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Context menu for this page
		ContextMenuItem ctx_items[1];
		int ctx_count = 0;
		ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

		// Handle global input
		GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, LIBRARY_MENU_HELP_STATE, ctx_items, ctx_count);
		if (global.should_quit) {
			return MODULE_EXIT_QUIT;
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
			ModuleExitReason reason = MODULE_EXIT_TO_MENU;

			switch (act.index) {
			case LIBRARY_FILES:
				reason = PlayerModule_run(screen);
				break;
			case LIBRARY_PLAYLISTS:
				reason = PlaylistModule_run(screen);
				break;
			}

			if (reason == MODULE_EXIT_QUIT) {
				return MODULE_EXIT_QUIT;
			}

			// TG5050: a keyboard inside the sub-module may have triggered
			// display recovery — pick up the new screen surface
			{
				SDL_Surface* ns = DisplayHelper_getReinitScreen();
				if (ns)
					screen = ns;
			}

			// Sub-module returned to library menu
			dirty = 1;
			break;
		}
		case LISTVIEW_BACK:
			return MODULE_EXIT_TO_MENU;
		default:
			break;
		}
		if (UI_listViewBusy(v))
			dirty = 1;

		// Handle power management
		ModuleCommon_PWR_update(&dirty, &show_setting);

		if (dirty) {
			render_library_menu(screen);

			GFX_flip(screen);
			dirty = 0;
		} else {
			UI_listViewTickIdle(v);
			GFX_sync();
		}
	}
}
