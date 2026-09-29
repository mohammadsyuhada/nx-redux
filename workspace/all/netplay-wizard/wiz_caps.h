// wiz_caps.h - an optional capability token the two wizards trade.
//
// A pak's launcher can pass --caps <token> (e.g. Dreamcast's
// "dcbios=<fingerprint>", so both sides can agree on a BIOS). The wizard
// appends it to its HELLO line as a trailing "caps=<token>" field and writes
// the peer's token to the session file as NETPLAY_PEER_CAPS. A wizard that
// predates the field ignores it (every HELLO parse reads a fixed number of
// fields), and a peer that sends none yields an empty token.
//
// Libc only, so the host unit test links it directly.

#ifndef WIZ_CAPS_H
#define WIZ_CAPS_H

#include <stdbool.h>
#include <stddef.h>

// Longest token, including the terminator.
#define WIZ_CAPS_MAX 64

// Non-empty, shorter than WIZ_CAPS_MAX, only [A-Za-z0-9._=,-]: safe on the
// wire (no spaces) and in the session file, which launch.sh sources.
bool WizCaps_isValid(const char* token);
// The token of the "caps=" field in a received HELLO line, or "" when the
// line has none or its token is invalid. Only fields after the fourth count,
// so a game named "caps=..." is never mistaken for it.
void WizCaps_find(const char* line, char* out, size_t out_size);
// " caps=<token>" to append to an outgoing HELLO, or "" for no/invalid token.
void WizCaps_field(const char* token, char* out, size_t out_size);

#endif
