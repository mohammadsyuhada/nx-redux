#ifndef GBALINK_MODE_H
#define GBALINK_MODE_H

// Resolves the gpsp_serial value handed to the core: "auto" becomes the hidden
// "auto_cable" in USB Cable sessions, and the USB-only "lockstep" falls back to
// "auto" everywhere else; every other key and value passes through unchanged.
const char* gbalink_serial_option(const char* key, const char* value, const char* netplay_mode);

#endif
