// netplay_boot.c - boot-time netplay engine start for wizard-launched sessions.
//
// The pre-launch wizard (netplay.elf, run via netplay-prelaunch.sh from the
// pak's launch.sh) does all interactive setup: network (hotspot/WiFi), peer
// discovery, handshake. It hands the result to minarch as environment
// variables; this module turns them into a running link-engine session
// before the first frame, replacing the deleted in-game Host/Join menus.
// Env-based on purpose: /tmp/netplay_session can survive an OSD/power-quit,
// env vars cannot, so a stale session file never starts netplay.

#include "netplay_boot.h"
#include "netplay_helper.h"
#include "netplay.h"
#include "gbalink.h"
#include "gbalink_mode.h"
#include "gblink.h"
#include "wifi_direct.h" // WIFI_DIRECT_HOTSPOT_IP macro only, no link dependency on wifi_direct.c
#include "api.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "minarch.h"

// The host boots its core and binds its listen socket while the client does
// the same; whoever is slower decides the gap. 20 x 500ms rides out even a
// slow-booting core (PS BIOS) without a perceptible wait in the common case.
#define BOOT_CONNECT_ATTEMPTS 20
#define BOOT_CONNECT_DELAY_MS 500

// Message for the caller's failure dialog; NULL = the generic one.
static const char* boot_error;

const char* NetplayBoot_lastError(void) {
	return boot_error;
}

static int boot_start_host(LinkType type, const char* core_name, const char* game_name, uint32_t crc,
						   const char* hotspot_ip) {
	switch (type) {
	case LINK_TYPE_GBALINK: {
		// The host's link mode, synced to the client; NULL for cores without one (mGBA)
		const char* key = gbalink_link_mode_key(core_name);
		return GBALink_startHost(game_name, crc, hotspot_ip, key ? minarch_getCoreOptionValue(key) : NULL);
	}
	case LINK_TYPE_GBLINK:
		return GBLink_startHost(game_name, crc, hotspot_ip);
	default:
		return Netplay_startHost(game_name, crc, hotspot_ip);
	}
}

static int boot_connect_once(LinkType type, const char* peer_ip) {
	switch (type) {
	case LINK_TYPE_GBALINK:
		return GBALink_connectToHost(peer_ip, GBALINK_DEFAULT_PORT);
	case LINK_TYPE_GBLINK:
		return GBLink_connectToHost(peer_ip, GBLINK_DEFAULT_PORT);
	default:
		return Netplay_connectToHost(peer_ip, NETPLAY_DEFAULT_PORT);
	}
}

int NetplayBoot_startFromEnv(const char* core_name) {
	boot_error = NULL;
	const char* role = getenv("NETPLAY_ROLE");
	if (!role || !role[0])
		return 0; // plain launch

	const char* peer_ip = getenv("NETPLAY_PEER_IP");
	const char* mode = getenv("NETPLAY_MODE");
	bool hotspot = mode && strcmp(mode, "hotspot") == 0;

	CoreLinkSupport support = checkCoreLinkSupport(core_name);
	if (!support.show_netplay) {
		// Packaging bug: a netplay marker shipped in a pak whose core has no
		// link backend. Fail loudly rather than strand the peer.
		LOG_error("NetplayBoot: NETPLAY_ROLE=%s but core %s has no link support\n", role, core_name);
		return -1;
	}
	LinkType type = support.has_netpacket ? LINK_TYPE_GBALINK
					: support.has_gblink  ? LINK_TYPE_GBLINK
										  : LINK_TYPE_NETPLAY; // same priority as the old menu (ma_menu.c:1678)

	// mGBA's link is the real cable, which only runs over the USB Cable. The
	// wizard offers mGBA nothing else; this catches a stale or hand-made env.
	if (type == LINK_TYPE_GBALINK && !gbalink_transport_ok(core_name, mode)) {
		LOG_error("NetplayBoot: mGBA link needs the USB Cable (NETPLAY_MODE=%s)\n", mode ? mode : "NULL");
		boot_error = "MGBA link needs the USB Cable";
		return -1;
	}

	char game_name[256];
	getGameName(game_name, sizeof(game_name));
	uint32_t crc = calculateGameCRC();

	if (strcmp(role, "host") == 0)
		return boot_start_host(type, core_name, game_name, crc, hotspot ? WIFI_DIRECT_HOTSPOT_IP : NULL);

	if (!peer_ip || !peer_ip[0]) {
		LOG_error("NetplayBoot: client launch with no NETPLAY_PEER_IP\n");
		return -1;
	}
	for (int attempt = 0; attempt < BOOT_CONNECT_ATTEMPTS; attempt++) {
		if (attempt)
			SDL_Delay(BOOT_CONNECT_DELAY_MS);
		int rc = boot_connect_once(type, peer_ip);
		if (rc == 0)
			return 0;
		if (type == LINK_TYPE_GBALINK && rc == GBALINK_CONNECT_NEEDS_RELOAD && gbalink_link_mode_key(core_name)) {
			// Host runs a different gpsp_serial mode: adopt it, persist it,
			// reload the core with the new mode, then reconnect. Ports the
			// menu-layer fallback this migration deletes (netplay_helper.c
			// 2251-2269); the engine getters/appliers live on in gbalink.c.
			GBALink_applyPendingLinkMode();
			minarch_saveConfig();
			GBALink_disconnect();
			minarch_reloadGame();
		}
	}
	LOG_error("NetplayBoot: could not reach host %s after %d attempts\n", peer_ip, BOOT_CONNECT_ATTEMPTS);
	return -1;
}
