#ifndef WIZARD_H
#define WIZARD_H

#include <stdbool.h>

#include "sdl.h"
#include "network_common.h"

#define WIZ_TCP_PORT 55440
#define WIZ_UDP_PORT 55441
#define WIZ_MAGIC 0x4E58575Au /* "NXWZ" — NextUI wizard */
#define WIZ_PROTO_VERSION 1
#define WIZ_RSYNC_PORT 18731
#define WIZ_SESSION_PATH_DEFAULT "/tmp/netplay_session"
#define WIZ_RSYNC_CONF "/tmp/netplay_rsyncd.conf"
#define WIZ_RSYNC_PID "/tmp/netplay_rsyncd.pid"
#define WIZ_RSYNC_LOG "/tmp/netplay_rsyncd.log"
#define WIZ_MAX_PATTERNS 8

typedef struct {
	const char* game;					 // --game (required in wizard mode)
	const char* serve_dir;				 // --serve-dir (optional; host offers sync)
	const char* fetch_to;				 // --fetch-to (optional; client accepts sync)
	char patterns[WIZ_MAX_PATTERNS][64]; // parsed --fetch-files
	int pattern_count;
	const char* session_path; // --session-file (default WIZ_SESSION_PATH_DEFAULT)
	bool cleanup;			  // --cleanup
} WizArgs;

typedef struct {
	char role[8]; // "host" | "client"
	char peer_ip[16];
	char mode[8];		// "hotspot" | "wifi"
	char prev_ssid[33]; // SSID to restore on cleanup ("" = none)
} WizSession;

// All int returns below: 0 = ok, -1 = error (message already drawn by the
// callee), -2 = user cancelled (B).

// wizard.c
// Owned by wizard.c's main(); the ported wifi/net/sync code draws on it.
extern SDL_Surface* wiz_screen;

int wizard_write_session(const char* path, const WizSession* s, const char* game);
int wizard_read_session(const char* path, WizSession* s);

// wizard_wifi.c (Task 3)
// The hotspot SSID minus LINK_HOTSPOT_SSID_PREFIX, filled by wiz_hotspot_start()
// and rendered on the host's waiting screen. Empty until then.
extern char wiz_hotspot_code[8];

int wiz_wifi_ensure_connected(WizSession* s);			// WiFi mode, both roles
int wiz_hotspot_start(WizSession* s, const char* game); // hotspot host
int wiz_hotspot_join(WizSession* s);					// hotspot client
// wizard_net.c (Task 4)
int wiz_host_rendezvous(const WizArgs* a, WizSession* s); // wait+handshake(+sync serve)
int wiz_client_rendezvous(const WizArgs* a, WizSession* s);
// wizard_sync.c (Task 5)
int wiz_sync_serve_start(const char* serve_dir, const char* client_ip);
void wiz_sync_serve_stop(void);
int wiz_sync_pull(const char* host_ip, const char* fetch_to,
				  char names[][64], int name_count);
#endif
