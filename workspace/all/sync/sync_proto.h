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
int sync_names_find(const SyncNames* l, const char* name);	  // exact; index or -1
int sync_names_find_ci(const SyncNames* l, const char* name); // any case; first index or -1
bool sync_names_has_ci(const SyncNames* l, const char* name);
void sync_names_free(SyncNames* l);

// The display name before " (TAG)": "Game Boy Advance (MGBA)" -> "Game Boy
// Advance". False when folder isn't "Name (TAG)" or out is too small.
bool sync_console_display(const char* folder, char* out, size_t size);

// Two different folders of the same console (same display name).
bool sync_is_sibling(const char* a, const char* b);

// False for what isn't a game, by the launcher's own rule (hide() in
// common/utils.c): dot entries (.media, ._ junk), *.disabled and map.txt,
// the last in any case (on FAT, MAP.TXT opens as the map too).
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

// The client's phases: 1-4 states and saves (push shared, push saves, pull
// shared, pull saves), then BIOS both ways and collections on every sync, then
// ROMs both ways when ROM sync is on.
#define SYNC_PHASE_BIOS_PUSH 5
#define SYNC_PHASE_BIOS_PULL 6
#define SYNC_PHASE_COLLECTIONS 7
#define SYNC_PHASE_ROMS_PUSH 8
#define SYNC_PHASE_ROMS_PULL 9

int sync_phase_count(bool roms);
bool sync_is_bios_phase(int phase);
bool sync_is_roms_phase(int phase);

// BIOS files only fill gaps: a file the receiver already has is never
// replaced (the two devices may hold different dumps under one name).
// Dreamcast memory cards (vmu_save_*.bin) live in Saves/DC*, never in Bios/:
// an old copy left there must not travel (unanchored, at any depth).
#define SYNC_BIOS_EXCLUDE_PATTERN "vmu_save_*.bin"
#define SYNC_BIOS_ARGS "--ignore-existing --exclude='" SYNC_BIOS_EXCLUDE_PATTERN "'"

// Collections (Collections/<Name>.txt, one SD-relative game path per line)
// merge on every sync: each device keeps its lines in order and gains the
// other's lines it lacks, in the other's order. Nothing is ever removed.

// The client pulls the peer's collections into an empty temp folder and pushes
// back only the ones it changed, so no --update: the two clocks may disagree,
// and a file the peer saved "later" by its clock must still take the merge.
// The pull takes top-level *.txt files only (no folders, no alias map in any
// case). The daemon's [collections] keeps the alias map out for every client.
#define SYNC_COLLECTIONS_RSYNC_OPTS \
	"-rtv --no-perms --omit-dir-times --modify-window=1 --info=progress2 " SYNC_JUNK_CLIENT_ARGS
#define SYNC_MAP_PATTERN "/[mM][aA][pP].[tT][xX][tT]"
#define SYNC_COLLECTIONS_PULL_ARGS "--exclude='" SYNC_MAP_PATTERN "' --exclude='*/' --include='/*.[tT][xX][tT]' --exclude='*'"
#define SYNC_COLLECTIONS_DAEMON_EXCLUDE SYNC_MAP_PATTERN

// A collection file: "<Name>.txt" (extension in any case, as the launcher
// matches it) at the top level, not hidden by the launcher's rule (dot
// files, map.txt the alias map, *.disabled).
bool sync_is_collection_name(const char* name);

// A file's lines: split on \n, one trailing \r trimmed from each; a final
// newline makes no extra empty line. False when out of memory.
bool sync_text_lines(const char* text, size_t len, SyncNames* out);

// False when text holds a NUL byte: its lines can't be kept whole, so such a
// file is left as it is.
bool sync_text_is_lines(const char* text, size_t len);

// out = mine in order, then each line of theirs mine lacks (exact match), in
// theirs' order, once; blank lines (empty or whitespace only) are not added.
// Returns the number of lines appended (0: mine unchanged), -1 when out of
// memory.
int sync_merge_lines(const SyncNames* mine, const SyncNames* theirs, SyncNames* out);

// Against an older peer without the [bios] (or [collections]) module, rsync
// prints "@ERROR: Unknown module 'bios'" and exits 5 (couldn't start the
// protocol). Only then is a BIOS or collections phase skipped rather than a
// failed sync. Another exit 5 ("chdir failed", max connections) or any other
// failure fails as on other phases.
#define SYNC_RSYNC_EXIT_START 5
#define SYNC_RSYNC_UNKNOWN_MODULE "Unknown module"
bool sync_line_is_unknown_module(const char* line);
// unknown_module: the phase's output had a sync_line_is_unknown_module line.
bool sync_phase_skippable(int phase, int exit_code, bool unknown_module);

// The rules as client options ("--filter='- /music-player/' ...") and as an
// rsyncd.conf filter value ("- /music-player/ + ..."). -1 when out is too small.
int sync_shared_client_args(char* out, size_t size);
int sync_shared_daemon_filter(char* out, size_t size);

#endif
