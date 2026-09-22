/*
 * settings_developer.c - Developer settings for NxRedux Settings
 *
 * Provides developer-oriented options: disable sleep, SSH toggle,
 * SSH on boot and debug logging within the settings framework.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "settings_developer.h"
#include "defines.h"
#include "api.h"
#include "utils.h"
#include "config.h"
#include "ui_loadingoverlay.h"
#include "ssh_desc.h"

// ============================================
// Developer settings page
// ============================================

#define DEV_ITEM_COUNT 6
#define DEV_IDX_DISABLE_SLEEP 0
#define DEV_IDX_KEEP_AWAKE_USB 1
#define DEV_IDX_SSH_TOGGLE 2
#define DEV_IDX_SSH_ON_BOOT 3
#define DEV_IDX_DEBUG_LOGGING 4
#define DEV_IDX_CLEAN_DOTFILES 5

static const char* on_off_labels[] = {"Off", "On"};
static int on_off_values[] = {0, 1};

// Platform stored for SSH password display
static DevicePlatform current_platform = PLAT_UNKNOWN;

// Track SSH runtime state (not persisted)
static int ssh_running = 0;

// Background WiFi-IP poller for the SSH login hint. WIFI_connectionInfo() shells
// out to wpa_cli + ip (tens of ms), so it must never run on the UI thread per
// frame; a detached-style long-lived thread refreshes ssh_ip every ~3s while the
// page is shown. The UI thread only ever snapshots ssh_ip under the mutex.
static char ssh_ip[32];
static pthread_mutex_t ssh_ip_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t ssh_ip_thread;
static volatile int ssh_ip_running = 0;
static int ssh_ip_started = 0;

// Defined below (after the poller); dev_set_ssh refreshes the hint via it.
static const char* dev_get_ssh_desc(void);

// ============================================
// Disable sleep
// ============================================

static int dev_get_disable_sleep(void) {
	return CFG_getDisableSleep() ? 1 : 0;
}

static void dev_set_disable_sleep(int v) {
	CFG_setDisableSleep(v != 0);
}

static void dev_reset_disable_sleep(void) {
	CFG_setDisableSleep(CFG_DEFAULT_DISABLE_SLEEP);
}

// ============================================
// Keep awake over USB
// ============================================

static int dev_get_keep_awake_usb(void) {
	return CFG_getKeepAwakeUSB() ? 1 : 0;
}

static void dev_set_keep_awake_usb(int v) {
	CFG_setKeepAwakeUSB(v != 0);
}

static void dev_reset_keep_awake_usb(void) {
	CFG_setKeepAwakeUSB(CFG_DEFAULT_KEEP_AWAKE_USB);
}

// ============================================
// Enable SSH (runtime toggle with overlay)
// ============================================

static int dev_ssh_check_running(void) {
	// Check if sshd process is running
	int ret = system("pidof sshd > /dev/null 2>&1");
	ssh_running = (ret == 0) ? 1 : 0;
	return ssh_running;
}

static void* ssh_toggle_thread(void* arg) {
	struct {
		int val;
		volatile int* done;
	}* ctx = arg;

	if (ctx->val) {
		// Try both init script names (tg5040 uses sshd, tg5050 uses S50sshd)
		system("/etc/init.d/sshd start > /dev/null 2>&1 || /etc/init.d/S50sshd start > /dev/null 2>&1");
	} else {
		system("/etc/init.d/sshd stop > /dev/null 2>&1 || /etc/init.d/S50sshd stop > /dev/null 2>&1");
	}
	*ctx->done = 1;
	return NULL;
}

static int dev_get_ssh(void) {
	return dev_ssh_check_running();
}

static void dev_set_ssh(int val) {
	SettingsPage* page = settings_menu_current();
	if (!page || !page->screen)
		return;

	volatile int done = 0;
	struct {
		int val;
		volatile int* done;
	} ctx = {val, &done};

	pthread_t t;
	pthread_create(&t, NULL, ssh_toggle_thread, &ctx);
	pthread_detach(t);

	const char* title = val ? "Starting SSH..." : "Stopping SSH...";

	while (!done) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_B))
			break;

		GFX_clear(page->screen);
		settings_menu_render(page->screen, 0);
		UI_renderLoadingOverlay(page->screen, title, "Press B to cancel");
		GFX_flip(page->screen);
	}

	// Update runtime state
	dev_ssh_check_running();

	// Refresh the hint immediately so the login line appears the moment the
	// server comes up (on_tick also does this every frame).
	page->items[DEV_IDX_SSH_TOGGLE].desc = dev_get_ssh_desc();

	// Re-sync the SSH toggle item with actual state
	settings_item_sync(&page->items[DEV_IDX_SSH_TOGGLE]);
}

static void dev_reset_ssh(void) {
	// Reset = stop SSH
	if (ssh_running) {
		dev_set_ssh(0);
	}
}

// ============================================
// SSH on boot
// ============================================

static int dev_get_ssh_on_boot(void) {
	return CFG_getSSHOnBoot() ? 1 : 0;
}

static void dev_set_ssh_on_boot(int v) {
	CFG_setSSHOnBoot(v != 0);
}

static void dev_reset_ssh_on_boot(void) {
	CFG_setSSHOnBoot(CFG_DEFAULT_SSH_ON_BOOT);
}

// ============================================
// Debug logging
// ============================================

static int dev_get_debug_logging(void) {
	return CFG_getDebugLogging() ? 1 : 0;
}

static void dev_set_debug_logging(int v) {
	CFG_setDebugLogging(v != 0);
}

static void dev_reset_debug_logging(void) {
	CFG_setDebugLogging(CFG_DEFAULT_DEBUG_LOGGING);
}

// ============================================
// Clean dot files
// ============================================

static volatile int dotclean_done = 0;
static int dotclean_count = 0;

static void* dotclean_thread(void* arg) {
	(void)arg;
	dotclean_count = 0;

	// Build and run a shell command that finds and deletes macOS dot files
	// Matches: .Spotlight-V100, .apDisk, .fseventsd, .TemporaryItems,
	//          .Trash, .Trashes, ._*, .DS_Store, *_cache[0-9].db, __MACOSX
	// The card root is a runtime value on desktop ($HOME/NXRedux, or
	// NXREDUX_SDCARD), so a home folder with a space or apostrophe must be
	// quoted for the shell like any other user-controlled path.
	char sd_q[MAX_PATH * 4];
	strncpy(sd_q, SDCARD_PATH, sizeof(sd_q) - 1);
	sd_q[sizeof(sd_q) - 1] = '\0';
	escapeSingleQuotes(sd_q, sizeof(sd_q));

	char cmd[MAX_PATH * 4 + 512];
	snprintf(cmd, sizeof(cmd),
			 "cd '%s' && "
			 "{"
			 " find . -maxdepth 1 \\( -name '.Spotlight-V100' -o -name '.apDisk'"
			 " -o -name '.fseventsd' -o -name '.TemporaryItems'"
			 " -o -name '.Trash' -o -name '.Trashes' \\);"
			 " find . -depth -type f \\( -name '._*' -o -name '.DS_Store'"
			 " -o -name '*_cache[0-9].db' \\);"
			 " find . -depth -type d -name '__MACOSX';"
			 "} 2>/dev/null",
			 sd_q);

	FILE* fp = popen(cmd, "r");
	if (fp) {
		char line[1024];
		while (fgets(line, sizeof(line), fp)) {
			// Strip trailing newline
			size_t len = strlen(line);
			if (len > 0 && line[len - 1] == '\n')
				line[len - 1] = '\0';
			if (line[0] == '\0')
				continue;

			// Build full path and remove
			char fullpath[MAX_PATH * 4];
			snprintf(fullpath, sizeof(fullpath), "%s/%s", SDCARD_PATH, line + 2); // skip "./"
			// Use rm -rf for both files and directories. Single-quoted +
			// escaped: "._Link's Awakening.gb" is a perfectly normal name here.
			escapeSingleQuotes(fullpath, sizeof(fullpath));
			char rm_cmd[MAX_PATH * 4 + 16];
			snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", fullpath);
			system(rm_cmd);
			dotclean_count++;
		}
		pclose(fp);
	}

	dotclean_done = 1;
	return NULL;
}

static void dev_clean_dotfiles(void) {
	SettingsPage* page = settings_menu_current();
	if (!page || !page->screen)
		return;

	dotclean_done = 0;
	dotclean_count = 0;

	pthread_t t;
	pthread_create(&t, NULL, dotclean_thread, NULL);
	pthread_detach(t);

	while (!dotclean_done) {
		GFX_startFrame();
		PAD_poll();

		GFX_clear(page->screen);
		settings_menu_render(page->screen, 0);
		UI_renderLoadingOverlay(page->screen, "Cleaning dot files...", NULL);
		GFX_flip(page->screen);
	}

	// Show result
	char msg[128];
	if (dotclean_count == 0) {
		snprintf(msg, sizeof(msg), "Nothing to clean up.");
	} else {
		snprintf(msg, sizeof(msg), "Deleted %d item%s.",
				 dotclean_count, dotclean_count == 1 ? "" : "s");
	}

	// Show result for ~2 seconds
	unsigned long start = SDL_GetTicks();
	while (SDL_GetTicks() - start < 2000) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			break;

		GFX_clear(page->screen);
		settings_menu_render(page->screen, 0);
		UI_renderLoadingOverlay(page->screen, msg, NULL);
		GFX_flip(page->screen);
	}
}

// ============================================
// Background WiFi-IP poller
// ============================================

// One connection lookup; writes the current IP (or "" when disabled/offline)
// into ssh_ip under the mutex. Runs only on the poller thread.
static void ssh_ip_lookup(void) {
	char ip[32] = "";
	if (WIFI_enabled()) {
		struct WIFI_connection conn;
		if (WIFI_connectionInfo(&conn) == 0 && conn.valid)
			snprintf(ip, sizeof(ip), "%s", conn.ip);
	}
	pthread_mutex_lock(&ssh_ip_lock);
	snprintf(ssh_ip, sizeof(ssh_ip), "%s", ip);
	pthread_mutex_unlock(&ssh_ip_lock);
}

static void* ssh_ip_poller(void* arg) {
	(void)arg;
	// WIFI_init (PLAT_wifiInit) is idempotent and cheap (sets diagnostics config
	// + logs); settings_wifi.c calls it in wifi_on_show, so mirror that here in
	// case the Developer page is entered without first visiting Network.
	WIFI_init();
	ssh_ip_lookup(); // immediate first lookup: IP shows within one frame
	while (ssh_ip_running) {
		// Interruptible 3s sleep in 100ms steps: leaving the page stops us in ~100ms.
		settings_scanner_sleep(3, &ssh_ip_running, NULL);
		if (!ssh_ip_running)
			break;
		ssh_ip_lookup();
	}
	return NULL;
}

// ============================================
// Dynamic description for SSH item
// ============================================

static char ssh_desc_buf[128];

static const char* dev_get_ssh_desc(void) {
	// Snapshot the poller's IP under the mutex, then format cheaply (safe to run
	// every frame; no shelling out here).
	char ip[32];
	pthread_mutex_lock(&ssh_ip_lock);
	snprintf(ip, sizeof(ip), "%s", ssh_ip);
	pthread_mutex_unlock(&ssh_ip_lock);

	dev_format_ssh_desc(ssh_desc_buf, sizeof(ssh_desc_buf), ssh_running,
						current_platform == PLAT_TG5050, ip);
	return ssh_desc_buf;
}

// ============================================
// Page lifecycle
// ============================================

static void dev_on_show(SettingsPage* page) {
	// Re-check SSH status when page is shown
	dev_ssh_check_running();
	// Start the background WiFi-IP poller (long-lived while the page is shown)
	if (!ssh_ip_started) {
		ssh_ip_running = 1;
		ssh_ip_started = 1;
		pthread_create(&ssh_ip_thread, NULL, ssh_ip_poller, NULL);
	}
	// Update SSH item description
	if (page->item_count > DEV_IDX_SSH_TOGGLE) {
		page->items[DEV_IDX_SSH_TOGGLE].desc = dev_get_ssh_desc();
	}
	// Sync all items
	for (int i = 0; i < page->item_count; i++) {
		settings_item_sync(&page->items[i]);
	}
}

static void dev_on_hide(SettingsPage* page) {
	(void)page;
	// Stop and join the poller so it doesn't keep shelling out off-page.
	if (ssh_ip_started) {
		ssh_ip_running = 0;
		pthread_join(ssh_ip_thread, NULL);
		ssh_ip_started = 0;
	}
}

static void dev_on_tick(SettingsPage* page) {
	// Update SSH description dynamically
	if (page->item_count > DEV_IDX_SSH_TOGGLE) {
		page->items[DEV_IDX_SSH_TOGGLE].desc = dev_get_ssh_desc();
	}
}

// ============================================
// Page create / destroy
// ============================================

SettingsPage* developer_page_create(DevicePlatform dev_platform) {
	current_platform = dev_platform;

	// Check initial SSH state
	dev_ssh_check_running();

	SettingsPage* page = calloc(1, sizeof(SettingsPage));
	if (!page)
		return NULL;

	SettingItem* items = calloc(DEV_ITEM_COUNT, sizeof(SettingItem));
	if (!items) {
		free(page);
		return NULL;
	}

	int idx = 0;

	items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Disable sleep", "Prevent deep sleep mode. Useful for ADB debugging.",
		on_off_labels, 2, on_off_values,
		dev_get_disable_sleep, dev_set_disable_sleep, dev_reset_disable_sleep);

	items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Keep awake over USB", "Keep the screen on and block sleep while connected to a computer over USB.",
		on_off_labels, 2, on_off_values,
		dev_get_keep_awake_usb, dev_set_keep_awake_usb, dev_reset_keep_awake_usb);

	items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Enable SSH", dev_get_ssh_desc(),
		on_off_labels, 2, on_off_values,
		dev_get_ssh, dev_set_ssh, dev_reset_ssh);

	items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Start SSH on boot", "Automatically start SSH when device boots.",
		on_off_labels, 2, on_off_values,
		dev_get_ssh_on_boot, dev_set_ssh_on_boot, dev_reset_ssh_on_boot);

	items[idx++] = (SettingItem)ITEM_CYCLE_INIT(
		"Debug logging", "Save app and game logs to the SD card (.userdata/logs). Off keeps them in RAM only.",
		on_off_labels, 2, on_off_values,
		dev_get_debug_logging, dev_set_debug_logging, dev_reset_debug_logging);

	items[idx++] = (SettingItem)ITEM_BUTTON_INIT(
		"Clean dot files",
		"Remove macOS junk files (.DS_Store, ._*, .Trashes, etc.)",
		dev_clean_dotfiles);

	page->title = "Settings | Developer";
	page->items = items;
	page->item_count = idx;
	page->selected = 0;
	page->scroll = 0;
	page->is_list = 0;
	page->on_show = dev_on_show;
	page->on_hide = dev_on_hide;
	page->on_tick = dev_on_tick;
	page->dynamic_start = -1;
	page->max_items = DEV_ITEM_COUNT;

	// Sync initial values
	for (int i = 0; i < idx; i++) {
		settings_item_sync(&items[i]);
	}

	return page;
}

void developer_page_destroy(SettingsPage* page) {
	if (!page)
		return;
	// Safety net if the process tears down while the page is still shown
	// (on_hide normally joins the poller first).
	if (ssh_ip_started) {
		ssh_ip_running = 0;
		pthread_join(ssh_ip_thread, NULL);
		ssh_ip_started = 0;
	}
	free(page->items);
	free(page);
}
