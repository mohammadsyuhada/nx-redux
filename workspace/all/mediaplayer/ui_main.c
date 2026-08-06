#include <stdio.h>
#include <string.h>

#include "vp_defines.h"
#include "api.h"
#include "ui_main.h"
#include "ui_list.h"
#include "ui_controlshelp.h"
#include "ui_toast.h"
// Base menu items (always present)
static const char* base_menu_items[] = {"Library", "Online TV"};
#define BASE_MENU_ITEM_COUNT 2

// Render the main menu
void render_menu(SDL_Surface* screen, IndicatorType show_setting, int menu_selected,
				 char* toast_message, uint32_t toast_time) {
	SimpleMenuConfig config = {
		.title = "Media Player",
		.items = base_menu_items,
		.item_count = BASE_MENU_ITEM_COUNT,
		.btn_b_label = "EXIT",
		.get_label = NULL,
		.render_badge = NULL,
		.get_icon = NULL,
		.render_text = NULL};
	UI_renderSimpleMenu(screen, menu_selected, &config);

	// Toast notification
	UI_renderToast(screen, toast_message, toast_time);
}

// Main menu controls (A/B shown in footer)
static const ControlHelp main_menu_controls[] = {
	{"Up/Down", "Navigate"},
	{NULL, NULL}};

// File browser controls (A/B shown in footer)
static const ControlHelp browser_controls[] = {
	{"Up/Down", "Navigate"},
	{NULL, NULL}};

// IPTV user channel list controls
static const ControlHelp iptv_list_controls[] = {
	{"Up/Down", "Navigate"},
	{"Y", "Browse Channels"},
	{"X", "Remove Channel"},
	{NULL, NULL}};

// IPTV curated browse controls
static const ControlHelp iptv_curated_controls[] = {
	{"Up/Down", "Navigate"},
	{NULL, NULL}};

// Generic/default controls
static const ControlHelp default_controls[] = {
	{NULL, NULL}};

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, int app_state) {
	const ControlHelp* controls;
	const char* page_title;

	switch (app_state) {
	case STATE_MENU:
		controls = main_menu_controls;
		page_title = "Main Menu";
		break;
	case STATE_BROWSER:
		controls = browser_controls;
		page_title = "File Browser";
		break;
	case STATE_PLAYING:
		controls = default_controls;
		page_title = "Media Player";
		break;
	case STATE_IPTV_LIST:
		controls = iptv_list_controls;
		page_title = "IPTV";
		break;
	case STATE_IPTV_CURATED_COUNTRIES:
	case STATE_IPTV_CURATED_CHANNELS:
		controls = iptv_curated_controls;
		page_title = "Browse Channels";
		break;
	default:
		controls = default_controls;
		page_title = "Controls";
		break;
	}

	UI_renderControlsHelp(screen, page_title, controls);
}
