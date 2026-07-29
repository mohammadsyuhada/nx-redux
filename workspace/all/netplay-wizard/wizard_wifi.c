/*
 * Wizard network set-up — WiFi picker and hotspot host/join.
 *
 * Ported from netplay/netplay_helper.c: ensureWifiEnabled +
 * showWiFiNetworkSelection (:352-660), the hotspot host set-up (:1598-1640) and
 * the hotspot join hygiene (:2326-2460). Behaviour is the same flow; every
 * deliberate difference is marked "wizard:" in a comment.
 *
 * minarch's chrome is gone: no GFX_setMode toggling, no menu bitmap drawn
 * behind the text, no minarch_hdmimon/beforeSleep/afterSleep — the wizard owns
 * the screen (wiz_screen) and has no core to pause. minarch_menuMessage is
 * replaced by wiz_error().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "api.h"
#include "defines.h"
#include "keyboard.h"
#include "network_common.h"
#include "ui_buttonhintbar.h"
#include "ui_list.h"
#include "ui_message.h"
#include "wifi_direct.h"
#include "wizard.h"

// Rescan cadence for both pickers, as in minarch's WiFi list.
#define WIZ_SCAN_INTERVAL_MS 4000
// Upper bound on a picker with nothing to pick: the wizard sits between the
// launcher and the game, so it must never wait forever.
#define WIZ_PICKER_TIMEOUT_MS 120000
// How long an error screen stays up when the user does not dismiss it.
#define WIZ_ERROR_TIMEOUT_MS 5000
// DHCP lease wait after associating (minarch's 20 x 500ms).
#define WIZ_DHCP_TIMEOUT_MS 10000

#define WIZ_WIFI_MAX_NETWORKS 16
#define WIZ_HOTSPOT_MAX 8
#define WIZ_LABEL_MAX 96

// SSID minus LINK_HOTSPOT_SSID_PREFIX (4 chars); Task 4's waiting screen shows
// it to the joining player. Empty until wiz_hotspot_start() succeeds.
char wiz_hotspot_code[8] = {0};

//////////////////////////////////
// Screens
//////////////////////////////////

// Transient progress text ("Connecting...", "Getting IP address..."). The
// equivalent of netplay_helper's showOverlayMessage without the menu backdrop.
static void wiz_status(const char* message) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	GFX_flip(wiz_screen);
}

// Stands in for minarch_menuMessage(). Difference: minarch blocks on A forever
// because it returns to a menu; every wizard caller returns -1 straight after
// this, which aborts the wizard and launches the game, so the screen must clear
// itself for a user who walked away.
static void wiz_error(const char* message) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	UI_renderButtonHintBar(wiz_screen, (char*[]){"A", "OKAY", NULL});
	GFX_flip(wiz_screen);

	uint32_t start = SDL_GetTicks();
	while (SDL_GetTicks() - start < WIZ_ERROR_TIMEOUT_MS) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			break;
		GFX_sync();
	}
}

// One list screen for both pickers. UI_renderSimpleMenu draws every item it is
// given, so the caller's list is windowed here (a scan can return more networks
// than fit) and the slice start is handed over as the item array.
static void wiz_render_list(const char* title, const char** labels, int count,
							int selected, int* scroll) {
	ListLayout layout = UI_calcListLayout(wiz_screen);
	UI_adjustListScroll(selected, scroll, layout.items_per_page);

	int visible = count - *scroll;
	if (visible > layout.items_per_page)
		visible = layout.items_per_page;

	SimpleMenuConfig config = {
		.title = title,
		.items = labels + *scroll,
		.item_count = visible,
		.btn_b_label = "BACK",
		.hide_controls_hint = true};
	UI_renderSimpleMenu(wiz_screen, selected - *scroll, &config);
	UI_renderScrollIndicators(wiz_screen, *scroll, layout.items_per_page, count);
	GFX_flip(wiz_screen);
}

// Same frame as wiz_render_list but with no items yet, so the title bar and the
// B hint stay put while the (blocking) scan runs.
static void wiz_render_empty(const char* title, const char* message) {
	SimpleMenuConfig config = {
		.title = title,
		.items = NULL,
		.item_count = 0,
		.btn_b_label = "BACK",
		.hide_controls_hint = true};
	UI_renderSimpleMenu(wiz_screen, 0, &config);
	UI_renderCenteredMessage(wiz_screen, message);
	GFX_flip(wiz_screen);
}

//////////////////////////////////
// WiFi helpers
//////////////////////////////////

// RSSI: typically -30 (excellent) to -90 (poor).
static const char* wiz_signal_bars(int rssi) {
	if (rssi >= -50)
		return "[####]";
	if (rssi >= -60)
		return "[### ]";
	if (rssi >= -70)
		return "[##  ]";
	if (rssi >= -80)
		return "[#   ]";
	return "[    ]";
}

// Leading marker in a network's label.
static const char* wiz_status_marker(const WIFI_direct_network_t* net, const char* connected_ssid) {
	if (connected_ssid && strcmp(net->ssid, connected_ssid) == 0)
		return "[C]"; // currently connected
	if (net->has_saved_creds)
		return "[*]"; // saved credentials
	if (net->is_secured)
		return "[L]"; // locked, needs a password
	return "   ";	  // open
}

static bool wiz_have_ip(void) {
	char ip[16] = {0};
	return WIFI_direct_getIP(ip, sizeof(ip)) == 0 && ip[0] != '\0' && strcmp(ip, "0.0.0.0") != 0;
}

static bool wiz_wait_for_ip(int timeout_ms) {
	for (int waited = 0; waited < timeout_ms; waited += 500) {
		if (wiz_have_ip())
			return true;
		SDL_Delay(500);
	}
	return wiz_have_ip();
}

// Port of ensureWifiEnabled(). Returns false only if the WiFi stack refuses to
// come up; the caller draws the error.
//
// Turning the radio on here is PERSISTENT and deliberate: WIFI_direct_ensureReady()
// -> WIFI_enable(true) also does CFG_setWifi(true). Accepted by design — the user
// chose a networked mode, and this matches minarch's ensureWifiEnabled(). Cleanup
// (Task 6) restores the previous SSID *association*, never the radio power state,
// which is also why prev_ssid == "" means "nothing to reconnect" and not "radio was
// off". Do not add radio-state restore on top of this.
static bool wiz_wifi_ready(void) {
	if (WIFI_direct_isConnected())
		return true;

	// Cheap check before showing anything: a running supplicant means the stack
	// is up and merely unassociated.
	if (system("pidof wpa_supplicant > /dev/null 2>&1") == 0)
		return true;

	wiz_status("Enabling WiFi...");
	return WIFI_direct_ensureReady();
}

// The SSID Task 6 restores. Must be read before anything touches the
// association; no current connection means there is nothing to restore.
static void wiz_record_prev_ssid(WizSession* s) {
	if (WIFI_direct_getCurrentSSID(s->prev_ssid, sizeof(s->prev_ssid)) != 0)
		s->prev_ssid[0] = '\0';
}

// WIFI_direct_connect() already runs udhcpc internally; this is the fallback for
// the arm that did not connect (already-associated network) and for a lease that
// has not landed yet.
//
// -n (exit when no lease) is mandatory, not a copy of netplay_helper.c's line:
// busybox udhcpc runs in the foreground and -t caps discovers per round, not
// rounds, so without -n it retries forever and system() never returns — with the
// wizard holding PWR_disablePowerOff, that is a hardware-power-hold hang. Same
// reason wifi_direct.c:48-53 passes it. This fallback only runs after
// WIFI_direct_connect's three -n rounds already failed, i.e. almost exclusively
// in the conditions that would hang.
static bool wiz_ensure_ip(void) {
	if (wiz_have_ip())
		return true;

	wiz_status("Getting IP address...");
	system("udhcpc -i wlan0 -n -q -t 5 >/dev/null 2>&1");
	return wiz_wait_for_ip(WIZ_DHCP_TIMEOUT_MS);
}

// Undo a failed connect attempt before leaving the picker: WIFI_direct_connect()
// runs WIFI_selectOnly(), which disassociates and disables every other saved
// network, so an attempt that failed leaves the client stack worse than it found
// it. No-op when nothing was attempted (nothing was changed).
static void wiz_restore_connection(bool attempted) {
	if (!attempted)
		return;

	wiz_status("Restoring WiFi...");
	WIFI_direct_restorePreviousConnection();
}

// Connect arm of the picker. 0 = associated with an IP, -1 = stay in the list
// (message already drawn, except for a cancelled keyboard which needs none).
// *attempted is set once WIFI_direct_connect() has run, i.e. once the caller owes
// a wiz_restore_connection() on its way out.
static int wiz_picker_connect(const WIFI_direct_network_t* net, const char* connected_ssid,
							  bool* attempted) {
	if (connected_ssid && strcmp(net->ssid, connected_ssid) == 0) {
		wiz_status("Verifying connection...");
		// wizard: minarch returns success here whatever the IP state, because a
		// menu user can still fix it. The rendezvous cannot, so no IP is an error.
		if (!wiz_ensure_ip()) {
			wiz_error("Connected but no IP.\n\nPlease try again.");
			return -1;
		}
		return 0;
	}

	if (net->has_saved_creds || !net->is_secured) {
		wiz_status("Connecting...");
		*attempted = true;
		if (WIFI_direct_connect(net->ssid, NULL) != 0) { // NULL = use saved creds
			wiz_error("Connection failed.\n\nPlease check the network\nand try again.");
			return -1;
		}
	} else {
		char* password = Keyboard_getPassword();
		if (!password)
			return -1; // keyboard cancelled: back to the list, nothing to report

		wiz_status("Connecting...");
		*attempted = true;
		int ret = WIFI_direct_connect(net->ssid, password);
		free(password);

		if (ret != 0) {
			wiz_error("Connection failed.\n\nIncorrect password or\nnetwork unavailable.");
			return -1;
		}
	}

	if (!wiz_ensure_ip()) {
		wiz_error("Connected but no IP.\n\nPlease try again.");
		return -1;
	}
	return 0;
}

//////////////////////////////////
// WiFi mode
//////////////////////////////////

int wiz_wifi_ensure_connected(WizSession* s) {
	if (!wiz_wifi_ready()) {
		wiz_error("Failed to enable WiFi.\nPlease try again.");
		return -1;
	}

	// Both before the picker can change anything: prev_ssid is what --cleanup
	// restores in a later process (Task 6), saveCurrentConnection() is what
	// wiz_restore_connection() puts back inside this one when the user leaves the
	// picker after a failed attempt.
	wiz_record_prev_ssid(s);
	WIFI_direct_saveCurrentConnection();

	// wizard: minarch's ensureNetworkConnected() always shows the picker so the
	// user can confirm or switch networks. Here the user asked to launch a game,
	// not to manage WiFi — a working connection is the answer and the picker is
	// skipped. prev_ssid then names the network we are already on, which makes
	// Task 6's restore a no-op.
	if (WIFI_direct_isConnected() && wiz_have_ip())
		return 0;

	WIFI_direct_network_t networks[WIZ_WIFI_MAX_NETWORKS];
	char label_text[WIZ_WIFI_MAX_NETWORKS][WIZ_LABEL_MAX];
	const char* labels[WIZ_WIFI_MAX_NETWORKS];
	int count = 0;
	int selected = 0;
	int scroll = 0;
	bool dirty = true;
	bool scanned = false;
	bool preselected = false;
	bool attempted = false; // a connect attempt has run WIFI_selectOnly()
	uint32_t last_scan = 0;
	uint32_t start_time = SDL_GetTicks();

	// The SSID the list marks [C]. Read once: the picker is the only thing that
	// can change it, and doing so returns from this function.
	char connected_buf[WIFI_DIRECT_SSID_MAX] = {0};
	const char* connected_ssid =
		(WIFI_direct_getCurrentSSID(connected_buf, sizeof(connected_buf)) == 0) ? connected_buf : NULL;

	while (1) {
		uint32_t now = SDL_GetTicks();

		if (now - start_time > WIZ_PICKER_TIMEOUT_MS) {
			wiz_restore_connection(attempted);
			wiz_error("WiFi selection timed out.\n\nPlease try again.");
			return -1;
		}

		// wizard: minarch triggers a scan and reads the result 1.5s later. That
		// split is dead in the current wifi_direct.c — WIFI_direct_triggerScan()
		// is a no-op and WIFI_direct_scanNetworks() triggers, waits and reads in
		// one blocking call — so the read happens inline. The 4s cadence is kept.
		if (!scanned || now - last_scan >= WIZ_SCAN_INTERVAL_MS) {
			if (!scanned)
				wiz_render_empty("Select WiFi Network", "Scanning for networks...");

			count = WIFI_direct_scanNetworks(networks, WIZ_WIFI_MAX_NETWORKS);
			last_scan = SDL_GetTicks();
			scanned = true;
			dirty = true;

			for (int i = 0; i < count; i++) {
				// wizard: minarch renders "[C] ssid [####]". Marker and bars come
				// first here because a long SSID is truncated from the right, and
				// the markers are the part the user needs to see.
				snprintf(label_text[i], WIZ_LABEL_MAX, "%s %s %s",
						 wiz_status_marker(&networks[i], connected_ssid),
						 wiz_signal_bars(networks[i].rssi), networks[i].ssid);
				labels[i] = label_text[i];
			}

			// Pre-select the connected network, else the strongest saved one.
			// Once only: after that the cursor belongs to the user.
			if (count > 0 && !preselected) {
				preselected = true;
				int connected_idx = -1;
				int best_saved = -1;
				int best_rssi = -999;

				for (int i = 0; i < count; i++) {
					if (connected_ssid && strcmp(networks[i].ssid, connected_ssid) == 0)
						connected_idx = i;
					if (networks[i].has_saved_creds && networks[i].rssi > best_rssi) {
						best_rssi = networks[i].rssi;
						best_saved = i;
					}
				}

				if (connected_idx >= 0)
					selected = connected_idx;
				else if (best_saved >= 0)
					selected = best_saved;
				else
					selected = 0;
			}

			if (selected >= count)
				selected = count > 0 ? count - 1 : 0;
		}

		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B)) {
			// Cancelling after a failed attempt must not leave the device
			// disassociated with its other saved networks disabled.
			wiz_restore_connection(attempted);
			return -2;
		}

		if (count > 0) {
			if (PAD_justRepeated(BTN_UP)) {
				selected = (selected + count - 1) % count;
				dirty = true;
			} else if (PAD_justRepeated(BTN_DOWN)) {
				selected = (selected + 1) % count;
				dirty = true;
			} else if (PAD_justPressed(BTN_A)) {
				if (wiz_picker_connect(&networks[selected], connected_ssid, &attempted) == 0)
					return 0;

				// Failed: back to the list with fresh results. The timeout budget
				// restarts because it exists to catch an abandoned screen, and a
				// connect attempt (keyboard entry included) is not that — minarch
				// never restarts it, but there a timeout only closed a menu.
				scanned = false;
				dirty = true;
				start_time = SDL_GetTicks();
			}
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			if (count > 0)
				wiz_render_list("Select WiFi Network", labels, count, selected, &scroll);
			else
				wiz_render_empty("Select WiFi Network", "No networks found");
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

//////////////////////////////////
// Hotspot mode
//////////////////////////////////

int wiz_hotspot_start(WizSession* s, const char* game) {
	// The game title belongs to the rendezvous payload (Task 4), not to the AP:
	// the SSID carries a random code so two nearby hosts never collide.
	(void)game;

	if (!wiz_wifi_ready()) {
		wiz_error("Failed to enable WiFi.\nPlease try again.");
		return -1;
	}

	// Before startHotspot() hands wlan0 to hostapd. If WiFi was off, wiz_wifi_ready()
	// only just re-enabled it and the reconnect may not have completed — an empty
	// prev_ssid then means Task 6 restores nothing, same as minarch's
	// WIFI_direct_saveCurrentConnection().
	wiz_record_prev_ssid(s);

	char ssid[WIFI_DIRECT_SSID_MAX];
	NET_HotspotConfig hotspot_cfg = {
		.prefix = LINK_HOTSPOT_SSID_PREFIX,
		// minarch seeds with the game CRC; the wizard has no CRC, and the pid
		// keeps two launches in the same second apart.
		.seed = (uint32_t)time(NULL) ^ (uint32_t)getpid()};
	NET_generateHotspotSSID(ssid, sizeof(ssid), &hotspot_cfg);

	// Purge stale hotspot networks while wpa_supplicant is still up (startHotspot
	// kills it to hand wlan0 to hostapd). Clears entries this device accumulated
	// from past client joins.
	WIFI_direct_forgetAllHotspots();

	wiz_status("Starting hotspot...");
	if (WIFI_direct_startHotspot(ssid, WIFI_DIRECT_HOTSPOT_PASS) != 0) {
		wiz_error("Failed to start hotspot.\nCheck device capabilities.");
		return -1;
	}

	size_t prefix_len = strlen(LINK_HOTSPOT_SSID_PREFIX);
	snprintf(wiz_hotspot_code, sizeof(wiz_hotspot_code), "%s",
			 strlen(ssid) > prefix_len ? ssid + prefix_len : "????");

	// No IP check here: the AP address (10.0.0.1) is set by startHotspot with the
	// client stack down, and WIFI_direct_getIP() reads that client stack.
	return 0;
}

int wiz_hotspot_join(WizSession* s) {
	if (!wiz_wifi_ready()) {
		wiz_error("Failed to enable WiFi.\nPlease try again.");
		return -1;
	}

	wiz_record_prev_ssid(s);

	// Purge stale hotspot networks left by previous sessions before joining a new
	// one. Done up front (not only on teardown) so a backlog from aborted joins
	// can't grow unbounded or let wpa_supplicant prefer an old saved hotspot.
	WIFI_direct_forgetAllHotspots();

	char hotspots[WIZ_HOTSPOT_MAX][WIFI_DIRECT_SSID_MAX];
	char label_text[WIZ_HOTSPOT_MAX][WIZ_LABEL_MAX];
	const char* labels[WIZ_HOTSPOT_MAX];
	size_t prefix_len = strlen(LINK_HOTSPOT_SSID_PREFIX);
	int count = 0;
	int selected = 0;
	int scroll = 0;
	bool dirty = true;
	bool scanned = false;
	uint32_t last_scan = 0;
	uint32_t start_time = SDL_GetTicks();
	char selected_ssid[WIFI_DIRECT_SSID_MAX] = {0};

	while (!selected_ssid[0]) {
		uint32_t now = SDL_GetTicks();

		// wizard: minarch scans once and gives up. This screen rescans, so the
		// joiner can start first and wait for the host — but only for as long as
		// there is nothing to pick. Once a code is listed the user decides.
		if (count == 0 && now - start_time > WIZ_PICKER_TIMEOUT_MS) {
			wiz_error("No host found.\n\nMake sure the host has\nstarted hosting first.");
			return -1;
		}

		if (!scanned || now - last_scan >= WIZ_SCAN_INTERVAL_MS) {
			if (!scanned)
				wiz_render_empty("Select code shown on the host", "Scanning for hosts...");

			memset(hotspots, 0, sizeof(hotspots)); // scanForHotspots leaves the tail untouched
			// Three scan passes live inside WIFI_direct_scanForHotspots (a hotspot
			// can take a moment to appear); it returns early once it finds one.
			count = WIFI_direct_scanForHotspots(LINK_HOTSPOT_SSID_PREFIX, hotspots, WIZ_HOTSPOT_MAX);
			last_scan = SDL_GetTicks();
			scanned = true;
			dirty = true;

			for (int i = 0; i < count; i++) {
				snprintf(label_text[i], WIZ_LABEL_MAX, "%s",
						 strlen(hotspots[i]) > prefix_len ? hotspots[i] + prefix_len : "????");
				labels[i] = label_text[i];
			}

			if (selected >= count)
				selected = count > 0 ? count - 1 : 0;
		}

		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B))
			return -2;

		if (count > 0) {
			if (PAD_justRepeated(BTN_UP)) {
				selected = (selected + count - 1) % count;
				dirty = true;
			} else if (PAD_justRepeated(BTN_DOWN)) {
				selected = (selected + 1) % count;
				dirty = true;
			} else if (PAD_justPressed(BTN_A)) {
				strncpy(selected_ssid, hotspots[selected], sizeof(selected_ssid) - 1);
				selected_ssid[sizeof(selected_ssid) - 1] = '\0';
				continue;
			}
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			if (count > 0)
				wiz_render_list("Select code shown on the host", labels, count, selected, &scroll);
			else
				wiz_render_empty("Select code shown on the host", "Waiting for a host...");
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	char connect_msg[64];
	snprintf(connect_msg, sizeof(connect_msg), "Connecting to %s...",
			 strlen(selected_ssid) > prefix_len ? selected_ssid + prefix_len : "????");
	wiz_status(connect_msg);

	// Save the current connection so the failure paths below can put it back in
	// this process; s->prev_ssid is the copy that survives into --cleanup.
	WIFI_direct_saveCurrentConnection();

	// A device that hosted earlier still has hostapd/udhcpd running and an AP
	// address on its interface; both stop us associating as a client. wlan1 is
	// tg5040's boot-created AP vif — hostapd now runs on wlan0, but the vif can
	// still hold an address from an older build.
	system("killall hostapd 2>/dev/null");
	system("killall udhcpd 2>/dev/null");
	system("ip addr flush dev wlan1 2>/dev/null");
	system("ip link set wlan1 down 2>/dev/null");

	// Disconnect from current WiFi first to ensure a clean switch to the hotspot,
	// and flush stale state (old address, old default route) that would otherwise
	// keep us off the 10.0.0.x subnet.
	WIFI_direct_disconnect();
	system("ip addr flush dev wlan0 2>/dev/null");
	system("ip route flush dev wlan0 2>/dev/null");
	SDL_Delay(1000); // let the teardown settle before associating

	if (WIFI_direct_connect(selected_ssid, WIFI_DIRECT_HOTSPOT_PASS) != 0) {
		WIFI_direct_restorePreviousConnection(); // so the next scan works
		wiz_error("Failed to join hotspot.");
		return -1;
	}

	if (!wiz_have_ip()) {
		wiz_status("Waiting for network...");
		if (!wiz_wait_for_ip(WIZ_DHCP_TIMEOUT_MS)) {
			WIFI_direct_restorePreviousConnection();
			wiz_error("Failed to join hotspot.\n\nNo IP address from host.");
			return -1;
		}
	}

	return 0;
}
