/*
 * netplay.elf — Dreamcast netplay pre-launch wizard.
 *
 * DC.pak's launch.sh runs this before flycast when nextui left the
 * /tmp/netplay_launch flag. Role -> connection mode -> network set-up ->
 * rendezvous, then a shell-sourceable session file for launch.sh.
 *
 * Exit codes are the launch.sh contract:
 *   0 = session file written, start the game with netplay
 *   1 = user cancelled, start the game normally
 *   2 = usage/setup error, start the game normally
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "minarch.h"
#include "ui_list.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "ui_splash.h"
#include "utils.h"
#include "wifi_direct.h"
#include "wizard.h"

// How long a terminal message stays up before the process exits (ms).
#define WIZ_MESSAGE_MS 1000

typedef enum { ST_ROLE,
			   ST_MODE,
			   ST_NETSETUP,
			   ST_RENDEZVOUS,
			   ST_DONE } WizState;

SDL_Surface* wiz_screen = NULL;

// netplay/keyboard.c is linked for Task 3's password entry and reaches back
// into minarch for the screen and the sleep hooks. The wizard has no core to
// pause and no HDMI monitor, so only the screen accessor is real.
SDL_Surface* minarch_getScreen(void) {
	return wiz_screen;
}
void minarch_beforeSleep(void) {}
void minarch_afterSleep(void) {}
void minarch_hdmimon(void) {}

//////////////////////////////////
// CLI
//////////////////////////////////

// Split --fetch-files on commas. The patterns are matched against basenames
// inside --fetch-to, so a '/' would let them escape that directory.
static int parse_patterns(const char* csv, WizArgs* a) {
	const char* p = csv;

	while (*p) {
		const char* comma = strchr(p, ',');
		size_t len = comma ? (size_t)(comma - p) : strlen(p);

		if (len > 0) {
			if (a->pattern_count >= WIZ_MAX_PATTERNS) {
				fprintf(stderr, "netplay: too many --fetch-files patterns (max %d)\n", WIZ_MAX_PATTERNS);
				return -1;
			}
			if (len >= sizeof(a->patterns[0])) {
				fprintf(stderr, "netplay: --fetch-files pattern too long\n");
				return -1;
			}
			if (memchr(p, '/', len)) {
				fprintf(stderr, "netplay: --fetch-files patterns must not contain '/'\n");
				return -1;
			}
			memcpy(a->patterns[a->pattern_count], p, len);
			a->patterns[a->pattern_count][len] = '\0';
			a->pattern_count++;
		}

		if (!comma)
			break;
		p = comma + 1;
	}

	return 0;
}

static int parse_args(int argc, char** argv, WizArgs* a) {
	memset(a, 0, sizeof(*a));
	a->session_path = WIZ_SESSION_PATH_DEFAULT;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		bool has_value = (i + 1 < argc);

		if (strcmp(arg, "--cleanup") == 0) {
			a->cleanup = true;
		} else if (strcmp(arg, "--game") == 0 && has_value) {
			a->game = argv[++i];
		} else if (strcmp(arg, "--serve-dir") == 0 && has_value) {
			a->serve_dir = argv[++i];
		} else if (strcmp(arg, "--fetch-to") == 0 && has_value) {
			a->fetch_to = argv[++i];
		} else if (strcmp(arg, "--session-file") == 0 && has_value) {
			a->session_path = argv[++i];
		} else if (strcmp(arg, "--fetch-files") == 0 && has_value) {
			if (parse_patterns(argv[++i], a) != 0)
				return -1;
		} else {
			fprintf(stderr, "netplay: unknown or incomplete argument '%s'\n", arg);
			return -1;
		}
	}

	if (!a->cleanup && (!a->game || !a->game[0])) {
		fprintf(stderr, "netplay: --game is required\n");
		return -1;
	}

	return 0;
}

//////////////////////////////////
// Session file
//////////////////////////////////

static void fput_shq(FILE* fp, const char* key, const char* val) {
	fprintf(fp, "%s='", key);
	for (const char* c = val; *c; c++)
		if (*c == '\'')
			fputs("'\\''", fp);
		else
			fputc(*c, fp);
	fputs("'\n", fp);
}

int wizard_write_session(const char* path, const WizSession* s, const char* game) {
	FILE* fp = fopen(path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "NETPLAY_ROLE=%s\nNETPLAY_PEER_IP=%s\nNETPLAY_MODE=%s\n",
			s->role, s->peer_ip, s->mode);
	fput_shq(fp, "NETPLAY_GAME", game);
	fput_shq(fp, "NETPLAY_PREV_SSID", s->prev_ssid);

	// A short write (tmpfs ENOSPC/EIO) must not read as success: exit 0 is the
	// hard "start with netplay" contract, and launch.sh would then source a
	// truncated file while --cleanup restored nothing. Errors surface at fclose
	// as often as at fprintf, so check both.
	int failed = (ferror(fp) != 0);
	if (fclose(fp) != 0)
		failed = 1;

	if (failed) {
		unlink(path);
		return -1;
	}

	return 0;
}

// Reverse of fput_shq: drop the wrapping single quotes and turn '\'' back into
// a literal quote. Not a shell parser — it only handles what fput_shq emits.
static void shq_unescape(const char* src, char* out, size_t out_size) {
	size_t o = 0;
	const char* c = src;

	if (*c == '\'')
		c++;

	while (*c && o + 1 < out_size) {
		if (*c == '\'') {
			if (c[1] == '\\' && c[2] == '\'' && c[3] == '\'') {
				out[o++] = '\'';
				c += 4;
				continue;
			}
			break; // closing quote
		}
		out[o++] = *c++;
	}

	out[o] = '\0';
}

// Copy KEY=value into out when line starts with key. quoted = written by
// fput_shq. Leaves out untouched on a non-match.
static void take_key(const char* line, const char* key, char* out, size_t out_size, bool quoted) {
	size_t klen = strlen(key);
	if (strncmp(line, key, klen) != 0 || line[klen] != '=')
		return;

	const char* val = line + klen + 1;
	if (quoted) {
		shq_unescape(val, out, out_size);
	} else {
		strncpy(out, val, out_size - 1);
		out[out_size - 1] = '\0';
	}
}

// NETPLAY_GAME is written for launch.sh but not read back: WizSession has no
// field for it and nothing on the cleanup path needs the title.
//
// Returns -1 for a missing file AND for one that yielded no role/mode: a
// zero-byte or truncated session is indistinguishable from none as far as
// teardown goes, and acting on half of one is worse than acting on nothing.
int wizard_read_session(const char* path, WizSession* s) {
	FILE* fp = fopen(path, "r");
	if (!fp)
		return -1;

	memset(s, 0, sizeof(*s));

	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		int len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		take_key(line, "NETPLAY_ROLE", s->role, sizeof(s->role), false);
		take_key(line, "NETPLAY_PEER_IP", s->peer_ip, sizeof(s->peer_ip), false);
		take_key(line, "NETPLAY_MODE", s->mode, sizeof(s->mode), false);
		take_key(line, "NETPLAY_PREV_SSID", s->prev_ssid, sizeof(s->prev_ssid), true);
	}

	fclose(fp);

	// Role and mode are what teardown dispatches on; without them there is no
	// session, whatever the file contained.
	if (!s->role[0] || !s->mode[0])
		return -1;

	return 0;
}

//////////////////////////////////
// Teardown
//////////////////////////////////
//
// Everything below undoes network state, and none of it draws: --cleanup runs
// headless (no GFX_init, no SDL), so SDL_Delay() is usleep() here and the
// callers that do have a screen draw their own message before calling in.

// Hard ceiling on ONE teardown episode, measured from the entry point that
// started it (--cleanup, the start-up heal, or a cancel arm) and covering
// everything that follows, orphan AP stop included. The arithmetic in
// wiz_teardown()'s header lands under this on every path; this is what makes
// the bound hold anyway when a step overruns the estimate — which it did once
// already, when the first version of that comment priced wifi_init.sh at zero.
#define WIZ_TEARDOWN_BUDGET_MS 19000

// WIFI_connectPass()'s own ceiling. It cannot be interrupted once entered, so
// it is not started unless this much budget remains — otherwise the deadline
// would be a suggestion rather than a bound.
#define WIZ_CONNECT_WORST_MS 6000

// wpa_supplicant does not answer the instant wifi_init.sh returns, and every
// wpa_cli call in the reconnect would quietly fail until it does. Short,
// because in both arms the supplicant is already there: on the host arm
// wifi_init.sh start has just launched it (tg5050's script even verifies with
// pidof before returning), and on the client arm it never stopped. This covers
// the socket appearing after the process does, not a stack that is still coming
// up from cold.
#define WIZ_SUPPLICANT_TIMEOUT_MS 2000
#define WIZ_SUPPLICANT_POLL_US 250000

// How long the INTERACTIVE teardown waits for the reconnect's backgrounded
// udhcpc to land a lease, and how often it looks. Only ever spent when
// WIFI_connectPass() actually ran — see wiz_restore_prev_ssid().
#define WIZ_RESTORE_SETTLE_MS 2000
#define WIZ_RESTORE_POLL_MS 250

// The CHEAP "is there an IPv4 address on wlan0" test: one shell, three procs.
// Deliberately not WIFI_direct_getIP(), which goes through PLAT_wifiConnection
// (generic_wifi.c:277-336) — a wpa_cli status, an ip/grep/cut pipeline and an
// iw call, ~8 processes per look. At 250 ms polling that difference is seconds
// of pure fork overhead inside a budget measured in seconds.
#define WIZ_HAVE_IP_CMD "ip -4 addr show wlan0 2>/dev/null | grep -q 'inet '"

// Monotonic milliseconds. NOT SDL_GetTicks(): --cleanup never initialises SDL.
static uint32_t wiz_now_ms(void) {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

// Signed difference, so the comparison survives the uint32 wrap (same idiom as
// wizard_sync.c:827).
static bool wiz_past(uint32_t deadline) {
	return (int32_t)(wiz_now_ms() - deadline) >= 0;
}

// Presence test for the AP address WIFI_direct_startHotspot() puts on wlan0.
// Every byte is a compile-time constant (the address is wifi_direct.h's), and
// wlan0 is the interface wifi_direct.c hosts on, hardcoded there for the same
// single-radio reason. The trailing '/' is load-bearing: it is what stops
// "inet 10.0.0.10/24" — a CLIENT's lease from a host's udhcpd, which serves
// 10.0.0.10-50 — from reading as the host address 10.0.0.1.
#define WIZ_HOTSPOT_IP_CMD \
	"ip -4 addr show wlan0 2>/dev/null | grep -q 'inet " WIFI_DIRECT_HOTSPOT_IP "/'"

static bool wiz_hostapd_running(void) {
	return system("pidof hostapd > /dev/null 2>&1") == 0;
}

static bool wiz_hotspot_ip_present(void) {
	return system(WIZ_HOTSPOT_IP_CMD) == 0;
}

/*
 * A netplay AP that no session file accounts for — the STRICT test, BOTH marks.
 *
 * Used wherever nothing has said this device hosted: the two entry points
 * (--cleanup and the start-up heal) run it before they even look for a session
 * file, and the corrupt-session arm relies on that same call. Both marks are
 * required because a lone hostapd could belong to the stock AP vif some
 * firmware brings up at boot (wizard_wifi.c:569), which is not ours to kill;
 * pairing it with an address test that is wlan0-specific and 10.0.0.1-specific
 * excludes that vif (it lives on wlan1, wifi_direct.c:261/278) and excludes a
 * client's 10.0.0.10-50 lease.
 *
 * WHAT THIS DELIBERATELY MISSES, stated because the loose test in
 * wiz_stop_hotspot() catches it and this one does not: an AP whose hostapd
 * CRASHED but whose address and udhcpd survive. Dropping the hostapd mark to
 * catch it would make the test "10.0.0.1 is on wlan0", which fires on a home
 * network that happened to lease this device 10.0.0.1 — a full WiFi-stack
 * restart on a device that never went near netplay. A crashed hostapd is not
 * broadcasting, so what it leaves behind is a stale address rather than a live
 * AP; the false positive is the worse of the two, so this sacrifices the catch.
 * wiz_stop_hotspot() can afford the loose test only because its caller already
 * knows from the session file that this device hosted.
 */
static bool wiz_orphan_ap_present(void) {
	return wiz_hostapd_running() && wiz_hotspot_ip_present();
}

/*
 * Everything WIFI_direct_stopHotspot() does when its hotspot_active flag is
 * true, for the case where that flag is false because ANOTHER PROCESS started
 * the AP.
 *
 * The flag (wifi_direct.c:26) is process-local, and the wizard's teardown is
 * usually not running in the process that hosted: `netplay.elf --cleanup` is a
 * fresh process launch.sh starts after the game exits, and the start-up heal is
 * the NEXT wizard. In both, WIFI_direct_stopHotspot() sees a false flag and
 * returns 0 without touching a hostapd that is very much alive — the AP would
 * outlive the session that created it, which is the one thing --cleanup exists
 * to prevent.
 *
 * Mirror of wifi_direct.c:357-388, tmp-file names included; if that sequence
 * changes, this one changes with it. The alternative — teaching
 * WIFI_direct_stopHotspot() to treat a live hostapd as an active hotspot, which
 * would delete this function — also changes behaviour for minarch's netplay.c,
 * gblink.c and gbalink.c, and that is not this task's file to change.
 */
static void wiz_stop_hotspot_orphan(void) {
	system("killall hostapd 2>/dev/null");
	usleep(200000); // let hostapd release wlan0 (wifi_direct.c:364)
	system("kill $(cat /tmp/gbalink_udhcpd.pid 2>/dev/null) 2>/dev/null");
	system("killall udhcpd 2>/dev/null");

	system("ip addr flush dev wlan0 2>/dev/null");
	system("ip link set wlan0 down 2>/dev/null");
	system("rm -f /tmp/gbalink_*.conf /tmp/gbalink_*.pid /tmp/gbalink_*.leases 2>/dev/null");

	// Hands wlan0 back to the client stack: wifi_init.sh start restarts
	// wpa_supplicant, which reassociates to a saved network on its own. Also
	// what makes the CFG_init() in run_cleanup() mandatory — see there.
	WIFI_enable(true);
	usleep(500000);
}

// The host's half of a hotspot teardown, wherever it runs.
static void wiz_stop_hotspot(void) {
	// Whether the library call is about to do the work — and, because that flag
	// is process-local, also the exact test for "this is the process that
	// hosted". Captured BEFORE the call, which resets it.
	bool was_ours = WIFI_direct_isHotspotActive();

	// Does the whole job when was_ours. A no-op in every other process.
	WIFI_direct_stopHotspot();

	// !was_ours is load-bearing, not defensive. WIFI_direct_stopHotspot() ends
	// in WIFI_enable(true) (wifi_direct.c:384), i.e. wifi_init.sh start — on a
	// platform whose wifi_init brings up its own AP vif with a hostapd, probing
	// afterwards would read state that call just CREATED, and the orphan stop
	// would then kill the stock AP, down the wlan0 wpa_supplicant had just been
	// given, and pay a second full stack restart. On the most-travelled path in
	// this task (the in-wizard cancel). So the probe only ever runs in a process
	// that did not host.
	//
	// There, EITHER mark is enough, because the caller already knows from the
	// session file that this device hosted: a live hostapd is ours (startHotspot
	// kills any other first, wifi_direct.c:273), and the AP address still on
	// wlan0 means the client stack never came back even if hostapd has since
	// died on its own — the case wiz_orphan_ap_present()'s strict test gives up.
	if (!was_ours && (wiz_hostapd_running() || wiz_hotspot_ip_present()))
		wiz_stop_hotspot_orphan();
}

/*
 * Put the association back the way the wizard found it. "" = nothing to
 * restore; note that the radio's POWER state is deliberately never restored
 * (the ruling is recorded at wizard_wifi.c:160-168).
 *
 * WIFI_direct_connect() is NOT used here, and that is this function's whole
 * shape. It ends in wifi_acquire_dhcp() (wifi_direct.c:38-61), which runs
 * `udhcpc -i wlan0 -n -q -t 6` THREE times — with busybox's default -T 3 that
 * is ~18 s per round against a network that does not answer, i.e. ~55 s of
 * blocking DHCP on top of its own 10 s association poll. That is the right
 * trade when JOINING a host's AP (the session cannot proceed without a lease,
 * and someone is watching a progress screen), and the wrong one here: the
 * session is over, and on --cleanup this runs on launch.sh's exit path with no
 * screen at all, where it would read as a minute-long hang after the game quits.
 *
 * WIFI_connectPass() is the same association without the blocking DHCP: it
 * polls at most 10 x 500 ms for the link (generic_wifi.c:664-685) and hands the
 * lease to a BACKGROUNDED udhcpc (:681), which keeps working after this process
 * exits. `interactive` then buys a bounded wait for that lease, and only where
 * there is a screen to justify it.
 */
static void wiz_restore_prev_ssid(const char* ssid, bool interactive, uint32_t deadline) {
	char current[WIFI_DIRECT_SSID_MAX];
	bool connected = false;

	if (!ssid || !ssid[0])
		return;

	// Bounded twice: by its own short timeout and by the episode's deadline.
	for (uint32_t waited = 0; waited < WIZ_SUPPLICANT_TIMEOUT_MS && !wiz_past(deadline);
		 waited += WIZ_SUPPLICANT_POLL_US / 1000) {
		if (system("pidof wpa_supplicant > /dev/null 2>&1") == 0)
			break;
		usleep(WIZ_SUPPLICANT_POLL_US);
	}

	// Already there, and on the host arm this is the COMMON case rather than the
	// lucky one: the AP teardown's wifi_init.sh spends ~9 s in a foreground
	// udhcpc, and a wpa_supplicant with every saved network enabled reassociates
	// during it. Otherwise: WiFi mode never left this network, or
	// wizard_wifi.c's own failure paths (wiz_restore_connection at :213, the two
	// WIFI_direct_restorePreviousConnection calls at :585/:593) already put it
	// back. Reassociating to reach a state we are in already would only cost the
	// game the wait.
	if (WIFI_direct_getCurrentSSID(current, sizeof(current)) != 0 ||
		strcmp(current, ssid) != 0) {
		// Only with room for the whole call: WIFI_connectPass() cannot be
		// interrupted once entered, so starting it with less than its own worst
		// case left would let the episode overrun the deadline by up to 6 s.
		// Skipping it is a graceful degradation rather than a failure — the
		// supplicant this teardown restarted has every saved network enabled
		// (select_network's disable is runtime-only and unsaved,
		// generic_wifi.c:704-706), so it reassociates on its own; what is lost
		// is determinism about WHICH saved network, not connectivity.
		if (!wiz_past(deadline - WIZ_CONNECT_WORST_MS)) {
			// NULL password = "use the saved network". VERIFIED, not assumed:
			// generic_wifi.c:613 finds the EXISTING network id for this SSID and
			// takes the "using existing network configuration" arm at :648 — the
			// stored psk is left alone and the security argument is not read at
			// all on that arm. prev_ssid is by construction a network this
			// device was associated to (wizard_wifi.c:184-187 reads it from the
			// connection info), so that entry exists. This is the same call
			// WIFI_direct_connect() makes at wifi_direct.c:135, minus its DHCP.
			//
			// SECURITY_WPA2_PSK, not SECURITY_NONE, and only the FALLBACK arm
			// can tell the difference: if wifi_find_network_id() returns -1 —
			// the entry genuinely absent, or one transient `wpa_cli
			// list_networks` failure — control reaches the add-branch, and there
			// SECURITY_NONE would set key_mgmt NONE (:637-641) and save_config
			// it, leaving a PERMANENT open twin of the user's home SSID that
			// wpa_supplicant could then associate to on any open AP announcing
			// that name. With WPA2_PSK neither sub-branch runs: the entry is
			// added with no key material, cannot associate, and is inert. Same
			// argument, same constant, as wifi_direct.c:235.
			//
			// The saved-network alternative,
			// WIFI_direct_restorePreviousConnection(), cannot be used at all:
			// the SSID it restores is process-local state (wifi_direct.c:28)
			// that a cleanup process never wrote.
			WIFI_connectPass(ssid, SECURITY_WPA2_PSK, NULL);
			connected = true;
		}
	}

	// WIFI_connectPass() runs select_network (generic_wifi.c:655), which
	// disables every OTHER saved network for as long as the supplicant lives,
	// and only undoes that on its success path (:674). An attempt that failed
	// would otherwise leave the device unable to roam to any of its networks
	// after the wizard is gone.
	WIFI_enableAll();

	// Interactive, and only when the call above actually ran. That gate is the
	// point: WIFI_connectPass()'s backgrounded udhcpc is the ONLY lease-fetcher
	// this function starts, so on the short-circuit path there is nothing to
	// wait for — wifi_init.sh's own udhcpc has already either landed a lease or
	// gone to the background to keep trying, and neither outcome is ours to
	// block on.
	//
	// What the wait buys when it does run: a -2 sends the user back to the mode
	// menu, and a retry that picks WiFi tests `isConnected() && have_ip()`
	// (wizard_wifi.c:290) to skip the picker. It is the difference between that
	// retry being instant and it showing a network list the user did not ask for
	// — which is also why it is not spent headless, where nobody is retrying.
	//
	// A stale 10.0.0.x left over from a hotspot (see wiz_teardown()'s client
	// arm) satisfies this probe and ends the wait early. Harmless: the wait is
	// best-effort, so forfeiting it costs nothing and cannot hang anything.
	if (interactive && connected) {
		for (uint32_t waited = 0; waited < WIZ_RESTORE_SETTLE_MS && !wiz_past(deadline);
			 waited += WIZ_RESTORE_POLL_MS) {
			if (system(WIZ_HAVE_IP_CMD) == 0)
				break;
			usleep(WIZ_RESTORE_POLL_MS * 1000);
		}
	}
}

/*
 * Undo the network state a session set up, as the session file describes it.
 *
 * Safe in a process that set none of it up and safe to call twice: every step
 * either probes the device for what it is about to undo or is idempotent by
 * construction.
 *
 * `interactive` = there is a screen in front of the user (the in-wizard cancel
 * paths). False on --cleanup and on the start-up heal's own reclaim, where the
 * only thing waiting is launch.sh.
 *
 * `deadline` is the episode's hard stop (WIZ_TEARDOWN_BUDGET_MS from the entry
 * point, so an orphan AP stop the caller ran first is inside it too).
 *
 * LATENCY BUDGET. This blocks launch.sh's exit path with no UI when interactive
 * is false, so the cost is stated rather than called "bounded". The dominant
 * term is NOT in this file:
 *
 *   wiz_sync_serve_stop              ~0.1 s  3 forks, no waits
 *   AP down + client stack back     ~10.0 s  200 ms + 500 ms of usleep, and
 *   (host arm)                              between them WIFI_enable(true) =
 *                                           wifi_init.sh start, which is
 *                                           SYNCHRONOUS and ends in a
 *                                           FOREGROUND `udhcpc -i wlan0 -b`
 *                                           (tg5040 :27, tg5050 :79). wlan0 is
 *                                           unassociated at that instant, so
 *                                           that udhcpc only returns when
 *                                           busybox backgrounds it ON LEASEFAIL
 *                                           — ~9 s at the defaults (-t 3
 *                                           discovers, -T 3 s apart). tg5050
 *                                           adds ~0.5 s verifying its
 *                                           wpa_supplicant start, up to 5 s if
 *                                           that retries.
 *     OR forgetAllHotspots (client)  ~0.5 s  4 wpa_cli round trips, no script
 *   supplicant poll                 <=2.0 s  ~1 fork in practice; see the macro
 *   WIFI_connectPass                <=6.0 s  10 x 500 ms assoc poll plus its
 *                                           wpa_cli calls. DHCP is handed to a
 *                                           BACKGROUNDED udhcpc, not waited on.
 *                                           Skipped entirely with < 6 s left,
 *                                           and skipped on the common host path
 *                                           anyway (the supplicant reassociates
 *                                           during that 9 s above).
 *   WIFI_enableAll                  ~0.1 s
 *   + IP settle                     <=2.3 s  interactive AND connectPass ran
 *   -----------------------------------------------------------------------
 *   headless client                 <=8.7 s
 *   headless host, short-circuited  <=12.8 s
 *   headless host, reconnecting     <=18.3 s
 *   interactive host, reconnecting  <=20.6 s  -> CLAMPED to 19 s by `deadline`
 *
 * That last row is why the deadline exists rather than the arithmetic alone:
 * the estimate above is the third one written for this function and the first
 * two were wrong, in one case by pricing wifi_init.sh at zero. THE ASSUMPTION
 * THE WHOLE TABLE RESTS ON is that busybox `udhcpc -b` backgrounds itself on
 * leasefail instead of retrying in the foreground; if a platform ever ships a
 * udhcpc that does not, wifi_init.sh start never returns and no deadline in
 * this file can help, because the block is inside system(). That is the one
 * thing to check on device.
 *
 * What used to make this ~70 s was WIFI_direct_connect()'s three blocking
 * udhcpc rounds — see wiz_restore_prev_ssid(). Nothing here may reintroduce an
 * unbounded wait: the wizard holds PWR_disablePowerOff, so a hang is one the
 * user cannot even switch the device off out of.
 */
static void wiz_teardown(const WizSession* s, bool interactive, uint32_t deadline) {
	// Unconditional, and the one part of this that is mandatory for --cleanup:
	// the host's rsyncd is the only piece of the session that can outlive the
	// wizard process. wizard_net.c stops it as soon as the client reports back,
	// but a run killed mid-sync leaves it holding port 18731 and serving
	// serve_dir to whoever asks. Idempotent when no daemon was ever started.
	wiz_sync_serve_stop();

	// WiFi mode set nothing up to undo: the picker left the device associated to
	// a real network, which is exactly where it should stay.
	if (strcmp(s->mode, "hotspot") != 0)
		return;

	if (strcmp(s->role, "host") == 0) {
		wiz_stop_hotspot();
	} else {
		// Drops this session's NXRedux-* entry (and any older one) and re-enables
		// the saved networks the join's select_network left disabled.
		//
		// NOTE, so that nobody "fixes" the wrong end of it: this arm can leave a
		// stale 10.0.0.x on wlan0. Nothing here flushes the interface, and the
		// reconnect's lease comes from WIFI_connectPass()'s backgrounded udhcpc
		// (generic_wifi.c:676-682), which — unlike wifi_acquire_dhcp()
		// (wifi_direct.c:43-44) — does not flush the old address and routes
		// first. Harmless, and deliberately left alone: the AP probes match
		// "10.0.0.1/" exactly, so a client's 10.0.0.10-50 leftover cannot be
		// read as a host AP. Widening those probes to the 10.0.0.x SUBNET to
		// "catch" this would make every client teardown look like an orphaned
		// AP and trigger a full WiFi-stack restart that nothing asked for.
		WIFI_direct_forgetAllHotspots();
	}

	wiz_restore_prev_ssid(s->prev_ssid, interactive, deadline);
}

//////////////////////////////////
// Cleanup mode (headless, no SDL)
//////////////////////////////////

// The teardown for a session file that is no longer live, shared by --cleanup
// and by the start-up heal. Always leaves the file gone.
//
// BOTH callers run wiz_orphan_ap_present()/wiz_stop_hotspot_orphan() before
// calling this, unconditionally, so an AP is already down by the time either
// arm below runs — that hoist is what covers the session-less orphan, and it is
// also what the corrupt arm here leans on instead of probing again.
static void wiz_reclaim_session(const char* path, uint32_t deadline) {
	// wizard_read_session only memsets after a successful fopen, so a failed
	// read must not leave the caller looking at stack garbage.
	WizSession session = {0};

	if (wizard_read_session(path, &session) == 0) {
		wiz_teardown(&session, false, deadline);
	} else {
		// No role and no mode to dispatch on: a write cut short by the same
		// power loss that skipped the cleanup, or a file written by hand.
		// Nothing here can know which network to restore, so what is left is
		// the state that needs no session to justify undoing.
		fprintf(stderr, "netplay: session file '%s' is unreadable; "
						"undoing what the device still shows\n",
				path);
		wiz_sync_serve_stop();
		// Cheap and never wrong: it only ever removes NXRedux-*/GBLink-*/GBALink-*
		// entries, which are single-session by construction, and re-enables the
		// saved networks. There is no prev_ssid to restore, so the supplicant's
		// own reassociation is what brings WiFi back.
		WIFI_direct_forgetAllHotspots();
	}

	// Unconditional: a file that failed to parse has nothing to act on but must
	// still not survive into the next launch.
	if (unlink(path) != 0 && errno != ENOENT)
		fprintf(stderr, "netplay: could not remove session file '%s': %s\n",
				path, strerror(errno));
}

// ALWAYS returns 0. launch.sh runs this on its exit path, and a device that
// could not be tidied up is no reason to fail the exit path of a game that has
// already finished; problems go to stderr instead.
static int run_cleanup(const WizArgs* a) {
	bool have_session;
	bool orphan_ap;
	uint32_t deadline;

	// BEFORE the session-file test, deliberately: a host killed mid-sync leaves
	// an rsyncd holding port 18731 and serving serve_dir, and it leaves NO
	// session file to find it by — wizard_net.c stops the daemon on every path
	// it returns from, so only a kill escapes that, and the session file is
	// written after the rendezvous rather than before it. Shell and files only,
	// so unlike everything below it this is safe ahead of CFG_init(), and it is
	// a no-op when no daemon was ever started.
	wiz_sync_serve_stop();

	have_session = (access(a->session_path, F_OK) == 0);

	// The SAME argument, applied to the higher-stakes leak. The AP exists from
	// wiz_hotspot_start() onwards, the session file only from ST_DONE, and the
	// host's wait screen between them has no wall-clock ceiling by design
	// (wizard_net.c:900-902). A host SIGKILLed in that window leaves hostapd,
	// udhcpd, 10.0.0.1 on wlan0, a dead wpa_supplicant — and nothing naming any
	// of it. The device cannot recover on its own either: hostapd holds wlan0
	// IFF_UP, so the next run's WIFI_direct_ensureReady() (generic_wifi.c:127-138
	// via wizard_wifi.c:169-180) believes the stack is fine and never restarts
	// it. So this probe is NOT behind the session-file gate.
	orphan_ap = wiz_orphan_ap_present();

	// Two cheap system() probes and a missing file: the normal ending of every
	// launch that did not use netplay, and of one that did and was cleaned up
	// already. Nothing was left behind, so nothing below needs to run — not even
	// CFG_init.
	if (!have_session && !orphan_ap)
		return 0;

	// MANDATORY before any WIFI_* call below, and therefore above both of them.
	// Both hotspot teardowns end in WIFI_enable(true) -> CFG_setWifi() ->
	// CFG_sync(), which serialises config.c's in-memory settings over
	// minuisettings.txt, merging every key it knows. In wizard mode GFX_init() ->
	// CFG_init() (api.c:290) has loaded that struct from disk first; here nothing
	// has, and syncing the all-zero definition (config.c:19) would silently reset
	// every setting on the device. CFG_init() is the headless load — nextval.c:13
	// and poweroff_next.c:338 use it exactly this way.
	CFG_init(NULL, NULL);

	// One budget for the whole episode, started before the orphan stop because
	// that is where most of it goes (wifi_init.sh start, ~10 s).
	deadline = wiz_now_ms() + WIZ_TEARDOWN_BUDGET_MS;

	// Before the session teardown, not after: with the AP down, wlan0 is back
	// with the client stack, which is what the reconnect inside
	// wiz_reclaim_session() needs. A valid host session runs this arm too — its
	// own wiz_stop_hotspot() then finds nothing left to do and goes straight on
	// to restoring prev_ssid.
	if (orphan_ap)
		wiz_stop_hotspot_orphan();

	if (have_session)
		wiz_reclaim_session(a->session_path, deadline);

	return 0;
}

//////////////////////////////////
// Screens
//////////////////////////////////

#define WIZ_MENU_ITEMS 2

static const char* role_items[] = {"Host Game", "Join Game"};
static const char* mode_items[] = {"Hotspot", "WiFi"};

static void render_role_menu(const char* game, int selected) {
	SimpleMenuConfig config = {
		.title = game,
		.items = role_items,
		.item_count = WIZ_MENU_ITEMS,
		.btn_b_label = "CANCEL",
		.btn_a_label = "SELECT",
		.hide_controls_hint = true};
	UI_renderSimpleMenu(wiz_screen, selected, &config);
	GFX_flip(wiz_screen);
}

static void render_mode_menu(int selected) {
	SimpleMenuConfig config = {
		.title = "Connection",
		.items = mode_items,
		.item_count = WIZ_MENU_ITEMS,
		.btn_b_label = "BACK",
		.btn_a_label = "SELECT",
		.hide_controls_hint = true};
	UI_renderSimpleMenu(wiz_screen, selected, &config);
	GFX_flip(wiz_screen);
}

static void show_message(const char* message, int hold_ms) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	GFX_flip(wiz_screen);
	SDL_Delay(hold_ms);
}

// Two-item menus: wrap rather than clamp. Returns true if the cursor moved.
static bool menu_navigate(int* selected) {
	if (PAD_justPressed(BTN_UP)) {
		*selected = (*selected + WIZ_MENU_ITEMS - 1) % WIZ_MENU_ITEMS;
		return true;
	}
	if (PAD_justPressed(BTN_DOWN)) {
		*selected = (*selected + 1) % WIZ_MENU_ITEMS;
		return true;
	}
	return false;
}

//////////////////////////////////
// Wizard
//////////////////////////////////

/*
 * The cancel/error teardown, and the only thing that clears `mode`.
 *
 * Clearing it is what keeps a -2 RETRY coherent. On a -2 the user lands back on
 * the mode menu and may pick the other mode; until they do, the struct would
 * otherwise still claim a hotspot that no longer exists, and anything that
 * dispatched on it (the sweep after the loop) would tear the same session down
 * a second time. ST_MODE always writes `mode` before ST_NETSETUP can read it,
 * so clearing it costs the retry nothing.
 *
 * What deliberately survives:
 *   role      the user is going back to the MODE menu, not the role menu;
 *             ST_ROLE overwrites it if they walk back further.
 *   prev_ssid a best-effort record, and after a teardown that reconnected it,
 *             it names the network the device is on RIGHT NOW. It is not stale
 *             either way: every entry into ST_NETSETUP re-reads the live SSID
 *             before touching the association (wizard_wifi.c:282, :436, :472 all
 *             call wiz_record_prev_ssid), so the retry captures the current
 *             value and overwrites this one.
 *   peer_ip   wizard_net.c's field; it clears it on its own failure paths.
 *
 * The message is drawn because the teardown blocks: a hotspot host teardown is a
 * whole wifi_init.sh restart plus an association, up to the 19 s ceiling in
 * wiz_teardown()'s budget, and without this the last screen would sit there
 * looking hung. That screen is also what pays for the interactive flag — the
 * extra wait it buys is the one a -2 retry benefits from.
 */
static void wiz_cancel(WizSession* s) {
	show_message("Cleaning up...", 0);
	wiz_teardown(s, true, wiz_now_ms() + WIZ_TEARDOWN_BUDGET_MS);
	s->mode[0] = '\0';
}

int main(int argc, char* argv[]) {
	WizArgs args;
	if (parse_args(argc, argv, &args) != 0)
		return 2;

	if (args.cleanup)
		return run_cleanup(&args);

	wiz_screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(wiz_screen, "Netplay");

	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	InitSettings();
	PAD_init();
	PWR_init();
	setup_signal_handlers();

	// Discovery and save sync run unattended for minutes; keep the device up
	// for the whole run (the process exits before the game starts).
	PWR_disableSleep();
	PWR_disableAutosleep();
	PWR_disablePowerOff();

	// A previous run that never reached --cleanup: an OSD kill or a flat battery
	// takes launch.sh's exit path with it, and that run's AP (or its rsyncd) is
	// still up. Undo it BEFORE this run sets anything up — wiz_hotspot_start()
	// would otherwise stack an AP on an AP, and wiz_record_prev_ssid() would
	// file the dead session's hotspot as the network to restore afterwards.
	//
	// Two independent pieces of evidence, and the AP probe is NOT gated on the
	// session file for the reason spelled out in run_cleanup(): the AP outlives
	// wiz_hotspot_start() while the file only appears at ST_DONE, so the window
	// between them leaves an AP that nothing names. CFG_init() has already run
	// here, inside GFX_init(), which is why this needs no equivalent of
	// run_cleanup()'s.
	bool stale_session = (access(args.session_path, F_OK) == 0);
	bool orphan_ap = wiz_orphan_ap_present();

	if (stale_session || orphan_ap) {
		uint32_t deadline = wiz_now_ms() + WIZ_TEARDOWN_BUDGET_MS;

		show_message("Cleaning up previous session...", 0);
		if (orphan_ap)
			wiz_stop_hotspot_orphan();
		if (stale_session)
			wiz_reclaim_session(args.session_path, deadline);
	}

	WizSession session;
	memset(&session, 0, sizeof(session));

	WizState state = ST_ROLE;
	int role_selected = 0;
	int mode_selected = 0;
	// Anything that is not a completed session (including a signal) means
	// "launch the game without netplay"; only ST_DONE clears this to 0.
	int exit_code = 1;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		switch (state) {
		case ST_ROLE:
			if (menu_navigate(&role_selected)) {
				dirty = true;
			} else if (PAD_justPressed(BTN_A)) {
				strcpy(session.role, role_selected == 0 ? "host" : "client");
				state = ST_MODE;
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				exit_code = 1;
				app_quit = true;
			}
			break;

		case ST_MODE:
			if (menu_navigate(&mode_selected)) {
				dirty = true;
			} else if (PAD_justPressed(BTN_A)) {
				strcpy(session.mode, mode_selected == 0 ? "hotspot" : "wifi");
				state = ST_NETSETUP;
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				state = ST_ROLE;
				dirty = true;
			}
			break;

		case ST_NETSETUP: {
			bool is_host = (strcmp(session.role, "host") == 0);
			int rc;

			if (strcmp(session.mode, "hotspot") == 0)
				rc = is_host ? wiz_hotspot_start(&session, args.game) : wiz_hotspot_join(&session);
			else
				rc = wiz_wifi_ensure_connected(&session);

			// Both non-zero arms undo whatever got as far as being set up. A -2
			// from the WiFi picker or the hotspot list has usually left nothing
			// behind (each screen unwinds its own attempt before returning), but
			// a -1 from wiz_hotspot_join() after it associated has: the NXRedux-*
			// network is saved by then.
			if (rc == -2) {
				wiz_cancel(&session);
				state = ST_MODE;
			} else if (rc != 0) {
				wiz_cancel(&session);
				exit_code = 2;
				app_quit = true;
			} else {
				state = ST_RENDEZVOUS;
			}
			dirty = true;
			break;
		}

		case ST_RENDEZVOUS: {
			int rc = (strcmp(session.role, "host") == 0)
						 ? wiz_host_rendezvous(&args, &session)
						 : wiz_client_rendezvous(&args, &session);

			// Both failure arms undo the ST_NETSETUP work first — by here a
			// hotspot host has an AP up and both roles may have an rsyncd.
			if (rc == -2) {
				wiz_cancel(&session);
				state = ST_MODE;
			} else if (rc != 0) {
				wiz_cancel(&session);
				exit_code = 2;
				app_quit = true;
			} else {
				state = ST_DONE;
			}
			dirty = true;
			break;
		}

		case ST_DONE:
			if (wizard_write_session(args.session_path, &session, args.game) != 0) {
				show_message("Failed to write session file", WIZ_MESSAGE_MS);
				// wizard_write_session() unlinks what it half-wrote, so
				// launch.sh's --cleanup would find no session file and undo
				// nothing (run_cleanup's first test) — the AP and the rsyncd
				// would outlive a game that is about to start WITHOUT netplay.
				// This is the last process that can take them down.
				wiz_cancel(&session);
				exit_code = 2;
			} else {
				show_message("Connected - starting game...", WIZ_MESSAGE_MS);
				exit_code = 0;
			}
			app_quit = true;
			break;
		}

		if (dirty) {
			switch (state) {
			case ST_ROLE:
				render_role_menu(args.game, role_selected);
				break;
			case ST_MODE:
				render_mode_menu(mode_selected);
				break;
			default:
				break; // the network states draw their own screens
			}
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	// Anything that left the loop without going through wiz_cancel() still has
	// whatever ST_NETSETUP set up: app_quit is what a caught signal sets, and
	// wizard_net.c's wait screens turn it into a -2 that returns here with the
	// loop condition already false. Only mode[0] can be true at this point,
	// because wiz_cancel() is the one thing that clears it.
	//
	// exit_code 0 is the case that must be left alone: there the session file
	// exists, the game is about to use it, and launch.sh's --cleanup is what
	// takes it down afterwards. Still inside the PWR_disableSleep window, on
	// purpose — the teardown blocks for as long as a reassociation takes.
	if (exit_code != 0 && session.mode[0])
		wiz_cancel(&session);

	PWR_enableSleep();
	PWR_enableAutosleep();

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return exit_code;
}
