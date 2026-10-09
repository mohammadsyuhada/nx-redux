#include "gbalink_mode.h"

#include <stdbool.h>
#include <string.h>
#include <strings.h>

// gpsp_serial "auto" in a USB Cable session: the gpSP 004 patch resolves
// "auto_cable" like "auto" (wireless adapter for games that have one), then
// real cable for everything else. "lockstep" is USB-only (decision 1 of the
// lockstep spec): anywhere else it behaves as "auto", so a value persisted by
// an earlier USB session never changes Wi-Fi or single-player play.
const char* gbalink_serial_option(const char* key, const char* value, const char* netplay_mode) {
	if (!key || !value || strcmp(key, "gpsp_serial") != 0)
		return value;
	bool usb = netplay_mode && strcmp(netplay_mode, "usb") == 0;
	if (usb && strcmp(value, "auto") == 0)
		return "auto_cable";
	if (!usb && strcmp(value, "lockstep") == 0)
		return "auto";
	return value;
}

const char* gbalink_link_mode_key(const char* core_name) {
	if (core_name && strcasecmp(core_name, "gpsp") == 0)
		return "gpsp_serial";
	return NULL;
}

bool gbalink_transport_ok(const char* core_name, const char* netplay_mode) {
	if (!core_name || strcasecmp(core_name, "mgba") != 0)
		return true;
	return netplay_mode && strcmp(netplay_mode, "usb") == 0;
}
