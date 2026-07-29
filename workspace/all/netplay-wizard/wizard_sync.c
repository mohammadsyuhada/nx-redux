/*
 * Wizard save sync — rsyncd on the host, one-shot pull on the client.
 *
 * The host publishes exactly one module, [sync] over serve_dir, on
 * WIZ_RSYNC_PORT, and only for the length of one handshake: wizard_net.c starts
 * it after the HELLO exchange and stops it the moment the client reports back
 * (wizard_net.c:733/747/755). Two lines of its config are the whole security
 * posture, and they are the two lines NOT copied from Device Sync's rsyncd.conf
 * (sync.c:384-409, which offers three writable modules to anyone who asks):
 *
 *     read only = true     the client pulls; nothing on the wire may write here
 *     hosts allow = <ip>   only the peer that just completed the handshake
 *
 * EXEC SAFETY. The names that reach wiz_sync_pull() are remote input — they
 * arrive as FILE lines on the control channel. wizard_net.c:369 whitelists them
 * to [A-Za-z0-9._-]+, but a whitelist upstream is a second line of defence and
 * never a licence to interpolate, so the pull runs posix_spawn(RSYNC_BIN, argv)
 * with NO SHELL ANYWHERE. There is no command string to quote and therefore
 * nothing for a quote, space, semicolon or backtick in a name to break out of;
 * the whitelist and the spawn each stand on their own. sync.c:546 uses popen()
 * (which is a shell) for the same job, and this file deliberately parts company
 * with it there — that is the one pattern from sync.c not copied.
 *
 * The two system() calls that remain — starting the daemon and killall rsync —
 * carry no data at all: every byte of both command strings is a compile-time
 * constant. What does vary (serve_dir, the peer's address) goes into the config
 * FILE, never onto a command line, and is checked on the way in for the one
 * character that would matter there.
 *
 * ALL OR NOTHING, and it is the staging directory that makes that literal. Every
 * file is pulled into fetch_to/.netplay-staging and renamed into fetch_to only
 * once the WHOLE set has arrived; a failure removes the staging directory and
 * leaves fetch_to exactly as it was. Deleting the fetched names out of fetch_to
 * instead — the obvious approach — cannot distinguish a file rsync wrote from
 * one it SKIPPED as already up to date (`-t` without `--checksum` compares size
 * and mtime), so a set that failed half way would delete intact local saves the
 * transfer never touched. A half-synced VMU set must never reach the game, and
 * neither must a half-deleted one.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // O_DIRECTORY, unlinkat, posix_spawn
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "api.h"
#include "defines.h"
#include "ui_buttonhintbar.h"
#include "ui_list.h"
#include "ui_message.h"
#include "wizard.h"

// posix_spawn's environment. POSIX leaves this one to the caller to declare.
extern char** environ;

// rsync binary path (shared across platforms). Same definition as sync.c:41,
// duplicated rather than shared because nothing in sync.c is linkable here.
#define RSYNC_BIN SHARED_BIN_PATH "/rsync"

// How long the daemon gets to write its pidfile before we call it dead
// (sync.c:423).
#define WIZ_SYNC_DAEMON_SETTLE_US 500000

// Hard ceilings on the client's pull, in the order they bind: one file may not
// hold the UI longer than FILE, and the whole set may not exceed TOTAL.
//
// CROSS-FILE INVARIANT. The client must reach its SYNC-FAIL before the host
// stops listening, and FOUR constants in this file govern that, not one. Every
// step between the deadline firing and wizard_net.c:1118 sending the line:
//
//     WIZ_SYNC_TOTAL_TIMEOUT_MS    23000   the budget itself
//   + WIZ_SYNC_POLL_US as ms         100   deadline is read once per poll
//   + WIZ_SYNC_REAP_GRACE_MS        2000   worst case waiting for the child
//   + WIZ_SYNC_ERROR_TIMEOUT_MS     2000   drawn BEFORE wiz_sync_pull returns
//   ---------------------------------------
//                                  27100 < 30000 (WIZ_NET_PULL_TIMEOUT_MS)
//
// 2.9 s of margin, and it is not all spare: the host starts its 30 s clock when
// it sends SYNC-READY, while the client starts this budget only after reading
// the n FILE lines that follow, so that difference is charged here too.
//
// All four are load-bearing. Raising the error hold to wizard_net.c's 5 s idiom
// inverts the ordering just as surely as raising TOTAL would, and the reap grace
// joined the sum the moment wiz_reap() replaced a bare waitpid(). The 30 s on
// the other side is wizard_net.c's, not this file's to move; the margin is
// restored from here. The native test asserts this sum across both files.
#define WIZ_SYNC_FILE_TIMEOUT_MS 15000
#define WIZ_SYNC_TOTAL_TIMEOUT_MS 23000
// rsync's own timeouts: the polite version of the two above, so a peer that
// vanished fails with rsync's own error before anything gets SIGKILLed. Both are
// supported by the bundled binary (skeleton/SYSTEM/shared/bin/rsync).
#define WIZ_SYNC_IO_TIMEOUT_S 8
#define WIZ_SYNC_CONNECT_TIMEOUT_S 4

// Progress poll cadence, and so also the longest the read loop can go without
// noticing its own deadline.
#define WIZ_SYNC_POLL_US 100000
// Deliberately shorter than wizard_net.c's 5 s: it is inside the SYNC-FAIL
// budget above, and see wiz_sync_error().
#define WIZ_SYNC_ERROR_TIMEOUT_MS 2000

// How long a child that closed its output gets to exit before the whole process
// group is killed, and how often that is checked. Nothing here ever waits on a
// child without a bound.
#define WIZ_SYNC_REAP_GRACE_MS 2000
#define WIZ_SYNC_REAP_POLL_US 20000

// Must equal the literal in wizard.h's wiz_sync_pull() signature: the parameter
// is char names[][64] and this is what indexes it here.
#define WIZ_SYNC_NAME_MAX 64
// Longest run of rsync output held at once. --info=progress2 emits one short
// line per redraw, so this is slack, not a budget.
#define WIZ_SYNC_LINE_MAX 512
// Where a pull lands before any of it is allowed into fetch_to. Inside fetch_to
// so the commit is a same-filesystem rename; leading dot so a directory listing
// (and NextUI's own file browsers, which hide dotfiles) does not show it.
#define WIZ_SYNC_STAGE_DIR ".netplay-staging"
// Longest path this file builds: fetch_to + '/' + staging + '/' + a 63-char name.
#define WIZ_SYNC_PATH_MAX 512

//////////////////////////////////
// Screens
//////////////////////////////////

// Terminal message, the twin of wizard_net.c's wiz_net_error() and
// wizard_wifi.c's wiz_error(): same frame, same dismissal, file-local to this
// stage of the wizard. One deliberate difference — the hold is shorter, because
// every caller of the functions below draws its own error straight after a -1
// (wizard_net.c:736 and :1119), and two five-second screens back to back read as
// a hang. This one names what broke; the caller's names what it means for the
// game.
static void wiz_sync_error(const char* message) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	UI_renderButtonHintBar(wiz_screen, (char*[]){"A", "OKAY", NULL});
	GFX_flip(wiz_screen);

	uint32_t start = SDL_GetTicks();
	while (SDL_GetTicks() - start < WIZ_SYNC_ERROR_TIMEOUT_MS) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			break;
		GFX_sync();
	}
}

// Caption counts files, bar counts bytes of the file in flight. Drawn straight
// rather than through the frame loop: the read loop it belongs to is blocked on
// rsync between calls, so there is no frame to be in step with — the same window
// the host spends inside wiz_wait_line() with B disabled.
static void wiz_sync_render_progress(int index, int count, int percent) {
	char message[64];
	int bar_w = wiz_screen->w - SCALE1(PADDING * 8);
	int bar_h = SCALE1(12);
	int bar_x = SCALE1(PADDING * 4);
	int bar_y = wiz_screen->h / 2 + SCALE1(10);
	int fill_w;

	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	fill_w = (bar_w * percent) / 100;

	snprintf(message, sizeof(message), "Copying saves... (%d/%d)", index + 1, count);

	GFX_clear(wiz_screen);
	// GFX_blitText centres inside dst_rect, so a full-width rect is a centred
	// line (wizard_net.c:146 does the same).
	GFX_blitText(font.medium, message, 0, COLOR_WHITE, wiz_screen,
				 &(SDL_Rect){0, bar_y - SCALE1(FONT_MEDIUM + PADDING), wiz_screen->w,
							 SCALE1(FONT_MEDIUM)});

	UI_renderRoundedRectBg(wiz_screen, bar_x, bar_y, bar_w, bar_h, RGB_DARK_GRAY);
	// UI_fillRoundedRect clamps its radius to w/2, so a fill narrower than the
	// corner radius still draws; no minimum width needed.
	if (fill_w > 0)
		UI_renderRoundedRectBg(wiz_screen, bar_x, bar_y, fill_w, bar_h, THEME_COLOR1);

	GFX_flip(wiz_screen);
}

//////////////////////////////////
// Pure helpers
//////////////////////////////////
//
// No sockets, no SDL, no daemon: text in, text out. Same split as wizard_net.c's
// wire-protocol section and for the same reason — this is the half that can be
// exercised on a development host, and it is the half that handles remote input.

// A value on its way into rsyncd.conf. That file is line-oriented (one directive
// per line, value to end of line), so a \n or \r inside a value would end the
// directive early and start one the caller never wrote — "hosts allow = x\nhosts
// allow = *" being the case that matters. Both values written here are trusted by
// construction; this is what makes that assumption checkable rather than assumed.
static bool wiz_conf_value_is_safe(const char* value) {
	if (!value || !value[0])
		return false;

	for (const char* c = value; *c; c++)
		if (*c == '\n' || *c == '\r')
			return false;

	return true;
}

// A dotted quad, by shape. `hosts allow` also accepts names, ranges and
// patterns, so anything that is not digits and dots would WIDEN that ACL rather
// than narrow it ("*" being the extreme). wizard_net.c fills peer_ip from
// inet_ntop() and the client dials an address read the same way, so [0-9.] is
// the entire alphabet either side can produce. Shape only — this is not an
// address parser, and rsync validates the value itself.
static bool wiz_ip_is_plain(const char* ip) {
	int digits = 0;

	if (!ip || !ip[0] || strlen(ip) >= 16)
		return false;

	for (const char* c = ip; *c; c++) {
		if (*c >= '0' && *c <= '9') {
			digits++;
			continue;
		}
		if (*c == '.')
			continue;
		return false;
	}

	return digits > 0;
}

// The whitelist wizard_net.c:369 applies on the wire, re-applied at this module's
// own boundary. Not redundancy for its own sake:
//   - these names are remote input and this file hands them to an exec and to
//     unlinkat(); a '/' or a ".." would reach outside fetch_to either way,
//   - wiz_name_is_safe() is static to wizard_net.c and cannot be seen from here,
//     and
//   - a caller added later (a bench harness, Task 7's launch glue) must not be
//     able to skip the check by not being wizard_net.c.
// Deliberately identical to that one, down to the explicit ranges instead of
// isalnum() (locale-dependent); if either moves, both move.
static bool wiz_sync_name_is_safe(const char* name) {
	if (!name || !name[0])
		return false;
	if (strlen(name) >= WIZ_SYNC_NAME_MAX)
		return false;
	if (name[0] == '-') // rsync would read it as an option
		return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;

	for (const char* c = name; *c; c++) {
		if (*c >= 'A' && *c <= 'Z')
			continue;
		if (*c >= 'a' && *c <= 'z')
			continue;
		if (*c >= '0' && *c <= '9')
			continue;
		if (*c == '.' || *c == '_' || *c == '-')
			continue;
		return false;
	}

	return true;
}

// The host's rsyncd.conf, written to an already-open stream so the exact file
// can be checked without starting a daemon. Diverges from sync.c's
// write_rsync_config() in four ways, all of them the security posture: one
// module instead of three, read only = true instead of no, a hosts allow naming
// the single peer instead of no ACL at all, and list = false.
//
// list = false is not decoration. `hosts allow` is enforced when a MODULE is
// opened, not when the daemon is asked to enumerate them, so without it a device
// that is not the paired peer still gets "sync" back from
// `rsync rsync://<host>:18731/` (verified against the shipped rsync 3.2.0dev:
// the pull is refused with "@ERROR: access denied" but the listing is served).
// With it the listing comes back empty and an explicit pull by name still works,
// which is the only access this protocol needs.
//
// transfer logging is left off for the same family of reasons: the log would
// otherwise name every file the peer read, and it lives in /tmp.
// 0 = the whole file was written, -1 = the stream errored.
static int wiz_write_serve_conf(FILE* fp, const char* serve_dir, const char* client_ip) {
	fprintf(fp, "pid file = %s\n", WIZ_RSYNC_PID);
	fprintf(fp, "port = %d\n", WIZ_RSYNC_PORT);
	fprintf(fp, "use chroot = no\n");
	fprintf(fp, "uid = 0\n");
	fprintf(fp, "gid = 0\n");
	fprintf(fp, "log file = %s\n", WIZ_RSYNC_LOG);
	fprintf(fp, "hosts allow = %s\n\n", client_ip);
	fprintf(fp, "[sync]\n");
	fprintf(fp, "  path = %s\n", serve_dir);
	fprintf(fp, "  read only = true\n");
	fprintf(fp, "  list = false\n");

	return ferror(fp) ? -1 : 0;
}

// Index of the first name that repeats an earlier one, or -1 when the set is
// what it claims to be: a set. O(n²) over at most WIZ_NET_SYNC_MAX_FILES = 32
// names, i.e. free.
static int wiz_find_duplicate(char names[][WIZ_SYNC_NAME_MAX], int count) {
	for (int i = 1; i < count; i++)
		for (int j = 0; j < i; j++)
			if (strcmp(names[i], names[j]) == 0)
				return i;

	return -1;
}

// rsync://<host>:<port>/sync/<name>. One file, one URL — split out so the exact
// string handed to the exec can be checked without a daemon.
static void wiz_build_pull_url(char* out, size_t out_size, const char* host_ip, const char* name) {
	snprintf(out, out_size, "rsync://%s:%d/sync/%s", host_ip, WIZ_RSYNC_PORT, name);
}

// The percentage an --info=progress2 line carries, or -1 for a line that carries
// none (rsync's banners, blank lines). The line reads
//
//     131,072 100%   12.50MB/s    0:00:00 (xfr#1, to-chk=0/1)
//
// so the first '%' with digits in front of it is the transfer percentage; the
// rate that follows carries no '%' of its own. Bytes, not files, because this bar
// is per-file: sync.c:450 counts files out of "to-chk=X/Y", which for a
// single-file pull only ever reads 0% then 100%.
static int wiz_progress_percent(const char* line) {
	for (const char* c = line; *c; c++) {
		const char* start = c;
		int percent;

		if (*c != '%')
			continue;

		while (start > line && start[-1] >= '0' && start[-1] <= '9')
			start--;
		if (start == c)
			continue; // a '%' with no number in front of it

		percent = atoi(start);
		if (percent < 0)
			percent = 0;
		if (percent > 100)
			percent = 100;
		return percent;
	}

	return -1;
}

// Consume every complete line in buf, returning the newest percentage they
// carried (or `current` when none did) and moving the unterminated tail to the
// front. Lines end with \r OR \n: --info=progress2 redraws in place with \r and
// only ends with \n, which is why sync.c:480 reads the same way. This one works
// off a buffer rather than fgetc() because the caller has to select() on the same
// descriptor for its deadline, and stdio's buffer is invisible to select().
//
// A buffer that fills up with no terminator in it at all is not progress output,
// so it is dropped rather than grown — which is also what guarantees the caller
// always has room to read into.
static int wiz_take_percent(char* buf, size_t size, size_t* len, int current) {
	size_t start = 0;
	int percent = current;

	for (size_t i = 0; i < *len; i++) {
		if (buf[i] != '\r' && buf[i] != '\n')
			continue;

		buf[i] = '\0';
		if (i > start) {
			int found = wiz_progress_percent(buf + start);
			if (found >= 0)
				percent = found;
		}
		start = i + 1;
	}

	if (start > 0) {
		memmove(buf, buf + start, *len - start);
		*len -= start;
	} else if (*len == size) {
		*len = 0;
	}

	return percent;
}

//////////////////////////////////
// Staging directory
//////////////////////////////////
//
// Nothing the host sends is written into fetch_to. Every file lands in
// fetch_to/.netplay-staging first and is renamed in only once the WHOLE set has
// arrived, which is what makes the all-or-nothing rule true rather than
// approximately true.
//
// The rule this replaces — pull straight into fetch_to and unlink the names
// again on failure — cannot tell "rsync wrote this" from "this was already
// here". `-t` without `--checksum` SKIPS a file whose size and mtime already
// match, so two devices that synced yesterday and fail today would have had an
// intact, untouched local save deleted by the failure path. That is silent save
// loss on a device whose entire purpose is saves.
//
// No SDL and no network below: filesystem only, so the whole flow is exercisable
// on a development host.

// fetch_to/.netplay-staging.
static void wiz_stage_path(char* out, size_t out_size, const char* fetch_to) {
	snprintf(out, out_size, "%s/%s", fetch_to, WIZ_SYNC_STAGE_DIR);
}

// The staging directory and everything in it, gone. Runs as the failure path,
// after a successful commit (by then it is empty), and again before a pull
// starts — a directory that already exists belongs to a run that was killed, so
// it is neither ours to commit nor the user's to keep.
//
// ONE LEVEL DEEP, deliberately. rsync is invoked without -r and without -d, so
// it never creates a directory in the destination; the only things in here are
// this file's own single-file pulls. The AT_REMOVEDIR fallback below therefore
// only ever has to clear an EMPTY directory left by something else. A non-empty
// one would survive, rmdir() would fail, and the next wiz_stage_prepare() would
// then fail on mkdir() and take wiz_sync_pull() out with fetch_to untouched —
// i.e. this fails closed, which is why it does not need to recurse.
static void wiz_stage_purge(const char* fetch_to) {
	char path[WIZ_SYNC_PATH_MAX];
	DIR* dir;

	wiz_stage_path(path, sizeof(path), fetch_to);

	dir = opendir(path);
	if (dir) {
		int dir_fd = dirfd(dir);
		struct dirent* entry;

		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
				continue;
			if (dir_fd < 0)
				continue;
			// Only ever our own pulls in here, but a directory left by anything
			// else must not stop the purge from finishing.
			if (unlinkat(dir_fd, entry->d_name, 0) != 0)
				unlinkat(dir_fd, entry->d_name, AT_REMOVEDIR);
		}

		closedir(dir);
	}

	rmdir(path);
}

// An empty staging directory, ready to receive. 0 = it exists and is empty.
static int wiz_stage_prepare(const char* fetch_to) {
	char path[WIZ_SYNC_PATH_MAX];

	wiz_stage_purge(fetch_to);
	wiz_stage_path(path, sizeof(path), fetch_to);

	return mkdir(path, 0755) == 0 ? 0 : -1;
}

// Move the finished set into fetch_to, one rename() per name. Same filesystem by
// construction (the staging directory is inside fetch_to), so each rename is
// atomic and replaces a local save only at the instant the whole set is known
// good. 0 = every name moved.
//
// A failure part-way leaves the earlier renames in place: POSIX cannot move a
// set atomically. That window is the microseconds between two renames in a
// directory this process owns, against the multi-second transfer window the
// staging directory removes — and every rename that did land is a complete,
// correct file, so a retry simply redoes them. Best effort rather than
// stop-at-first-failure for the same reason: the closer to the full set, the
// better off both sides are.
static int wiz_stage_commit(const char* fetch_to, char names[][WIZ_SYNC_NAME_MAX], int count) {
	char path[WIZ_SYNC_PATH_MAX];
	int stage_fd;
	int dest_fd;
	int result = 0;

	wiz_stage_path(path, sizeof(path), fetch_to);

	stage_fd = open(path, O_RDONLY | O_DIRECTORY);
	if (stage_fd < 0)
		return -1;

	dest_fd = open(fetch_to, O_RDONLY | O_DIRECTORY);
	if (dest_fd < 0) {
		close(stage_fd);
		return -1;
	}

	for (int i = 0; i < count; i++)
		if (renameat(stage_fd, names[i], dest_fd, names[i]) != 0)
			result = -1;

	close(dest_fd);
	close(stage_fd);
	return result;
}

//////////////////////////////////
// Daemon (host side)
//////////////////////////////////

// Idempotent, and called on every exit path including wiz_sync_serve_start()'s
// own failures. Clone of sync.c:430-442 plus the log.
void wiz_sync_serve_stop(void) {
	FILE* fp = fopen(WIZ_RSYNC_PID, "r");

	if (fp) {
		int pid = 0;
		if (fscanf(fp, "%d", &pid) == 1 && pid > 0)
			kill(pid, SIGTERM);
		fclose(fp);
		unlink(WIZ_RSYNC_PID);
	}

	// A daemon that died before writing its pidfile leaves no other handle, so
	// this is not belt-and-braces (sync.c:440 does the same for the same
	// reason). Safe here: the only other rsync user on the device is sync.elf,
	// a launcher app, which cannot be running while launch.sh has a game
	// mid-launch — and the wizard's own pull is a client that has already
	// finished by the time anything calls this.
	system("killall rsync 2>/dev/null");

	unlink(WIZ_RSYNC_CONF);
	// Not in sync.c: the log names the peer and every file it read, and /tmp
	// outlives this process. The session ends, the record goes with it.
	unlink(WIZ_RSYNC_LOG);
}

int wiz_sync_serve_start(const char* serve_dir, const char* client_ip) {
	char cmd[512];
	FILE* fp;
	int wrote;

	// serve_dir is launch.sh argv (DC.pak's own config, trusted local input) and
	// client_ip is inet_ntop() output from the accepted socket (digits and dots
	// by construction). Both assumptions are checked rather than relied on,
	// because the file they land in is parsed as configuration.
	if (!wiz_conf_value_is_safe(serve_dir) || !wiz_ip_is_plain(client_ip)) {
		wiz_sync_error("Could not share saves.");
		return -1;
	}

	if (access(RSYNC_BIN, X_OK) != 0) {
		// Nothing created yet, so nothing to unwind.
		wiz_sync_error("Save sharing is not\navailable on this device.");
		return -1;
	}

	// Start from a known state. Two different leftovers are possible after a
	// launch that was killed mid-session: a pidfile with nothing behind it,
	// which would make the check below pass without a daemon (rsync writes that
	// file only after it binds), and a daemon still holding port 18731, which
	// would make the new one fail to bind. Both are the teardown that never ran,
	// so run it.
	wiz_sync_serve_stop();

	fp = fopen(WIZ_RSYNC_CONF, "w");
	if (!fp) {
		wiz_sync_error("Could not share saves.");
		return -1;
	}

	// From here on the process owns state on disk, and every failure path below
	// hands it back before returning.
	wrote = wiz_write_serve_conf(fp, serve_dir, client_ip);
	if (fclose(fp) != 0)
		wrote = -1;

	if (wrote != 0) {
		wiz_sync_serve_stop();
		wiz_sync_error("Could not share saves.");
		return -1;
	}

	// No data in this command line: both halves are compile-time constants, so
	// system() here carries none of the risk the pull's argv would.
	snprintf(cmd, sizeof(cmd), "%s --daemon --config=%s", RSYNC_BIN, WIZ_RSYNC_CONF);
	if (system(cmd) != 0) {
		wiz_sync_serve_stop();
		wiz_sync_error("Could not start the\nsave server.");
		return -1;
	}

	usleep(WIZ_SYNC_DAEMON_SETTLE_US);
	if (access(WIZ_RSYNC_PID, F_OK) != 0) {
		// Forked cleanly and then refused to bind (port already held, serve_dir
		// unreadable). The daemon is the one thing killall can still catch.
		wiz_sync_serve_stop();
		wiz_sync_error("Could not start the\nsave server.");
		return -1;
	}

	return 0;
}

//////////////////////////////////
// Pull (client side)
//////////////////////////////////

// Start rsync with no shell between us and it: argv reaches the kernel as it
// stands, so nothing in it is parsed for metacharacters (see the file header).
// stdout and stderr both land on the returned descriptor — the shell's "2>&1"
// without the shell. 0 = *out_fd and *out_pid are the caller's to close and reap.
//
// The child is put in its own PROCESS GROUP, which is load-bearing rather than
// tidy. rsync in receiving mode forks: the process spawned here becomes the
// generator and a child of it becomes the receiver — and the receiver is the one
// that opens and writes the destination file. On a clean exit the generator
// signals the receiver itself, but a SIGKILL aimed at the generator alone skips
// that, and the receiver is reparented to init and keeps transferring at full
// rate (measured: still growing the destination ten seconds after the kill,
// closing our end of the pipe does not stop it — it talks to the generator over
// its own pipe, not stdout). That orphan would outlive the wizard into the game
// session, holding WiFi, CPU, the wizard's inherited sockets, and could re-create
// a file after the staging directory was discarded. pgid = the child's own pid
// means kill(-pid) reaches the whole family and nothing survives.
//
// The trade this makes, stated because it is a real one: rsync is no longer in
// the wizard's process group, so a signal sent to THAT group (a launcher killing
// the whole job) no longer reaches it. Contained by where it can write — only
// inside fetch_to/.netplay-staging, which is never a save the game reads and
// which the next wiz_stage_prepare() purges — and bounded by rsync's own
// --timeout. Every path this file takes kills the group explicitly, so the
// window is a wizard killed from outside, not one that returned.
static int wiz_spawn_rsync(char* const argv[], int* out_fd, pid_t* out_pid) {
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attr;
	int fds[2];
	pid_t pid = -1;
	int rc;

	*out_fd = -1;
	*out_pid = -1;

	if (pipe(fds) != 0)
		return -1;

	if (posix_spawn_file_actions_init(&actions) != 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (posix_spawnattr_init(&attr) != 0) {
		posix_spawn_file_actions_destroy(&actions);
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
	posix_spawnattr_setpgroup(&attr, 0); // 0 = "your own pid", i.e. a new group

	posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
	// Actions run in order, so these close the originals after the dup2s above
	// have copied them. Closing our read end in the child is what makes read()
	// see EOF when rsync exits; the guard covers the (unreachable here, but free)
	// case of a pipe landing on a standard descriptor, where closing it would
	// close the output we just redirected.
	if (fds[0] > STDERR_FILENO)
		posix_spawn_file_actions_addclose(&actions, fds[0]);
	if (fds[1] > STDERR_FILENO)
		posix_spawn_file_actions_addclose(&actions, fds[1]);

	rc = posix_spawn(&pid, RSYNC_BIN, &actions, &attr, argv, environ);
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&actions);
	close(fds[1]);

	if (rc != 0) {
		close(fds[0]);
		return -1;
	}

	*out_fd = fds[0];
	*out_pid = pid;
	return 0;
}

// Reap the spawned generator without ever waiting on it indefinitely. A child
// that has closed its output is exiting and needs no help, so it gets grace_ms
// of WNOHANG polling; anything still there after that has the whole process
// group killed under it. The final wait is bounded by SIGKILL being uncatchable.
//
// Only the generator is reaped, and that is correct: once the group is dead the
// receiver belongs to init. 0 = *status is valid.
static int wiz_reap(pid_t pid, int* status, uint32_t grace_ms) {
	uint32_t start = SDL_GetTicks();

	while (SDL_GetTicks() - start < grace_ms) {
		pid_t got = waitpid(pid, status, WNOHANG);

		if (got == pid)
			return 0;
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return -1; // ECHILD: nothing of ours left to wait for
		}

		usleep(WIZ_SYNC_REAP_POLL_US);
	}

	kill(-pid, SIGKILL);

	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR)
			continue;
		return -1;
	}

	return 0;
}

// One file, start to finish, into the STAGING directory — never into fetch_to.
// index/count drive the caption only; deadline is an absolute SDL_GetTicks()
// value. No message is drawn here — the caller draws one for the pull as a
// whole. 0 = the file is staged, -1 = anything else, with *expired telling the
// caller which kind of failure to name.
static int wiz_pull_one(const char* host_ip, const char* stage_dir, const char* name,
						int index, int count, uint32_t deadline, bool* expired) {
	char url[128 + WIZ_SYNC_NAME_MAX];
	char dest[WIZ_SYNC_PATH_MAX];
	char io_timeout[32];
	char con_timeout[32];
	char buf[WIZ_SYNC_LINE_MAX];
	size_t len = 0;
	int percent = 0;
	int shown = -1;
	int fd = -1;
	pid_t pid = -1;
	int status = 0;
	bool eof = false;
	bool timed_out = false;

	// Set before the first return so no caller can read a previous file's value.
	*expired = false;

	wiz_build_pull_url(url, sizeof(url), host_ip, name);
	// Trailing slash: the destination is a directory. Without it rsync would
	// treat a stage_dir that does not exist yet as the filename to write.
	snprintf(dest, sizeof(dest), "%s/", stage_dir);
	snprintf(io_timeout, sizeof(io_timeout), "--timeout=%d", WIZ_SYNC_IO_TIMEOUT_S);
	snprintf(con_timeout, sizeof(con_timeout), "--contimeout=%d", WIZ_SYNC_CONNECT_TIMEOUT_S);

	// No -r and no -l, deliberately: every transfer here is one named regular
	// file, so there is no directory to walk and no symlink to preserve. A host
	// that offers a symlink under a whitelisted name gets "skipping non-regular
	// file" out of rsync itself instead of planting a link that points into the
	// client's filesystem. -t keeps the mtime, --inplace matches sync.c and
	// avoids a second copy on a small tmpfs, --no-perms keeps the host's mode
	// bits off our files.
	//
	// argv, not a command string: the only element carrying remote input is url,
	// and it is one element whatever bytes it holds. Its "rsync://" prefix also
	// means it can never look like an option, whatever the name contributed.
	char* argv[] = {(char*)RSYNC_BIN, "-t", "--inplace", "--no-perms",
					"--info=progress2", io_timeout, con_timeout, url, dest, NULL};

	if (wiz_spawn_rsync(argv, &fd, &pid) != 0)
		return -1;

	wiz_sync_render_progress(index, count, 0);

	while (1) {
		fd_set fds;
		struct timeval tv = {0, WIZ_SYNC_POLL_US};
		int sel;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		sel = select(fd + 1, &fds, NULL, NULL, &tv);
		if (sel > 0) {
			ssize_t got = read(fd, buf + len, sizeof(buf) - len);

			if (got == 0) {
				eof = true; // both ends closed: rsync has exited or is about to
				break;
			}
			if (got > 0) {
				len += (size_t)got;
				percent = wiz_take_percent(buf, sizeof(buf), &len, percent);
			} else if (errno != EINTR) {
				break; // read error: rsync is still out there, kill it below
			}
			// EINTR falls through to the deadline check rather than restarting
			// the loop: a signal must never buy another full iteration without
			// the deadline being looked at.
		} else if (sel < 0 && errno != EINTR) {
			break;
		}

		if (percent != shown) {
			wiz_sync_render_progress(index, count, percent);
			shown = percent;
		}

		// The one thing that must not happen here is an unbounded wait. This
		// loop does not poll PAD (the pull is not cancelable, symmetrically with
		// the host's send window at wizard_net.c:754) and wizard.c holds
		// PWR_disablePowerOff for the whole run, so a peer that stops talking
		// mid-file would otherwise leave a device the user cannot even switch
		// off. rsync's --timeout is the polite version of this; SIGKILL is the
		// version that always works. Signed difference so the comparison
		// survives SDL_GetTicks() wrapping.
		if ((int32_t)(SDL_GetTicks() - deadline) >= 0) {
			timed_out = true;
			break;
		}
	}

	close(fd);

	// EOF is the ONLY exit that leaves rsync to finish on its own; a timeout, a
	// read error and a select error all leave a child still running, and each of
	// them used to fall into a blocking waitpid() that would never return. Every
	// one of them now kills the process group first, and wiz_reap() bounds the
	// wait even on the EOF path.
	if (!eof)
		kill(-pid, SIGKILL);

	*expired = timed_out;

	if (wiz_reap(pid, &status, WIZ_SYNC_REAP_GRACE_MS) != 0)
		return -1;
	if (!eof || timed_out)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	// Exit 0 is not the same as "the file is here". A host that offers a symlink
	// under a whitelisted name gets `skipping non-regular file` out of rsync —
	// which is the refusal we want, but rsync still exits 0, and the client would
	// otherwise start a netplay session missing a save both sides think it has.
	// Verified against the shipped rsync 3.2.0dev. Nothing pre-exists in the
	// staging directory, so unlike a check against fetch_to this one cannot be
	// satisfied by a stale file from an earlier session. Concatenation is safe
	// here: the name passed wiz_sync_name_is_safe() before anything was spawned.
	snprintf(dest, sizeof(dest), "%s/%s", stage_dir, name);
	if (access(dest, F_OK) != 0)
		return -1;

	// rsync says nothing at all for a file that was already up to date, which
	// would otherwise leave the bar wherever the last line put it.
	wiz_sync_render_progress(index, count, 100);
	return 0;
}

int wiz_sync_pull(const char* host_ip, const char* fetch_to,
				  char names[][64], int name_count) {
	char stage[WIZ_SYNC_PATH_MAX];
	uint32_t start = SDL_GetTicks();
	bool expired = false;

	// SYNC-READY 0 is a legal offer — the host had files when it listed them and
	// none when it served them, or a later protocol version says "nothing this
	// round". Nothing to copy is not a failure, and standing up a transfer for
	// zero files would only be waste. (wizard_net.c bounds n to 0..32 before it
	// gets here, so a negative count is unreachable; reading it as "nothing to
	// do" is still the safe way to read it.)
	if (name_count <= 0)
		return 0;

	if (!host_ip || !fetch_to || !names) {
		wiz_sync_error("Save transfer failed.");
		return -1;
	}

	if (!wiz_ip_is_plain(host_ip)) {
		wiz_sync_error("The host's address is\nnot usable.");
		return -1;
	}

	// Before the first spawn, not per file: nothing may be fetched until every
	// name in the set is one this file is prepared to write and to rename.
	for (int i = 0; i < name_count; i++) {
		if (!wiz_sync_name_is_safe(names[i])) {
			wiz_sync_error("The host offered an\nunexpected file.");
			return -1;
		}
		// ".netplay-staging" is itself a legal name under that whitelist, and a
		// host offering it would have the commit try to rename a file over the
		// directory it is renaming out of. Refused rather than special-cased.
		if (strcmp(names[i], WIZ_SYNC_STAGE_DIR) == 0) {
			wiz_sync_error("The host offered an\nunexpected file.");
			return -1;
		}
	}

	// A name twice is not a set, and nothing downstream dedupes. The repeat
	// would stage one file, and then the SECOND renameat() for that name would
	// fail with ENOENT — after the first had already replaced the local save.
	// That is a failure reported AFTER fetch_to was modified: precisely the
	// guarantee the staging directory exists to provide, broken deterministically
	// by two identical lines on the wire.
	//
	// Refused rather than skipped. A host that read its own directory cannot
	// produce a repeat (wizard_net.c:694 lists dirents), so a set containing one
	// comes from a peer that is buggy or hostile, and this file has no way to
	// tell which of the two copies it was meant to keep. Refusing costs a
	// conforming host nothing and leaves fetch_to untouched, which silently
	// keeping the first copy would not.
	if (wiz_find_duplicate(names, name_count) >= 0) {
		wiz_sync_error("The host offered the\nsame save twice.");
		return -1;
	}

	if (wiz_stage_prepare(fetch_to) != 0) {
		wiz_sync_error("Save transfer failed.");
		return -1;
	}

	wiz_stage_path(stage, sizeof(stage), fetch_to);

	for (int i = 0; i < name_count; i++) {
		uint32_t elapsed = SDL_GetTicks() - start;
		uint32_t left = (elapsed < WIZ_SYNC_TOTAL_TIMEOUT_MS)
							? WIZ_SYNC_TOTAL_TIMEOUT_MS - elapsed
							: 0;
		// Both ceilings, whichever binds first. A budget already spent yields a
		// zero-length window, which the read loop turns into an immediate
		// timeout — the correct outcome, not a special case.
		uint32_t window = (left < WIZ_SYNC_FILE_TIMEOUT_MS) ? left : WIZ_SYNC_FILE_TIMEOUT_MS;

		if (wiz_pull_one(host_ip, stage, names[i], i, name_count,
						 SDL_GetTicks() + window, &expired) != 0) {
			// The whole staging directory goes, part-transferred file included.
			// fetch_to has not been touched at all, so the client keeps exactly
			// the saves it started with.
			wiz_stage_purge(fetch_to);
			wiz_sync_error(expired ? "The host stopped\nsending saves."
								   : "Save transfer failed.");
			return -1;
		}
	}

	// Only now, with the whole set on disk, does anything reach fetch_to.
	if (wiz_stage_commit(fetch_to, names, name_count) != 0) {
		wiz_stage_purge(fetch_to);
		wiz_sync_error("Save transfer failed.");
		return -1;
	}

	// Empty by now; this just takes the directory itself back out of fetch_to.
	wiz_stage_purge(fetch_to);
	return 0;
}
