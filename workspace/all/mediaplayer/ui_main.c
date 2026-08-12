#include <stdio.h>
#include <string.h>

#include "api.h"
#include "ui_main.h"
#include "ui_listview.h"
#include "ui_toast.h"
// Base menu items (always present)
static const char* base_menu_items[] = {"Library", "Online TV"};
#define BASE_MENU_ITEM_COUNT 2

// Main menu ListView (full mode: the widget owns selection and input;
// MenuModule_run drives it through MediaMainMenu_view()).
static ListView main_menu_view;

ListView* MediaMainMenu_view(void) {
	return &main_menu_view;
}

static void main_menu_get_row(void* ctx, int i, bool selected,
							  ListViewRow* out) {
	const char** items = ctx;
	out->label = items[i];
	(void)selected;
}

// Render the main menu
void render_menu(SDL_Surface* screen, IndicatorType show_setting,
				 char* toast_message, uint32_t toast_time) {
	(void)show_setting;
	GFX_clear(screen);
	ListView* v = &main_menu_view;
	v->title = "Media Player";
	v->font = font.large;
	v->count = BASE_MENU_ITEM_COUNT;
	v->get_row = main_menu_get_row;
	v->ctx = (void*)base_menu_items;
	v->list_id = (const void*)base_menu_items;
	v->hint_pairs = (char*[]){"B", "EXIT", "A", "OPEN", NULL};
	UI_listViewRender(v, screen);

	// Toast notification
	UI_renderToast(screen, toast_message, toast_time);
}
