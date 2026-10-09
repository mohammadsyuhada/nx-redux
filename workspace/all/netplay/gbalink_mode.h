#ifndef GBALINK_MODE_H
#define GBALINK_MODE_H

#include <stdbool.h>

// Resolves the gpsp_serial value handed to the core: "auto" becomes the hidden
// "auto_cable" in USB Cable sessions, and the USB-only "lockstep" falls back to
// "auto" everywhere else; every other key and value passes through unchanged.
const char* gbalink_serial_option(const char* key, const char* value, const char* netplay_mode);

// The core option that carries the GBA link mode the host syncs to the
// client, or NULL when the core has none (mGBA: always the real cable).
const char* gbalink_link_mode_key(const char* core_name);

// false when this core's link can't run over this transport: mGBA's real
// cable is USB-only (netplay_mode "usb"). gpSP and unknown cores: true.
bool gbalink_transport_ok(const char* core_name, const char* netplay_mode);

#endif
