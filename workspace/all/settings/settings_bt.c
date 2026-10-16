#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "settings_bt.h"
#include "defines.h"
#include "api.h"
#include "ui_accent.h"
#include "ui_list.h"

// ============================================
// BT Device Info (attached to user_data)
// ============================================

typedef struct {
	char name[249];
	char addr[18];
	BluetoothDeviceType device_type;
	int paired;
	int bonded;
	int connected;
} BtDeviceInfo;

// ============================================
// Static items
// ============================================

#define BT_STATIC_COUNT 4
#define BT_IDX_SCAN 3
#define BT_IDX_TOGGLE 0
#define BT_IDX_DIAG 1
#define BT_IDX_BOOT 2

static const char* on_off_labels[] = {"Off", "On"};

// ============================================
// Scanner thread state
// ============================================

static pthread_t bt_scanner_thread;
static volatile int bt_scanner_running = 0;
static volatile int bt_needs_refresh = 0;
static int bt_scanner_started = 0;
static SettingsPage* bt_page_ref = NULL;
// Gate starts against page exit; the inventory worker may still be finishing
// a query after the page is popped, but cannot start another radio session.
static pthread_mutex_t bt_scan_lifecycle = PTHREAD_MUTEX_INITIALIZER;
static int bt_scan_requested;
static int bt_scanner_failed;

// ============================================
// BT device options submenu
// ============================================

typedef struct {
	SettingsPage page;
	SettingItem items[4];
	BtDeviceInfo dev_info;
} BtDeviceOptions;

// Forward declarations
static void* bt_scanner(void* arg);
static void bt_on_show(SettingsPage* page);
static void bt_on_hide(SettingsPage* page);
static void bt_on_tick(SettingsPage* page);
static void bt_device_draw(SDL_Surface* screen, SettingItem* item,
						   int x, int y, int w, int h, int selected);

// ============================================
// BT toggle (blocking with overlay)
// ============================================

// File-scope context: the worker is detached and can outlive bt_set_toggle()
// (the user may press B to stop waiting before BT_enable returns). Keeping the
// context in static storage — never on the caller's stack — means the thread's
// final writes land in valid memory, and the busy flag stops a second toggle
// from racing the first over the same context.
static struct {
	int val;
	volatile int done;
	volatile int busy;
} bt_toggle_ctx;

// The value is cached because settings_item_sync can run on every UI update.
// A helper process is only used when the page opens or a write completes.
static volatile int bt_boot_value;
static struct {
	int val;
	volatile int done;
	volatile int busy;
	volatile int result;
	int handled;
} bt_boot_ctx;

static void* bt_boot_thread(void* arg) {
	(void)arg;
	bt_boot_ctx.result = BT_bootReconnectEnable(bt_boot_ctx.val ? true : false);
	bt_boot_value = BT_bootReconnectEnabled() ? 1 : 0;
	bt_boot_ctx.done = 1;
	bt_boot_ctx.busy = 0;
	return NULL;
}

static int bt_get_boot(void) {
	return bt_boot_value;
}

static void bt_set_boot(int val);

static void* bt_toggle_thread(void* arg) {
	(void)arg;
	BT_enable(bt_toggle_ctx.val ? true : false);
	pthread_mutex_lock(&bt_scan_lifecycle);
	if (bt_page_ref && bt_toggle_ctx.val && BT_enabled()) {
		bt_scan_requested = 1;
		bt_needs_refresh = 1;
	}
	pthread_mutex_unlock(&bt_scan_lifecycle);
	bt_toggle_ctx.done = 1;
	bt_toggle_ctx.busy = 0;
	return NULL;
}

static int bt_get_toggle(void) {
	return BT_enabled() ? 1 : 0;
}

static void bt_set_toggle(int val) {
	SettingsPage* page = settings_menu_current();
	if (!page || !page->screen)
		return;
	if (bt_toggle_ctx.busy || bt_boot_ctx.busy) // a settings write is still running
		return;

	bt_toggle_ctx.val = val;

	const char* title = val ? "Enabling Bluetooth..." : "Disabling Bluetooth...";
	settings_run_async(page->screen, title, bt_toggle_thread, NULL,
					   &bt_toggle_ctx.done, &bt_toggle_ctx.busy);

	// Re-sync with actual hardware state
	settings_item_sync(&page->items[BT_IDX_TOGGLE]);
}

static int bt_get_diag(void) {
	return BT_diagnosticsEnabled() ? 1 : 0;
}

static void bt_set_diag(int val) {
	BT_diagnosticsEnable(val ? true : false);
}

// Scan again queues one request; cached inventory refreshes never queue scans.
static void bt_scan_again(void) {
	pthread_mutex_lock(&bt_scan_lifecycle);
	int state = BT_discoveryState();
	if (bt_page_ref && BT_enabled() && !bt_scan_requested && !bt_toggle_ctx.busy &&
		state != BT_DISCOVERY_STARTING && state != BT_DISCOVERY_ACTIVE) {
		if (bt_scanner_failed) {
			bt_scanner_running = 1;
			if (pthread_create(&bt_scanner_thread, NULL, bt_scanner, bt_page_ref) != 0) {
				bt_scanner_running = 0;
				pthread_mutex_unlock(&bt_scan_lifecycle);
				return;
			}
			bt_scanner_started = 1;
			bt_scanner_failed = 0;
		}
		bt_scan_requested = 1;
		bt_needs_refresh = 1;
	}
	pthread_mutex_unlock(&bt_scan_lifecycle);
}

// ============================================
// Device action callbacks (run blocking BT ops off UI thread)
// ============================================

static BtDeviceOptions* active_bt_options = NULL;

typedef struct {
	void (*action)(char* addr);
	char addr[18];
	volatile int done;
	volatile int busy;
} BtActionCtx;

// Static, not stack-allocated: bt_run_action may return (user presses B) while
// the blocking BT op is still running in the detached worker, so the context it
// writes on completion must remain valid. The busy flag rejects a second action
// until the first worker has finished with the shared context.
static BtActionCtx bt_action_ctx;
static volatile int bt_pair_result = BT_PAIR_OK;

static void bt_set_boot(int val) {
	SettingsPage* page = settings_menu_current();
	if (!page || !page->screen || bt_boot_ctx.busy || bt_toggle_ctx.busy || bt_action_ctx.busy)
		return;
	bt_boot_ctx.val = val;
	bt_boot_ctx.handled = 0;
	bt_action_ctx.done = 0;
	settings_run_async(page->screen, val ? "Enabling boot reconnect..." : "Disabling boot reconnect...",
		bt_boot_thread, NULL, &bt_boot_ctx.done, &bt_boot_ctx.busy);
	if (!bt_boot_ctx.busy && !bt_boot_ctx.done) {
		bt_boot_ctx.result = -1;
		bt_boot_ctx.done = 1;
	}
	settings_item_sync(&page->items[BT_IDX_BOOT]);
}

static void bt_pair_worker(char* addr) {
	bt_pair_result = BT_pair(addr);
}

static void* bt_action_thread(void* arg) {
	(void)arg;
	bt_action_ctx.action(bt_action_ctx.addr);
	bt_action_ctx.done = 1;
	bt_action_ctx.busy = 0;
	return NULL;
}

static void bt_run_action(void (*action)(char*), const char* addr, const char* msg) {
	if (!bt_page_ref || !bt_page_ref->screen)
		return;

	if (bt_action_ctx.busy || bt_boot_ctx.busy) // a previous action is still running
		return;

	bt_pair_result = BT_PAIR_OK;
	bt_boot_ctx.done = 0;
	bt_action_ctx.action = action;
	bt_action_ctx.addr[0] = '\0';
	strncpy(bt_action_ctx.addr, addr, sizeof(bt_action_ctx.addr) - 1);
	bt_action_ctx.addr[sizeof(bt_action_ctx.addr) - 1] = '\0';

	// Use the owning bt page's screen, not settings_menu_current(): this runs
	// from the pushed device-options submenu, whose own screen is unset.
	settings_run_async(bt_page_ref->screen, msg, bt_action_thread, NULL,
					   &bt_action_ctx.done, &bt_action_ctx.busy);

	bt_needs_refresh = 1;
	settings_menu_pop();
}

static void bt_action_connect(void) {
	if (!active_bt_options)
		return;
	bt_run_action(BT_connect, active_bt_options->dev_info.addr, "Connecting...");
}

static void bt_action_disconnect(void) {
	if (!active_bt_options)
		return;
	bt_run_action(BT_disconnect, active_bt_options->dev_info.addr, "Disconnecting...");
}

static void bt_action_pair(void) {
	if (!active_bt_options)
		return;
	bt_run_action(bt_pair_worker, active_bt_options->dev_info.addr, "Pairing...");
}

static void bt_action_unpair(void) {
	if (!active_bt_options)
		return;
	bt_run_action(BT_unpair, active_bt_options->dev_info.addr, "Unpairing...");
}

// ============================================
// Build device options submenu
// ============================================

static void build_bt_device_options(BtDeviceOptions* opts, BtDeviceInfo* info) {
	memset(opts, 0, sizeof(*opts));
	opts->dev_info = *info;
	opts->page.title = opts->dev_info.name;
	opts->page.items = opts->items;
	opts->page.is_list = 0;
	opts->page.dynamic_start = -1;

	int idx = 0;

	if (info->paired) {
		if (info->connected) {
			opts->items[idx] = (SettingItem){
				.name = "Disconnect",
				.desc = "Disconnect from this device",
				.type = ITEM_BUTTON,
				.visible = 1,
				.on_press = bt_action_disconnect,
			};
			idx++;
		} else {
			opts->items[idx] = (SettingItem){
				.name = "Connect",
				.desc = "Connect to this paired device",
				.type = ITEM_BUTTON,
				.visible = 1,
				.on_press = bt_action_connect,
			};
			idx++;
		}

		opts->items[idx] = (SettingItem){
			.name = "Unpair",
			.desc = "Remove pairing with this device",
			.type = ITEM_BUTTON,
			.visible = 1,
			.on_press = bt_action_unpair,
		};
		idx++;
	} else {
		opts->items[idx] = (SettingItem){
			.name = "Pair",
			.desc = "Pair with this device",
			.type = ITEM_BUTTON,
			.visible = 1,
			.on_press = bt_action_pair,
		};
		idx++;
	}

	opts->page.item_count = idx;
}

// ============================================
// Device list item: open options on A press
// ============================================

#define MAX_BT_OPTIONS 32
static BtDeviceOptions bt_options_pool[MAX_BT_OPTIONS];
static int bt_options_used = 0;

static void bt_device_press(void) {
	SettingsPage* page = settings_menu_current();
	if (!page)
		return;

	// Copy the device info out under the page lock: the scanner thread
	// rebuilds page->items[] and device_info_pool under the wrlock, and
	// this handler runs after the menu loop released its read lock.
	BtDeviceInfo info;
	int have_info = 0;
	pthread_rwlock_rdlock(&page->lock);
	SettingItem* sel = settings_page_visible_item(page, page->selected);
	if (sel && sel->user_data) {
		info = *(BtDeviceInfo*)sel->user_data;
		have_info = 1;
	}
	pthread_rwlock_unlock(&page->lock);
	if (!have_info)
		return;

	if (bt_options_used >= MAX_BT_OPTIONS)
		bt_options_used = 0;
	BtDeviceOptions* opts = &bt_options_pool[bt_options_used++];
	active_bt_options = opts;

	build_bt_device_options(opts, &info);
	settings_menu_push(&opts->page);
}

// ============================================
// Custom draw for device items
// ============================================

static void bt_device_draw(SDL_Surface* screen, SettingItem* item,
						   int x, int y, int w, int h, int selected) {
	BtDeviceInfo* info = (BtDeviceInfo*)item->user_data;
	if (!info)
		return;

	TTF_Font* f = font.small;
	SDL_Color text_color = UI_getListTextColor(selected);

	// Status label
	const char* status = info->connected ? "Connected"
						 : info->bonded	 ? "Paired"
										 : "Available";

	// Device name (truncate first so we know the label width for pills)
	char truncated[256];
	int max_text_w = w - SCALE1(BUTTON_PADDING * 2) - SCALE1(48); // room for status + icons
	const char* display_name = info->name[0] ? info->name : info->addr;
	GFX_truncateText(f, display_name, truncated, max_text_w, 0);

	// Draw 2-layer selection pill
	if (selected) {
		// Layer 1: full-width background
		SDL_Rect row_rect = {x, y, w, h};
		GFX_blitPillColor(ASSET_BUTTON, screen, &row_rect, THEME_COLOR2, RGB_WHITE);

		// Layer 2: label-width pill on top
		int text_w_px, text_h_px;
		GFX_measureText(f, truncated, &text_w_px, &text_h_px);
		int label_pill_w = text_w_px + SCALE1(BUTTON_PADDING * 2);
		SDL_Rect label_rect = {x, y, label_pill_w, h};
		GFX_blitPillColor(ASSET_BUTTON, screen, &label_rect, UI_accentMapped(screen->format), RGB_WHITE);
	}

	// Device name text
	int text_x = x + SCALE1(BUTTON_PADDING);
	int text_y = y + (h - TTF_FontHeight(f)) / 2;

	SDL_Surface* text_surf = GFX_renderText(f, truncated, text_color);
	if (text_surf) {
		SDL_BlitSurface(text_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
		SDL_FreeSurface(text_surf);
	}

	// Right side: status label + icons
	int right_x = x + w - SCALE1(BUTTON_PADDING);

	// Status label (right-aligned, like cycle values)
	SDL_Color status_color = selected ? COLOR_WHITE : UI_getListTextColor(0);
	int val_text_y = y + (h - TTF_FontHeight(font.tiny)) / 2;
	SDL_Surface* val_surf = GFX_renderText(font.tiny, status, status_color);
	if (val_surf) {
		right_x -= val_surf->w;
		SDL_BlitSurface(val_surf, NULL, screen, &(SDL_Rect){right_x, val_text_y, 0, 0});
		SDL_FreeSurface(val_surf);
		right_x -= SCALE1(4);
	}

	// Connected checkmark
	if (info->connected) {
		SDL_Rect icon_rect;
		GFX_assetRect(ASSET_CHECKCIRCLE, &icon_rect);
		right_x -= icon_rect.w;
		GFX_blitAsset(ASSET_CHECKCIRCLE, NULL, screen,
					  &(SDL_Rect){right_x, y + (h - icon_rect.h) / 2, 0, 0});
		right_x -= SCALE1(4);
	}

	// Device type icon (audio or controller)
	int type_asset = (info->device_type == BLUETOOTH_AUDIO) ? ASSET_AUDIO : ASSET_CONTROLLER;
	if (info->device_type != BLUETOOTH_NONE) {
		SDL_Rect type_rect;
		GFX_assetRect(type_asset, &type_rect);
		right_x -= type_rect.w;
		GFX_blitAsset(type_asset, NULL, screen,
					  &(SDL_Rect){right_x, y + (h - type_rect.h) / 2, 0, 0});
	}
}

// ============================================
// Scanner Thread
// ============================================

// Interruptible sleep: returns early if bt_scanner_running becomes 0 or bt_needs_refresh is set
static void bt_sleep(int seconds) {
	settings_scanner_sleep(seconds, &bt_scanner_running, &bt_needs_refresh);
}

static BtDeviceInfo device_info_pool[BT_MAX_ITEMS];
static int device_info_count = 0;

static void* bt_scanner(void* arg) {
	SettingsPage* page = (SettingsPage*)arg;

	while (bt_scanner_running) {
		bt_needs_refresh = 0;
		if (!BT_enabled()) {
			// BT disabled - clear dynamic items
			pthread_rwlock_wrlock(&page->lock);
			if (page->dynamic_start >= 0)
				page->item_count = page->dynamic_start;
			device_info_count = 0;
			page->needs_layout = 1;
			pthread_rwlock_unlock(&page->lock);
			pthread_mutex_lock(&bt_scan_lifecycle);
			bt_scan_requested = 1; // One scan when Bluetooth is enabled again.
			pthread_mutex_unlock(&bt_scan_lifecycle);
			bt_sleep(2);
			continue;
		}

		pthread_mutex_lock(&bt_scan_lifecycle);
		if (bt_scanner_running && bt_scan_requested && !bt_toggle_ctx.busy) {
			bt_scan_requested = 0;
			BT_discovery(1);
		}
		pthread_mutex_unlock(&bt_scan_lifecycle);
		if (!bt_scanner_running)
			break;

		// Get paired devices
		struct BT_devicePaired paired[32];
		int paired_count = BT_pairedDevices(paired, 32);
		if (!bt_scanner_running)
			break;

		// Get available (unpaired) devices
		struct BT_device available[32];
		int avail_count = BT_availableDevices(available, 32);
		if (!bt_scanner_running)
			break;

		// Save selected addr for restoration
		char selected_addr[18] = "";
		pthread_rwlock_rdlock(&page->lock);
		if (page->dynamic_start >= 0) {
			SettingItem* sel = settings_page_visible_item(page, page->selected);
			if (sel && sel->user_data) {
				BtDeviceInfo* info = (BtDeviceInfo*)sel->user_data;
				strncpy(selected_addr, info->addr, sizeof(selected_addr) - 1);
			}
		}
		pthread_rwlock_unlock(&page->lock);

		// Rebuild dynamic items
		pthread_rwlock_wrlock(&page->lock);

		int dyn_start = page->dynamic_start;
		if (dyn_start < 0)
			dyn_start = BT_STATIC_COUNT;

		device_info_count = 0;
		int item_idx = dyn_start;

		// Paired devices first
		for (int i = 0; i < paired_count && item_idx < page->max_items && device_info_count < BT_MAX_ITEMS; i++) {
			BtDeviceInfo* dinfo = &device_info_pool[device_info_count++];
			strncpy(dinfo->name, paired[i].remote_name, sizeof(dinfo->name) - 1);
			strncpy(dinfo->addr, paired[i].remote_addr, sizeof(dinfo->addr) - 1);
			dinfo->paired = 1;
			dinfo->bonded = paired[i].is_bonded;
			dinfo->connected = paired[i].is_connected;

			dinfo->device_type = BLUETOOTH_NONE;

			page->items[item_idx] = (SettingItem){
				.name = dinfo->name[0] ? dinfo->name : dinfo->addr,
				.type = ITEM_BUTTON,
				.visible = 1,
				.on_press = bt_device_press,
				.custom_draw = bt_device_draw,
				.user_data = dinfo,
			};
			item_idx++;
		}

		// Available (unpaired) devices
		for (int i = 0; i < avail_count && item_idx < page->max_items && device_info_count < BT_MAX_ITEMS; i++) {
			// Skip if already in paired list
			int already_paired = 0;
			for (int j = 0; j < paired_count; j++) {
				if (strcmp(paired[j].remote_addr, available[i].addr) == 0) {
					already_paired = 1;
					break;
				}
			}
			if (already_paired)
				continue;

			BtDeviceInfo* dinfo = &device_info_pool[device_info_count++];
			strncpy(dinfo->name, available[i].name, sizeof(dinfo->name) - 1);
			strncpy(dinfo->addr, available[i].addr, sizeof(dinfo->addr) - 1);
			dinfo->paired = 0;
			dinfo->bonded = 0;
			dinfo->connected = 0;
			dinfo->device_type = available[i].kind;

			page->items[item_idx] = (SettingItem){
				.name = dinfo->name[0] ? dinfo->name : dinfo->addr,
				.type = ITEM_BUTTON,
				.visible = 1,
				.on_press = bt_device_press,
				.custom_draw = bt_device_draw,
				.user_data = dinfo,
			};
			item_idx++;
		}

		page->item_count = item_idx;

		// Restore selection by addr
		if (selected_addr[0]) {
			for (int i = dyn_start; i < item_idx; i++) {
				BtDeviceInfo* dinfo = (BtDeviceInfo*)page->items[i].user_data;
				if (dinfo && strcmp(dinfo->addr, selected_addr) == 0) {
					page->selected = settings_page_actual_to_visible(page, i);
					break;
				}
			}
		}

		page->needs_layout = 1;
		pthread_rwlock_unlock(&page->lock);

		bt_sleep(2);
	}

	return NULL;
}

// ============================================
// Page Lifecycle
// ============================================

static void bt_on_show(SettingsPage* page) {
	BT_init();
	if (!bt_boot_ctx.busy)
		bt_boot_value = BT_bootReconnectEnabled() ? 1 : 0;

	// Join previous scanner if it was started
	if (bt_scanner_started) {
		pthread_join(bt_scanner_thread, NULL);
		bt_scanner_started = 0;
	}

	// Sync static items
	settings_item_sync(&page->items[BT_IDX_TOGGLE]);
	settings_item_sync(&page->items[BT_IDX_DIAG]);
	settings_item_sync(&page->items[BT_IDX_BOOT]);

	// Start scanner thread
	pthread_mutex_lock(&bt_scan_lifecycle);
	bt_page_ref = page;
	bt_scan_requested = 1;
	bt_scanner_failed = 0;
	bt_scanner_running = 1;
	if (pthread_create(&bt_scanner_thread, NULL, bt_scanner, page) == 0)
		bt_scanner_started = 1;
	else {
		bt_scanner_running = 0;
		bt_scanner_failed = 1;
		bt_scan_requested = 0;
	}
	pthread_mutex_unlock(&bt_scan_lifecycle);
}

static void bt_on_hide(SettingsPage* page) {
	(void)page;
	pthread_mutex_lock(&bt_scan_lifecycle);
	bt_scanner_running = 0;
	bt_scan_requested = 0;
	// Stop the owned radio client promptly; do not join inventory queries here.
	BT_discovery(0);
	bt_page_ref = NULL;
	pthread_mutex_unlock(&bt_scan_lifecycle);
}

static void bt_on_tick(SettingsPage* page) {
	// Routine progress belongs in the below-list status area. Promote only
	// errors and no-device guidance into the selected row's wrapped help.
	page->status_in_description = 0;
	if (bt_boot_ctx.done && !bt_boot_ctx.handled) {
		settings_item_sync(&page->items[BT_IDX_BOOT]);
		bt_boot_ctx.handled = 1;
	}
	int state = BT_discoveryState();
	pthread_mutex_lock(&bt_scan_lifecycle);
	int pending = bt_scan_requested;
	pthread_mutex_unlock(&bt_scan_lifecycle);
	bool enabled = BT_enabled();
	bool scanning = state == BT_DISCOVERY_ACTIVE;
	bool waiting = pending || state == BT_DISCOVERY_STARTING;
	SettingItem* scan = &page->items[BT_IDX_SCAN];
	int show_scan = enabled && !waiting && !scanning && !bt_toggle_ctx.busy;
	if (scan->visible != show_scan) {
		if (scan->visible) {
			int scan_idx = settings_page_actual_to_visible(page, BT_IDX_SCAN);
			if (page->selected >= scan_idx)
				page->selected = page->selected == scan_idx ? scan_idx - 1 : page->selected - 1;
			scan->visible = 0;
		} else {
			scan->visible = 1;
			int scan_idx = settings_page_actual_to_visible(page, BT_IDX_SCAN);
			if (page->selected >= scan_idx)
				page->selected++;
		}
	}
	// The scanner may have removed devices while the scan row changed visibility.
	if (page->needs_layout) {
		page->needs_layout = 0;
		int vis = settings_page_visible_count(page);
		if (page->selected >= vis && vis > 0)
			page->selected = vis - 1;
	}

	// Pairing failures take priority over discovery progress and completion.
	if (bt_action_ctx.done && bt_pair_result != BT_PAIR_OK) {
		page->status_in_description = 1;
		page->status_msg = bt_pair_result == BT_PAIR_FAILED
			? "Pairing failed. Put device in pairing mode and retry."
			: bt_pair_result == BT_PAIR_TRUST_FAILED
			? "Paired, but automatic reconnection setup failed."
			: "Paired, but connection failed. Retry Connect.";
	} else if (bt_boot_ctx.done && bt_boot_ctx.result != 0) {
		page->status_in_description = 1;
		page->status_msg = "Could not save boot reconnect setting. Retry.";
	} else if (!enabled) {
		page->status_msg = NULL;
	} else if (scanning) {
		page->status_msg = "Scanning for devices...";
	} else if (waiting) {
		page->status_msg = "Starting scan...";
	} else if (bt_scanner_failed || state == BT_DISCOVERY_FAILED) {
		page->status_in_description = 1;
		page->status_msg = "Could not start scan. Select Scan again to retry.";
	} else if (state == BT_DISCOVERY_INTERRUPTED || state == BT_DISCOVERY_STOPPED) {
		page->status_in_description = 1;
		page->status_msg = "Scan stopped early. Select Scan again to retry.";
	} else if (state == BT_DISCOVERY_COMPLETE) {
		if (page->item_count > page->dynamic_start) {
			page->status_msg = "Scan complete";
		} else {
			page->status_in_description = 1;
			page->status_msg = "No devices found. Put your device in pairing mode and scan again.";
		}
	} else {
		page->status_msg = NULL;
	}
}

// ============================================
// Page Creation
// ============================================

SettingsPage* bt_page_create(void) {
	SettingsPage* page = calloc(1, sizeof(SettingsPage));
	if (!page)
		return NULL;
	page->title = "Settings | Bluetooth";
	page->is_list = 0;
	page->dynamic_start = BT_STATIC_COUNT;
	page->max_items = BT_MAX_ITEMS;
	page->items = calloc(BT_MAX_ITEMS, sizeof(SettingItem));
	if (!page->items) {
		free(page);
		return NULL;
	}

	settings_page_init_lock(page);

	// BT toggle
	page->items[BT_IDX_TOGGLE] = (SettingItem){
		.name = "Bluetooth",
		.desc = "Enable or disable Bluetooth",
		.type = ITEM_CYCLE,
		.visible = 1,
		.labels = on_off_labels,
		.label_count = 2,
		.get_value = bt_get_toggle,
		.set_value = bt_set_toggle,
	};

	// BT diagnostics
	page->items[BT_IDX_DIAG] = (SettingItem){
		.name = "Bluetooth diagnostics",
		.desc = "Enable Bluetooth diagnostic logging",
		.type = ITEM_CYCLE,
		.visible = 1,
		.labels = on_off_labels,
		.label_count = 2,
		.get_value = bt_get_diag,
		.set_value = bt_set_diag,
	};

	page->items[BT_IDX_BOOT] = (SettingItem){
		.name = "Reconnect audio on boot",
		.desc = "Reconnect the last eligible headset on the next boot",
		.type = ITEM_CYCLE,
		.visible = BT_bootReconnectSupported(),
		.labels = on_off_labels,
		.label_count = 2,
		.get_value = bt_get_boot,
		.set_value = bt_set_boot,
	};

	page->items[BT_IDX_SCAN] = (SettingItem){
		.name = "Scan again",
		.desc = "Put your device in pairing mode, then scan again",
		.type = ITEM_BUTTON,
		.visible = 0,
		.on_press = bt_scan_again,
	};

	page->item_count = BT_STATIC_COUNT;

	page->on_show = bt_on_show;
	page->on_hide = bt_on_hide;
	page->on_tick = bt_on_tick;

	return page;
}

void bt_page_destroy(SettingsPage* page) {
	if (!page)
		return;
	bt_on_hide(page);
	// Inventory worker finishes without holding up page exit.
}
