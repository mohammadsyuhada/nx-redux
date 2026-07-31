#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "settings_menu.h"
#include "ui_list.h"
#include "ui_splash.h"
#include "ui_quitrequest.h"
#include "ui_confirmdialog.h"
#include "ui_menubar.h"
#include "ui_buttonhintbar.h"
#include "display_helper.h"
#include "ra_auth.h"
#include "ra_offline.h"

#include "ratools_browser.h"
#include "ratools_data.h"
#include "ratools_prefetch.h"
#include "ratools_reset.h"
#include "ratools_sync.h"

#define RA_ROOT_DIR SHARED_USERDATA_PATH "/.ra"

SDL_Surface* g_screen = NULL;

// ============================================
// Pages
// ============================================
static SettingsPage ra_settings_page;
static SettingItem ra_settings_items[16];

static void init_page(SettingsPage* page, const char* title, SettingItem* items,
					  int count, int is_list) {
	memset(page, 0, sizeof(SettingsPage));
	page->title = title;
	page->items = items;
	page->item_count = count;
	page->selected = 0;
	page->scroll = 0;
	page->is_list = is_list;
	page->dynamic_start = -1;
	page->max_items = count;
}

// ============================================
// Value labels (copied from settings.c)
// ============================================
static const char* on_off_labels[] = {"Off", "On"};
static int on_off_values[] = {0, 1};
static const char* notify_duration_labels[] = {"1s", "2s", "3s", "4s", "5s"};
static int notify_duration_values[] = {1, 2, 3, 4, 5};
#define NOTIFY_DURATION_COUNT 5
static const char* progress_duration_labels[] = {"Off", "1s", "2s", "3s", "4s", "5s"};
static int progress_duration_values[] = {0, 1, 2, 3, 4, 5};
#define PROGRESS_DURATION_COUNT 6

/* RA sort order */
static int ra_sort_values[] = {
	RA_SORT_UNLOCKED_FIRST,
	RA_SORT_DISPLAY_ORDER_FIRST,
	RA_SORT_DISPLAY_ORDER_LAST,
	RA_SORT_WON_BY_MOST,
	RA_SORT_WON_BY_LEAST,
	RA_SORT_POINTS_MOST,
	RA_SORT_POINTS_LEAST,
	RA_SORT_TITLE_AZ,
	RA_SORT_TITLE_ZA,
	RA_SORT_TYPE_ASC,
	RA_SORT_TYPE_DESC};
static const char* ra_sort_labels[] = {
	"Unlocked First",
	"Display Order (First)",
	"Display Order (Last)",
	"Won By (Most)",
	"Won By (Least)",
	"Points (Most)",
	"Points (Least)",
	"Title (A-Z)",
	"Title (Z-A)",
	"Type (Asc)",
	"Type (Desc)"};
#define RA_SORT_LABEL_COUNT 11

// ============================================
// Status display helpers (reused by the custom main-screen header and,
// where noted, by the settings page)
// ============================================
static const char* get_user_display(void) {
	const char* u = CFG_getRAUsername();
	return (u && u[0]) ? u : "(not set)";
}

static char pending_buf[64];
static const char* get_pending_display(void) {
	int n = RA_Offline_pendingCount();
	snprintf(pending_buf, sizeof(pending_buf), "%d unlock%s waiting",
			 n, n == 1 ? "" : "s");
	return pending_buf;
}

static char lastsync_buf[64];
static const char* get_lastsync_display(void) {
	time_t t = RA_Offline_lastSyncTime();
	if (t == 0)
		return "Never";
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(lastsync_buf, sizeof(lastsync_buf), "%Y-%m-%d %H:%M", &tm);
	return lastsync_buf;
}

static void on_prefetch(void) {
	RATPrefetch_run(g_screen);
}

// Blocking confirm dialog: A confirms, B cancels. Returns true on confirm.
static bool ra_confirm(const char* title, const char* subtitle) {
	bool dirty = true;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A)) {
			PAD_reset();
			return true;
		}
		if (PAD_justPressed(BTN_B)) {
			PAD_reset();
			return false;
		}
		if (dirty) {
			GFX_clear(g_screen);
			UI_renderConfirmDialog(g_screen, title, subtitle);
			GFX_flip(g_screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

static void on_reset_account(void) {
	if (ra_confirm("Reset account data?",
				   "Clears login, unlock progress, and pending syncs. Game data and badges are kept.")) {
		RATReset_clearAccountData();
	}
}

static void on_reset_all(void) {
	if (ra_confirm("Erase all achievement data?",
				   "Removes everything including cached games and badges. Redownloaded on next online play.")) {
		RATReset_clearAll();
	}
}

// ============================================
// RA settings callbacks (moved from settings.c)
// ============================================
static int get_ra_enable(void) {
	return CFG_getRAEnable() ? 1 : 0;
}
static void set_ra_enable(int v) {
	CFG_setRAEnable(v != 0);
}
static void reset_ra_enable(void) {
	CFG_setRAEnable(CFG_DEFAULT_RA_ENABLE);
}

// Hardcore mode is deliberately not offered: nx-redux is not an
// RA-approved hardcore-compliant emulator, so hardcore unlocks are
// softcore-locked server-side and enabling it only risks the account.

static const char* get_ra_username_display(void) {
	const char* u = CFG_getRAUsername();
	return (u && u[0]) ? u : "(not set)";
}
static void on_ra_username_set(const char* text) {
	if (text)
		CFG_setRAUsername(text);
}

static const char* get_ra_password_display(void) {
	const char* p = CFG_getRAPassword();
	return (p && p[0]) ? "********" : "(not set)";
}
static void on_ra_password_set(const char* text) {
	if (text)
		CFG_setRAPassword(text);
}

static char ra_auth_status_msg[256] = "";

static void on_ra_authenticate(void) {
	const char* username = CFG_getRAUsername();
	const char* password = CFG_getRAPassword();

	if (!username || !username[0] || !password || !password[0]) {
		snprintf(ra_auth_status_msg, sizeof(ra_auth_status_msg),
				 "Error: Username and password required");
		return;
	}

	RA_AuthResponse response;
	RA_AuthResult result = RA_authenticateSync(username, password, &response);

	if (result == RA_AUTH_SUCCESS) {
		CFG_setRAToken(response.token);
		CFG_setRAAuthenticated(true);
		snprintf(ra_auth_status_msg, sizeof(ra_auth_status_msg),
				 "Authenticated as %s", response.display_name);
	} else {
		CFG_setRAToken("");
		CFG_setRAAuthenticated(false);
		snprintf(ra_auth_status_msg, sizeof(ra_auth_status_msg),
				 "Error: %s", response.error_message);
	}
}

static const char* get_ra_status(void) {
	if (ra_auth_status_msg[0])
		return ra_auth_status_msg;
	if (CFG_getRAAuthenticated() && strlen(CFG_getRAToken()) > 0)
		return "Authenticated";
	return "Not authenticated";
}

static int get_ra_show_notifications(void) {
	return CFG_getRAShowNotifications() ? 1 : 0;
}
static void set_ra_show_notifications(int v) {
	CFG_setRAShowNotifications(v != 0);
}
static void reset_ra_show_notifications(void) {
	CFG_setRAShowNotifications(CFG_DEFAULT_RA_SHOW_NOTIFICATIONS);
}

static int get_ra_notify_duration(void) {
	return CFG_getRANotificationDuration();
}
static void set_ra_notify_duration(int val) {
	CFG_setRANotificationDuration(val);
}
static void reset_ra_notify_duration(void) {
	CFG_setRANotificationDuration(CFG_DEFAULT_RA_NOTIFICATION_DURATION);
}

static int get_ra_progress_duration(void) {
	return CFG_getRAProgressNotificationDuration();
}
static void set_ra_progress_duration(int val) {
	CFG_setRAProgressNotificationDuration(val);
}
static void reset_ra_progress_duration(void) {
	CFG_setRAProgressNotificationDuration(CFG_DEFAULT_RA_PROGRESS_NOTIFICATION_DURATION);
}

static int get_ra_sort_order(void) {
	return CFG_getRAAchievementSortOrder();
}
static void set_ra_sort_order(int val) {
	CFG_setRAAchievementSortOrder(val);
}
static void reset_ra_sort_order(void) {
	CFG_setRAAchievementSortOrder(CFG_DEFAULT_RA_ACHIEVEMENT_SORT_ORDER);
}

static void reset_ra_page(void) {
	reset_ra_enable();
	reset_ra_show_notifications();
	reset_ra_notify_duration();
	reset_ra_progress_duration();
	reset_ra_sort_order();
	for (int i = 0; i < ra_settings_page.item_count; i++)
		settings_item_sync(&ra_settings_page.items[i]);
}

// ============================================
// Menu construction (Settings sub-page only — the main page is
// custom-rendered, see render_main_screen())
// ============================================
static void build_menu_tree(void) {
	int idx = 0;
	ra_settings_items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Enable achievements", "Enable RetroAchievements integration",
		on_off_labels, 2, on_off_values, get_ra_enable, set_ra_enable, reset_ra_enable);
	ra_settings_items[idx++] = (SettingItem)ITEM_TEXT_INPUT_INIT(
		"Username", "RetroAchievements username",
		get_ra_username_display, on_ra_username_set);
	ra_settings_items[idx++] = (SettingItem)ITEM_TEXT_INPUT_INIT(
		"Password", "RetroAchievements password",
		get_ra_password_display, on_ra_password_set);
	ra_settings_items[idx++] = (SettingItem)ITEM_BUTTON_INIT(
		"Authenticate", "Test credentials and retrieve API token",
		on_ra_authenticate);
	ra_settings_items[idx++] = (SettingItem)ITEM_STATIC_INIT(
		"Status", "Authentication status",
		get_ra_status);
	ra_settings_items[idx++] = (SettingItem)ITEM_BUTTON_INIT(
		"Download all game data", "Cache achievement data for every game in your library",
		on_prefetch);
	ra_settings_items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Show notifications", "Show achievement unlock notifications",
		on_off_labels, 2, on_off_values, get_ra_show_notifications, set_ra_show_notifications, reset_ra_show_notifications);
	ra_settings_items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Notification duration", "How long achievement notifications stay on screen",
		notify_duration_labels, NOTIFY_DURATION_COUNT, notify_duration_values, get_ra_notify_duration, set_ra_notify_duration, reset_ra_notify_duration);
	ra_settings_items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Progress duration", "Duration for progress updates (top-left). Off to disable.",
		progress_duration_labels, PROGRESS_DURATION_COUNT, progress_duration_values, get_ra_progress_duration, set_ra_progress_duration, reset_ra_progress_duration);
	ra_settings_items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Achievement sort order", "How achievements are sorted in the in-game menu",
		ra_sort_labels, RA_SORT_LABEL_COUNT, ra_sort_values, get_ra_sort_order, set_ra_sort_order, reset_ra_sort_order);
	ra_settings_items[idx++] = (SettingItem)ITEM_BUTTON_INIT(
		"Reset account data", "Clear login and progress. Keeps game data.",
		on_reset_account);
	ra_settings_items[idx++] = (SettingItem)ITEM_BUTTON_INIT(
		"Erase all achievement data", "Remove everything, including games and badges.",
		on_reset_all);
	ra_settings_items[idx++] = (SettingItem)ITEM_BUTTON_INIT(
		"Reset settings to defaults", "Restore the options above to their default values.",
		reset_ra_page);
	init_page(&ra_settings_page, "RetroAchievements | Settings", ra_settings_items, idx, 0);

	for (int i = 0; i < ra_settings_page.item_count; i++)
		settings_item_sync(&ra_settings_page.items[i]);
}

// ============================================
// Custom main screen
// ============================================
#define MAIN_ITEM_COUNT 3

static const char* main_item_labels[MAIN_ITEM_COUNT] = {
	"Achievements", "Sync now", "Settings"};

static const SDL_Color col_header_title = {255, 255, 255, 255};
static const SDL_Color col_header_gray = {180, 180, 180, 255};
static const SDL_Color col_header_pending = {255, 200, 80, 255};

static bool ra_is_authenticated(void) {
	return CFG_getRAAuthenticated() && *CFG_getRAToken();
}

// Non-selectable status header below the menu bar. Recomputed every call so
// it always reflects the latest auth/sync/pending state. Returns the y
// (screen px) where the selectable list should start.
static int render_main_header(SDL_Surface* screen) {
	int x = SCALE1(PADDING + BUTTON_PADDING);
	int y = SCALE1(PADDING + PILL_SIZE + 1);
	bool auth = ra_is_authenticated();

	// Line 1: username, or "Not authenticated"
	const char* line1 = auth ? get_user_display() : "Not authenticated";
	SDL_Surface* s1 = TTF_RenderUTF8_Blended(font.medium, line1, col_header_title);
	if (s1) {
		SDL_BlitSurface(s1, NULL, screen, &(SDL_Rect){x, y});
		SDL_FreeSurface(s1);
	}
	y += TTF_FontHeight(font.medium) + SCALE1(2);

	// Line 2: cached score, prompt to authenticate, or omitted (no cached
	// login yet). Slot height is reserved either way so line 3 never shifts.
	char line2_buf[64];
	const char* line2 = NULL;
	if (!auth) {
		line2 = "Set credentials in Settings";
	} else {
		uint32_t score = 0, soft = 0;
		if (RAT_getCachedScore(&score, &soft)) {
			// nx-redux is softcore-only (hardcore removed), so the hardcore
			// total would read 0 forever - show the real (softcore) points
			snprintf(line2_buf, sizeof(line2_buf), "%u point%s", soft, soft == 1 ? "" : "s");
			line2 = line2_buf;
		}
	}
	if (line2) {
		SDL_Surface* s2 = TTF_RenderUTF8_Blended(font.small, line2, col_header_gray);
		if (s2) {
			SDL_BlitSurface(s2, NULL, screen, &(SDL_Rect){x, y});
			SDL_FreeSurface(s2);
		}
	}
	y += TTF_FontHeight(font.small) + SCALE1(2);

	// Line 3: pending unlocks, highlighted yellow when unlocks are waiting.
	int n = RA_Offline_pendingCount();
	SDL_Surface* s3 = TTF_RenderUTF8_Blended(font.small, get_pending_display(),
											 n > 0 ? col_header_pending : col_header_gray);
	if (s3) {
		SDL_BlitSurface(s3, NULL, screen, &(SDL_Rect){x, y});
		SDL_FreeSurface(s3);
	}
	y += TTF_FontHeight(font.small) + SCALE1(2);

	// Line 4: last sync time.
	char line4_buf[96];
	snprintf(line4_buf, sizeof(line4_buf), "Last sync: %s", get_lastsync_display());
	SDL_Surface* s4 = TTF_RenderUTF8_Blended(font.small, line4_buf, col_header_gray);
	if (s4) {
		SDL_BlitSurface(s4, NULL, screen, &(SDL_Rect){x, y});
		SDL_FreeSurface(s4);
	}
	y += TTF_FontHeight(font.small) + SCALE1(14);

	return y;
}

// Selectable list of the 3 main actions, drawn with the shared pill list
// rendering (same idiom as nxredux's game list).
static void render_main_list(SDL_Surface* screen, int list_top, int selected) {
	ListLayout layout = {
		.item_h = SCALE1(PILL_SIZE),
		.max_width = screen->w - SCALE1(PADDING * 2),
	};

	for (int i = 0; i < MAIN_ITEM_COUNT; i++) {
		char truncated[256];
		int y = list_top + i * layout.item_h;
		ListItemPos pos = UI_renderListItemPill(screen, &layout, font.large,
												main_item_labels[i], truncated,
												y, i == selected, 0);
		int text_width = pos.pill_width - SCALE1(BUTTON_PADDING * 2);
		UI_renderListItemText(screen, NULL, main_item_labels[i], font.large,
							  pos.text_x, pos.text_y, text_width, i == selected);
	}
}

static void render_main_screen(SDL_Surface* screen, int selected) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, "RetroAchievements");

	int list_top = render_main_header(screen);
	render_main_list(screen, list_top, selected);

	UI_renderButtonHintBar(screen, (char*[]){"B", "EXIT", "A", "OPEN", NULL});
}

// Run the settings_menu framework for ra_settings_page only, then return to
// the custom main screen (does NOT exit the app). settings_menu_handle_input
// pops the page and reports *quit=true once stack depth reaches 0 — since
// ra_settings_page has no submenu items, a single B press at depth 1 always
// triggers exactly that. We feed it a *local* quit flag so that report only
// breaks this sub-loop instead of the whole app.
static void run_settings_menu(SDL_Surface** screen_ptr) {
	settings_menu_push(&ra_settings_page);

	bool sub_quit = false;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!sub_quit) {
		GFX_startFrame();
		PAD_poll();

		settings_menu_handle_input(&sub_quit, &dirty);

		// TG5050: keyboard may have triggered display recovery
		{
			SDL_Surface* ns = DisplayHelper_getReinitScreen();
			if (ns) {
				*screen_ptr = ns;
				g_screen = ns;
				dirty = true;
			}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (dirty) {
			GFX_clear(*screen_ptr);
			settings_menu_render(*screen_ptr, show_setting);
			GFX_flip(*screen_ptr);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

// ============================================
// Main
// ============================================
int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	SDL_Surface* screen = GFX_init(MODE_MAIN);
	g_screen = screen;
	UI_showSplashScreen(screen, "RetroAchievements");

	InitSettings();
	PWR_init();
	PAD_init();

	RA_Offline_init(RA_ROOT_DIR);

	build_menu_tree();
	settings_menu_init();

	int selected = 0;
	bool quit = false;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		UI_handleQuitRequest(screen, &quit, &dirty, "Exit RetroAchievements?",
							 "Your settings are automatically saved");

		if (!quit) {
			if (PAD_justRepeated(BTN_DOWN)) {
				selected = (selected + 1) % MAIN_ITEM_COUNT;
				dirty = true;
			}
			if (PAD_justRepeated(BTN_UP)) {
				selected = (selected - 1 + MAIN_ITEM_COUNT) % MAIN_ITEM_COUNT;
				dirty = true;
			}
			if (PAD_justPressed(BTN_B)) {
				quit = true;
			}
			if (PAD_justPressed(BTN_A)) {
				switch (selected) {
				case 0:
					RATBrowser_run(g_screen);
					break;
				case 1:
					RATSync_run(g_screen);
					break;
				case 2:
					run_settings_menu(&screen);
					break;
				}
				// These are blocking sub-screens with their own input loops;
				// the B (or A) press that closed them is still latched in
				// the global pad state (no PAD_poll happened since). Clear
				// it so it isn't misread this frame — same convention as
				// settings.c/settings_wifi.c after modal transitions.
				PAD_reset();
				dirty = true;
			}
		}

		// TG5050: keyboard may have triggered display recovery
		{
			SDL_Surface* ns = DisplayHelper_getReinitScreen();
			if (ns) {
				screen = ns;
				g_screen = ns;
				dirty = true;
			}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (dirty) {
			render_main_screen(screen, selected);
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	GFX_clear(screen);
	GFX_flip(screen);

	QuitSettings();
	PWR_quit();
	PAD_quit();

	GFX_quit();

	return EXIT_SUCCESS;
}
