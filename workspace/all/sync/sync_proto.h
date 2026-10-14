// sync_proto: the pure pieces of the Device Sync protocol (discovery payload,
// role rule, the .userdata/shared filter), kept apart from the UI so they build
// and run on the host. Tests: workspace/all/sync/tests/run_tests.sh
#ifndef SYNC_PROTO_H
#define SYNC_PROTO_H

#include <stdbool.h>
#include <stddef.h>

// The USB cable link (usblink.elf) is the /30 10.99.0.0: host 10.99.0.1,
// device 10.99.0.2.
#define SYNC_CABLE_NET "10.99.0.0"

// Discovery payload, after the HELLO/ACK prefix: "<ip>" or "<ip>:<flag>".
// Flag 0 = ROMs off, R = ROMs on, c / C = the same but the sender must be the
// rsync client (a phone). Old firmware reads only R. New handhelds append
// "\0v2" after the C string: old firmware stops at the NUL, new firmware reads
// it as "filters .userdata/shared" (a client-only peer filters too).
#define SYNC_PAYLOAD_TRAILER "v2"

typedef struct {
	char ip[16];
	bool roms;
	bool client_only;
	bool filters;
} SyncPeer;

// len = bytes after the prefix, trailer included. False (out->ip empty) unless
// the IP part is a dotted-quad IPv4 literal: it ends up in shell commands.
bool sync_parse_payload(const char* buf, size_t len, SyncPeer* out);

// Writes prefix + "<ip>:<0|R>" + NUL + trailer; returns the byte count to send
// (trailer included), or -1 when it doesn't fit.
int sync_format_payload(char* out, size_t size, const char* prefix, const char* ip, bool roms);

// True when this device runs the rsync daemon. A client-only side is always
// the client; otherwise the lower IP serves (the rule old firmware uses).
bool sync_is_server(bool own_client_only, bool peer_client_only, const char* own_ip, const char* peer_ip);

// True for an address on the cable link's /30.
bool sync_ip_is_cable(const char* ip);

// The .userdata/shared filter: only the save-state folders (<TAG>-<core>) and
// the slot pictures/markers (.minui/<TAG>/) travel. NULL-terminated rsync
// filter rules, excludes first.
extern const char* const sync_shared_rules[];

// macOS junk (AppleDouble "._*" files, .DS_Store) a card picks up on a Mac;
// kept out of every module. Client options and rsyncd.conf exclude value.
#define SYNC_JUNK_CLIENT_ARGS "--exclude='._*' --exclude=.DS_Store"
#define SYNC_JUNK_DAEMON_EXCLUDE "._* .DS_Store"

// The rules as client options ("--filter='- /music-player/' ...") and as an
// rsyncd.conf filter value ("- /music-player/ + ..."). -1 when out is too small.
int sync_shared_client_args(char* out, size_t size);
int sync_shared_daemon_filter(char* out, size_t size);

#endif
