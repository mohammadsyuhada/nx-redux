/*
 * NX Redux Netplay Helper Module Implementation
 *
 * The interactive host/join/status UI that used to live here moved to the
 * pre-launch wizard (workspace/all/netplay-wizard/). What remains is the
 * in-game support surface: link-support detection, game identity helpers,
 * session-active checks, quit-time cleanup, and the async hotspot teardown
 * used by the engine backends.
 */

#include "netplay_helper.h"
#include "netplay.h"
#include "gbalink.h"
#include "gblink.h"
#include "api.h"
#ifdef HAS_WIFIMG
#include "wifi_direct.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

// Minarch accessor functions (game data/name for CRC + discovery identity)
#include "minarch.h"

//////////////////////////////////////////////////////////////////////////////
// Hotspot Connection State
//////////////////////////////////////////////////////////////////////////////

// Track if client connected to hotspot (for WiFi restoration on disconnect).
// Within minarch these are only cleared: the wizard owns the connect flow and
// tears down its own state; the backends reset these on disconnect.
int netplay_connected_to_hotspot = 0;
int gbalink_connected_to_hotspot = 0;
int gblink_connected_to_hotspot = 0;

//////////////////////////////////////////////////////////////////////////////
// Async Hotspot Stop + WiFi Restore
//////////////////////////////////////////////////////////////////////////////

// Structure for async hotspot stop + WiFi restore
typedef struct {
	bool stop_hotspot; // true if host (need to call WIFI_direct_stopHotspot)
} HotspotStopArgs;

static void* hotspot_stop_thread(void* arg) {
	HotspotStopArgs* args = (HotspotStopArgs*)arg;

#ifdef HAS_WIFIMG
	if (args->stop_hotspot) {
		WIFI_direct_stopHotspot();
	}

	// Forget every saved hotspot network (this session's plus any left behind by
	// earlier sessions that didn't tear down cleanly), and re-enable other networks
	// so wpa_supplicant.conf doesn't accumulate stale NextUI-* entries over time.
	WIFI_direct_forgetAllHotspots();

	// Restore previous WiFi connection
	WIFI_direct_restorePreviousConnection();
#endif

	free(args);
	return NULL;
}

void stopHotspotAndRestoreWiFiAsync(bool is_host) {
	HotspotStopArgs* args = malloc(sizeof(HotspotStopArgs));
	if (!args) {
		LOG_error("stopHotspotAndRestoreWiFiAsync: failed to allocate args\n");
		return;
	}

	args->stop_hotspot = is_host;

	// Spawn detached thread
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (pthread_create(&thread, &attr, hotspot_stop_thread, args) != 0) {
		LOG_error("stopHotspotAndRestoreWiFiAsync: failed to create thread\n");
		free(args);
	}

	pthread_attr_destroy(&attr);
}

//////////////////////////////////////////////////////////////////////////////
// Link Support / Session State
//////////////////////////////////////////////////////////////////////////////

int Multiplayer_isActive(void) {
	return GBALink_isConnected() || GBLink_isConnected() || Netplay_isConnected();
}

CoreLinkSupport checkCoreLinkSupport(const char* core_name) {
	CoreLinkSupport support = {false, false, false};

	if (Netplay_checkCoreSupport(core_name)) {
		support.show_netplay = true;
	}
	if (GBALink_checkCoreSupport(core_name)) {
		support.has_netpacket = true;
		support.show_netplay = true;
	}
	if (GBLink_checkCoreSupport(core_name)) {
		support.has_gblink = true;
		support.show_netplay = true;
	}

	return support;
}

//////////////////////////////////////////////////////////////////////////////
// Game Identity Helpers (used by netplay_boot.c)
//////////////////////////////////////////////////////////////////////////////

uint32_t calculateGameCRC(void) {
	uint32_t crc = 0;
	void* game_data = minarch_getGameData();
	size_t game_size = minarch_getGameSize();
	if (game_data && game_size > 0) {
		const uint8_t* data = (const uint8_t*)game_data;
		for (size_t j = 0; j < game_size && j < 1024; j++) {
			crc = (crc << 1) ^ data[j];
		}
	}
	return crc;
}

void getGameName(char* buf, size_t buf_size) {
	const char* game_name = minarch_getGameName();
	if (game_name[0]) {
		strncpy(buf, game_name, buf_size - 1);
		buf[buf_size - 1] = '\0';
		// Remove extension if present
		char* dot = strrchr(buf, '.');
		if (dot) {
			*dot = '\0';
		}
	} else {
		strncpy(buf, "Unknown Game", buf_size - 1);
		buf[buf_size - 1] = '\0';
	}
}

//////////////////////////////////////////////////////////////////////////////
// Link Cleanup
//////////////////////////////////////////////////////////////////////////////

void Netplay_quitAll(void) {
	GBLink_quit();
	GBALink_quit();
	Netplay_quit();
}
