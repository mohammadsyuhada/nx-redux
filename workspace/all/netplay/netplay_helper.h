/*
 * NX Redux Netplay Helper Module
 *
 * Slimmed to the pieces minarch still needs now that netplay setup lives in
 * the pre-launch wizard (workspace/all/netplay-wizard/): core link-support
 * detection, game identity helpers used by netplay_boot.c, session-active
 * checks, quit-time cleanup, and the async hotspot teardown the engine
 * backends (netplay.c / gbalink.c / gblink.c) call on disconnect.
 */

#ifndef NETPLAY_HELPER_H
#define NETPLAY_HELPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Link type enum for unified handling of all link types
typedef enum {
	LINK_TYPE_NETPLAY,
	LINK_TYPE_GBALINK,
	LINK_TYPE_GBLINK
} LinkType;

// Result of checking core link support capabilities
typedef struct {
	bool show_netplay;	// true if any link type is supported
	bool has_netpacket; // true if GBALink (netpacket interface) is supported
	bool has_gblink;	// true if GBLink (gambatte network) is supported
} CoreLinkSupport;

// Track if client connected to hotspot (for WiFi restoration on disconnect).
// Read/cleared by the engine backends' disconnect paths.
extern int netplay_connected_to_hotspot;
extern int gbalink_connected_to_hotspot;
extern int gblink_connected_to_hotspot;

// Non-blocking async hotspot stop + WiFi restoration.
// Called by the engine backends on disconnect to avoid 5-10 second delays.
// is_host: true if we were hosting (need to stop hotspot), false if client (just restore WiFi)
void stopHotspotAndRestoreWiFiAsync(bool is_host);

/**
 * Check if any multiplayer session is active (Netplay, GBALink, or GBLink)
 * @return 1 if any link type is connected, 0 otherwise
 */
int Multiplayer_isActive(void);

/**
 * Check which link types a core supports
 * @param core_name Core name (e.g., "gpsp", "gambatte", "fbneo")
 * @return CoreLinkSupport struct with support flags
 */
CoreLinkSupport checkCoreLinkSupport(const char* core_name);

/**
 * Calculate simple CRC from game data
 * @return CRC32-like checksum
 */
uint32_t calculateGameCRC(void);

/**
 * Get game name from current ROM (without extension)
 * @param buf Buffer to receive game name
 * @param buf_size Size of buffer
 */
void getGameName(char* buf, size_t buf_size);

/**
 * Clean up all link sessions (GBLink, GBALink, Netplay)
 * Call before quit to ensure clean shutdown
 */
void Netplay_quitAll(void);

#endif /* NETPLAY_HELPER_H */
