// sync_proto: the pure pieces of the Device Sync protocol (discovery payload,
// role rule, the .userdata/shared filter, the ROM sync rules), kept apart from
// the UI so they build and run on the host. Tests:
// workspace/all/sync/tests/run_tests.sh
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

// Art (<console>/.media/) only fills gaps: the two devices save it at
// different quality under the same paths, so newer-wins could replace the
// better copy. The ROM run leaves it out; a second run per ROM phase (same
// source and destination) copies only art the other side lacks.
#define SYNC_ROMS_NO_ART_ARGS "--exclude='/*/.media/'"
#define SYNC_ROMS_ART_ARGS "--ignore-existing --include='/*/' --include='/*/.media/***' --exclude='*'"

// No second copy across a console's emulator folders: a game isn't sent into
// a folder when the receiver keeps it (same top-level name, any case) in a
// sibling folder of that console but not in the folder itself.

// A growable list of names (a folder's entries, exclude patterns). Zero it to
// start; sync_names_free empties it again.
typedef struct {
	char** items;
	int count;
	int cap;
} SyncNames;

bool sync_names_add(SyncNames* l, const char* name);
int sync_names_find(const SyncNames* l, const char* name); // exact; index or -1
bool sync_names_has_ci(const SyncNames* l, const char* name);
void sync_names_free(SyncNames* l);

// The display name before " (TAG)": "Game Boy Advance (MGBA)" -> "Game Boy
// Advance". False when folder isn't "Name (TAG)" or out is too small.
bool sync_console_display(const char* folder, char* out, size_t size);

// Two different folders of the same console (same display name).
bool sync_is_sibling(const char* a, const char* b);

// False for what isn't a game, by the launcher's own rule (hide() in
// common/utils.c): dot entries (.media, ._ junk), *.disabled and map.txt.
bool sync_is_game_name(const char* name);

// The name in one `rsync --list-only --8-bit-output` line ("<perms> <size>
// <date> <time> <name>"; a symlink's " -> target" is dropped). False for the
// "." entry, a line that isn't an entry, or a name that doesn't fit.
bool sync_list_line_name(const char* line, char* out, size_t size, bool* is_dir);

// pattern made literal for an rsync exclude: when it holds a wildcard (* ? [),
// a backslash before each of \ * ? [; otherwise unchanged (rsync reads a
// backslash literally in a pattern without wildcards). Length, or -1 when out
// is too small.
int sync_escape_pattern(const char* pattern, char* out, size_t size);

// Exclude patterns for one Roms-level run, folder `folder`: "/<folder>/<name>"
// (escaped) for each game in sender_x the receiver has in receiver_siblings
// but not in receiver_x (case-insensitively). Names with a line break are
// skipped (patterns go one per line in an --exclude-from file). Appends to
// out; returns the number added, or -1 on failure.
int sync_sibling_excludes(const char* folder, const SyncNames* sender_x, const SyncNames* receiver_x,
						  const SyncNames* receiver_siblings, SyncNames* out);

// The rules as client options ("--filter='- /music-player/' ...") and as an
// rsyncd.conf filter value ("- /music-player/ + ..."). -1 when out is too small.
int sync_shared_client_args(char* out, size_t size);
int sync_shared_daemon_filter(char* out, size_t size);

#endif
