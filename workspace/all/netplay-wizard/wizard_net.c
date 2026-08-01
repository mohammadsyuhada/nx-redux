/*
 * Wizard rendezvous — UDP discovery, TCP control channel, handshake.
 *
 * One TCP control connection on WIZ_TCP_PORT carries newline-terminated ASCII
 * lines; the host advertises itself on WIZ_UDP_PORT so the WiFi-mode client can
 * list it. The exchange, in order:
 *
 *     client -> host:  HELLO 1 <game> client
 *     host -> client:  HELLO 1 <game> host      (or REJECT <reason>, then close)
 *     host -> client:  SYNC-READY <n>           (only when the host serves saves)
 *     host -> client:  FILE <name>              (n times, bare filenames)
 *     client -> host:  SYNC-DONE | SYNC-FAIL
 *     host -> client:  START
 *     client -> host:  START
 *
 * A space separates the fields, so a game title — and a save filename, same
 * reason — travels with each space replaced by \x1f; see the wire protocol
 * helpers. Every line stays parseable with one sscanf.
 *
 * A rejected or dropped client never ends the host's wait: the host closes that
 * connection and returns to its waiting screen. The client is the side that
 * gives up — back to its host list on WiFi, out of the wizard on hotspot.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "api.h"
#include "defines.h"
#include "network_common.h"
#include "ui_buttonhintbar.h"
#include "ui_list.h"
#include "ui_message.h"
#include "utils.h"
#include "wifi_direct.h"
#include "wizard.h"

// Longest control line accepted, terminator included. The longest line the
// protocol can produce is "HELLO 1 <game> client" with a 255-char game token.
#define WIZ_NET_LINE_MAX 512
// Widest single token. The sscanf widths below are written out as literals
// (a format string cannot interpolate a macro), so this and they must agree:
// every "%255s" in this file is WIZ_NET_TOKEN_MAX - 1.
#define WIZ_NET_TOKEN_MAX 256
// wiz_sync_pull() takes char names[][64]; a name that does not fit is refused
// rather than truncated.
#define WIZ_NET_NAME_MAX 64
#define WIZ_NET_SYNC_MAX_FILES 32
// Field separator substitute. Filenames and game titles carry spaces; \x1f
// (unit separator) is a control character, so neither can contain one.
#define WIZ_NET_SPACE_ESC '\x1f'

// Per-line ceiling on the control channel, as SO_RCVTIMEO and as the wall
// clock the wait loops enforce.
#define WIZ_NET_RECV_TIMEOUT_MS 5000
// The client's pull runs outside this protocol (rsync, Task 5) and only reports
// back when it is done, so its window is much wider than a line's.
#define WIZ_NET_PULL_TIMEOUT_MS 30000
#define WIZ_NET_CONNECT_TIMEOUT_MS 5000
// Hotspot only: the AP has just come up, so the first SYN can be dropped.
#define WIZ_NET_CONNECT_ATTEMPTS 3
#define WIZ_NET_RETRY_DELAY_MS 1500
#define WIZ_NET_BROADCAST_INTERVAL_US 1000000
#define WIZ_NET_DISCOVERY_POLL_MS 500
// Upper bound on a host list that never fills, mirroring wizard_wifi.c's
// pickers: the wizard sits between the launcher and the game and must never
// wait forever with nothing to pick.
#define WIZ_NET_LIST_TIMEOUT_MS 120000
#define WIZ_NET_ERROR_TIMEOUT_MS 5000
#define WIZ_NET_NOTICE_MS 1500
#define WIZ_NET_MAX_HOSTS NET_MAX_DISCOVERED_HOSTS
#define WIZ_NET_LABEL_MAX 96
// Discovery link_mode. Separates this wizard's broadcasts from any other user
// of NET_sendDiscoveryBroadcast that might share the port.
#define WIZ_NET_LINK_MODE "wizard"

// Backstop for a peer that reports readable and then stalls mid-line: every
// read here selects first, so this timeout should never be the one that fires.
static const NET_TCPConfig WIZ_NET_TCP_CONFIG = {
	.buffer_size = 65536,
	.recv_timeout_us = WIZ_NET_RECV_TIMEOUT_MS * 1000,
	.enable_keepalive = false};

//////////////////////////////////
// Screens
//////////////////////////////////

// wiz_net_status/wiz_net_error are deliberate twins of wizard_wifi.c's
// wiz_status/wiz_error: same frames, same dismissal, file-local to each stage
// of the wizard. Shared UI belongs in ../common/ui, which has no owner for
// "full-screen wizard message" yet; keep the two in step until it does.
static void wiz_net_status(const char* message) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	GFX_flip(wiz_screen);
}

// Terminal message. Auto-clears so a user who walked away is not left staring
// at it — every caller either returns -1 (the wizard exits and the game starts)
// or drops back to the host list.
static void wiz_net_error(const char* message) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	UI_renderButtonHintBar(wiz_screen, (char*[]){"A", "OKAY", NULL});
	GFX_flip(wiz_screen);

	uint32_t start = SDL_GetTicks();
	while (SDL_GetTicks() - start < WIZ_NET_ERROR_TIMEOUT_MS) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			break;
		GFX_sync();
	}
}

// Defined with the other wait loops below; the notice screen borrows it rather
// than sleeping, so it obeys the same frame pacing as every other wait here.
static int wiz_wait_ms(uint32_t ms, bool cancelable, const char* status);

// Non-terminal flash: the host uses it on its way back to the waiting screen,
// where a silent return would read as nothing having happened. An SDL_Delay
// here would be the longest button-dead window in the file outside the
// documented pull, with PWR_disablePowerOff held for the whole run.
static void wiz_net_notice(const char* message) {
	wiz_wait_ms(WIZ_NET_NOTICE_MS, false, message);
}

// GFX_blitText centres inside dst_rect, so a full-width rect at y is a centred
// line at y.
static void wiz_net_blit(TTF_Font* f, const char* text, int y) {
	GFX_blitText(f, text, 0, COLOR_WHITE, wiz_screen,
				 &(SDL_Rect){0, y, wiz_screen->w, SCALE1(FONT_LARGE)});
}

// The host's waiting screen: what the other player needs to type or know
// (hotspot code / local IP) above the instruction and the status line.
// Same three-tier layout as netplay_helper.c's hotspot/WiFi waiting screens.
static void wiz_net_render_waiting(const char* big, const char* instruction, const char* status) {
	int center_y = wiz_screen->h / 2;

	GFX_clear(wiz_screen);
	wiz_net_blit(font.large, big, center_y - SCALE1(FONT_LARGE + PADDING));
	wiz_net_blit(font.medium, instruction, center_y + SCALE1(5));
	wiz_net_blit(font.small, status, center_y + SCALE1(28));
	UI_renderButtonHintBar(wiz_screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(wiz_screen);
}

// The lobby variant of the waiting screen. Same three tiers, but the hint bar
// also offers A=START — the multi-join host (max_players > 2) starts the session
// on its own A press once at least one joiner is in. Kept a separate function so
// wiz_net_render_waiting's other callers (and the 2-player flow) keep the plain
// B-only hint unchanged.
static void wiz_net_render_lobby(const char* big, const char* instruction, const char* status) {
	int center_y = wiz_screen->h / 2;

	GFX_clear(wiz_screen);
	wiz_net_blit(font.large, big, center_y - SCALE1(FONT_LARGE + PADDING));
	wiz_net_blit(font.medium, instruction, center_y + SCALE1(5));
	wiz_net_blit(font.small, status, center_y + SCALE1(28));
	UI_renderButtonHintBar(wiz_screen, (char*[]){"A", "START", "B", "CANCEL", NULL});
	GFX_flip(wiz_screen);
}

// Message frame used while a control line is outstanding. cancelable drives the
// hint only; the wait loop owns the button.
static void wiz_net_render_wait_status(const char* message, bool cancelable) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	if (cancelable)
		UI_renderButtonHintBar(wiz_screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(wiz_screen);
}

// Host list, windowed the same way as wizard_wifi.c's pickers:
// UI_renderSimpleMenu draws every item it is handed, so the slice start is
// passed as the item array.
static void wiz_net_render_list(const char** labels, int count, int selected, int* scroll) {
	ListLayout layout = UI_calcListLayout(wiz_screen);
	UI_adjustListScroll(selected, scroll, layout.items_per_page);

	int visible = count - *scroll;
	if (visible > layout.items_per_page)
		visible = layout.items_per_page;

	SimpleMenuConfig config = {
		.title = "Select Host",
		.items = labels + *scroll,
		.item_count = visible,
		.btn_b_label = "BACK",
		.btn_a_label = "SELECT",
		.hide_controls_hint = true};
	UI_renderSimpleMenu(wiz_screen, selected - *scroll, &config);
	UI_renderScrollIndicators(wiz_screen, *scroll, layout.items_per_page, count);
	GFX_flip(wiz_screen);
}

static void wiz_net_render_empty(const char* message) {
	SimpleMenuConfig config = {
		.title = "Select Host",
		.items = NULL,
		.item_count = 0,
		.btn_b_label = "BACK",
		.btn_a_label = "SELECT",
		.hide_controls_hint = true};
	UI_renderSimpleMenu(wiz_screen, 0, &config);
	UI_renderCenteredMessage(wiz_screen, message);
	GFX_flip(wiz_screen);
}

//////////////////////////////////
// Wire protocol helpers
//////////////////////////////////
//
// Pure text in, pure text out — no sockets, no SDL, no wizard state. Kept that
// way on purpose: this is the half of the protocol that can be exercised on a
// development host, and every parse below is fed by a remote peer.

// Copies in to out with each space replaced by WIZ_NET_SPACE_ESC. Always
// NUL-terminates; a value too long for out is truncated, which the receiver
// then reads as a mismatch rather than as a shorter name.
static void wiz_esc_spaces(const char* in, char* out, size_t out_size) {
	size_t i = 0;

	if (!out || out_size == 0)
		return;

	for (; in && in[i] && i + 1 < out_size; i++)
		out[i] = (in[i] == ' ') ? WIZ_NET_SPACE_ESC : in[i];

	out[i] = '\0';
}

// In-place inverse. Idempotent for a peer that sent no escapes at all, which is
// what makes a space-free game title interoperable either way.
static void wiz_unesc_spaces(char* s) {
	for (; s && *s; s++)
		if (*s == WIZ_NET_SPACE_ESC)
			*s = ' ';
}

// 0 = parsed a syntactically valid HELLO. game/role come back still escaped.
static int wiz_parse_hello(const char* line, int* version, char* game, size_t game_size,
						   char* role, size_t role_size, int* player_num) {
	char verb[16];
	char game_tok[WIZ_NET_TOKEN_MAX];
	char role_tok[16];
	int ver = 0, pnum = 0;

	int fields = sscanf(line, "%15s %d %255s %15s %d", verb, &ver, game_tok, role_tok, &pnum);
	if (fields < 4)
		return -1;
	if (strcmp(verb, "HELLO") != 0)
		return -1;

	*version = ver;
	snprintf(game, game_size, "%s", game_tok);
	snprintf(role, role_size, "%s", role_tok);
	if (player_num)
		*player_num = (fields >= 5) ? pnum : 0;
	return 0;
}

// Two game names count as the same game when they agree after normalization:
// lowercase alphanumerics only, with (...) and [...] tag groups removed first.
// "Marvel vs. Capcom 2 (USA)" pairs with "marvel vs capcom 2" — region tags,
// dump flags, punctuation and spacing never block a pair — while a real title
// difference ("... 2" vs "... 3") still does. The \x1f space escaping is
// non-alphanumeric, so escaped and raw names normalize identically. This is
// the ONLY game gate a wizard session has: the link engines store name/CRC
// for their own discovery broadcasts but never re-verify them on a direct
// connect, so a mismatch that slips past here surfaces only in-game.
static void wiz_normalize_name(const char* in, char* out, size_t out_size) {
	size_t o = 0;
	int in_tag = 0;

	for (const char* p = in; *p && o < out_size - 1; p++) {
		char c = *p;
		if (c == '(' || c == '[') {
			in_tag++;
		} else if (c == ')' || c == ']') {
			if (in_tag)
				in_tag--;
		} else if (!in_tag && isalnum((unsigned char)c)) {
			out[o++] = (char)tolower((unsigned char)c);
		}
	}
	out[o] = '\0';
}

static bool wiz_names_equivalent(const char* a, const char* b) {
	char na[NET_MAX_GAME_NAME];
	char nb[NET_MAX_GAME_NAME];

	if (!a || !b)
		return false;
	wiz_normalize_name(a, na, sizeof(na));
	wiz_normalize_name(b, nb, sizeof(nb));
	if (!na[0] || !nb[0])
		return false; // a name that is all tags matches nothing
	return strcmp(na, nb) == 0;
}

// NULL = the peer is a match. Otherwise the REJECT reason, which is also the
// key wiz_reject_message() turns into a sentence. Order matters: version is
// checked first because a different version may not even mean the same thing by
// "game" or "role".
static const char* wiz_hello_mismatch(int version, const char* game, const char* role,
									  const char* own_game, const char* expect_role) {
	if (version != WIZ_PROTO_VERSION)
		return "version";
	if (!wiz_names_equivalent(game, own_game))
		return "game";
	if (!role || strcmp(role, expect_role) != 0)
		return "role";
	return NULL;
}

// 0 = the line was a REJECT and reason holds its argument.
static int wiz_parse_reject(const char* line, char* reason, size_t reason_size) {
	char verb[16];
	char reason_tok[32];

	int fields = sscanf(line, "%15s %31s", verb, reason_tok);
	if (fields < 1 || strcmp(verb, "REJECT") != 0)
		return -1;

	// A bare "REJECT" is legal on the wire; it just says nothing useful.
	snprintf(reason, reason_size, "%s", fields == 2 ? reason_tok : "");
	return 0;
}

// The user-facing half of a REJECT. Every reason is something the two devices
// disagree on, so each line names the disagreement rather than the error.
static const char* wiz_reject_message(const char* reason) {
	if (!reason)
		return "The host refused the connection.";
	if (strcmp(reason, "version") == 0)
		return "The host is running a\ndifferent netplay version.";
	if (strcmp(reason, "game") == 0)
		return "The host is running a\ndifferent game.";
	if (strcmp(reason, "role") == 0)
		return "Both devices are hosting.\n\nOne of them must join instead.";
	if (strcmp(reason, "sync") == 0)
		return "The host could not share\nits saves.";
	return "The host refused the connection.";
}

// 0 = the line was SYNC-READY and n holds its count (unvalidated; the caller
// bounds it against its own storage).
static int wiz_parse_sync_ready(const char* line, int* n) {
	char verb[16];
	int count = 0;

	if (sscanf(line, "%15s %d", verb, &count) != 2)
		return -1;
	if (strcmp(verb, "SYNC-READY") != 0)
		return -1;

	*n = count;
	return 0;
}

// 0 = the line was FILE and name holds its still-escaped argument. name must be
// at least WIZ_NET_TOKEN_MAX: a token wider than a filename may fit is read in
// full so wiz_name_is_safe() can refuse it, rather than silently truncated into
// a different, valid-looking name.
static int wiz_parse_file(const char* line, char* name, size_t name_size) {
	char verb[16];
	char name_tok[WIZ_NET_TOKEN_MAX];

	if (sscanf(line, "%15s %255s", verb, name_tok) != 2)
		return -1;
	if (strcmp(verb, "FILE") != 0)
		return -1;

	snprintf(name, name_size, "%s", name_tok);
	return 0;
}

// A name the protocol may carry, in either direction. This is a WHITELIST, not
// a list of things to reject: only [A-Za-z0-9._-] survives. Everything else —
// every shell metacharacter, space, slash, quote, control byte, high byte — is
// out by construction rather than by enumeration, so the rule cannot be
// outgrown by a character nobody thought of.
//
// It has to be this strict because these names are remote input on their way to
// wiz_sync_pull() (Task 5), and this repo's idiom for driving an external tool
// is system() — wizard_wifi.c does it eight times, and so does the hotspot ping
// below. A name like `id`.bin or a;id.bin passes --fetch-files happily
// (fnmatch("*.bin", "a;id.bin", 0) == 0 — the pattern list is NOT a defence),
// and would be LAN-triggerable command execution the moment it reached a shell.
//
// THIS IS THE WIRE-LEVEL GUARANTEE, NOT THE ONLY ONE. Task 5 must still treat
// these names as data: posix_spawn/execv with no shell, or quoted. A whitelist
// is the second line of defence, never a licence to interpolate.
//
// Rules the character class already subsumes, spelled out because they are the
// reasons: '/' would leave the directory the peer does not choose; a space
// would split one argv element into two; control characters (\n, \r, and
// WIZ_NET_SPACE_ESC itself) would break the line framing or return from the
// escape round trip as something else. Left explicit below: "."/".." (all
// whitelisted characters, but still traversal), a leading '-' (whitelisted, but
// rsync would read it as an option), and the length bound that
// wiz_sync_pull()'s names[][64] imposes.
//
// Deliberately tighter than fnmatch allows. Dreamcast save names
// (vmu_save_A1.bin, dc_nvmem.bin) fit comfortably; a name with a space is
// refused rather than transferred, and symmetrically so — the host will not
// offer it and the client will not accept it.
//
// Applied to the *decoded* name on both sides, so there is exactly one
// canonical form that ever gets checked.
static bool wiz_name_is_safe(const char* name) {
	if (!name || !name[0])
		return false;
	if (strlen(name) >= WIZ_NET_NAME_MAX)
		return false;
	if (name[0] == '-')
		return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;

	// Explicit ranges, not isalnum(): that one is locale-dependent, and this
	// rule must mean the same thing wherever it runs.
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

// Pattern check against --fetch-files. Note what this is NOT: with FNM_PATHNAME
// unset a '*' spans '/' and every metacharacter, so it constrains shape only —
// wiz_name_is_safe() is what constrains content, and it runs first at both call
// sites. No patterns means nothing is whitelisted; callers treat that as "this
// side wants no files" rather than as "all files".
static bool wiz_name_matches(const char* name, const char patterns[][64], int count) {
	for (int i = 0; i < count; i++)
		if (fnmatch(patterns[i], name, 0) == 0)
			return true;
	return false;
}

// Same-game test for the host list's ordering: the same normalized-name
// equivalence the HELLO gate uses (escape-tolerant — normalization strips
// \x1f). Only ordering rides on this: the broadcast field truncates titles
// at 64 bytes while own_game is full-length, so an ultra-long title may sort
// as "other game"; the HELLO gate compares full names and decides for real.
static bool wiz_game_matches_broadcast(const char* broadcast_game, const char* own_game) {
	return wiz_names_equivalent(broadcast_game, own_game);
}

//////////////////////////////////
// Control channel
//////////////////////////////////

// Lines arrive without regard for packet boundaries — the host emits
// SYNC-READY and every FILE back to back, and TCP is free to deliver them in
// one read. The reader keeps whatever came early instead of over-reading past a
// line into the next one.
typedef struct {
	int fd;
	char buf[WIZ_NET_LINE_MAX];
	size_t len;
} WizLineReader;

static void wiz_reader_init(WizLineReader* r, int fd) {
	r->fd = fd;
	r->len = 0;
}

static bool wiz_fd_readable(int fd, int timeout_ms) {
	fd_set fds;
	struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};

	if (fd < 0)
		return false;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	return select(fd + 1, &fds, NULL, NULL, &tv) > 0;
}

// Non-blocking. 1 = a line was copied to out (terminator stripped), 0 = nothing
// complete yet, -1 = the peer closed, errored, or sent a line too long to be
// one of ours.
static int wiz_reader_poll(WizLineReader* r, char* out, size_t out_size) {
	while (1) {
		char* nl = memchr(r->buf, '\n', r->len);
		if (nl) {
			size_t line_len = (size_t)(nl - r->buf);
			size_t rest = r->len - line_len - 1;
			size_t copy = line_len;

			// Tolerate CRLF: a scripted peer (netcat, a test harness) is as
			// likely to send it as not.
			if (copy > 0 && r->buf[copy - 1] == '\r')
				copy--;
			if (copy >= out_size)
				copy = out_size - 1;

			memcpy(out, r->buf, copy);
			out[copy] = '\0';

			memmove(r->buf, nl + 1, rest);
			r->len = rest;
			return 1;
		}

		if (r->len == sizeof(r->buf))
			return -1; // no terminator in a full buffer: not this protocol

		if (!wiz_fd_readable(r->fd, 0))
			return 0;

		ssize_t got = recv(r->fd, r->buf + r->len, sizeof(r->buf) - r->len, 0);
		if (got == 0)
			return -1; // orderly close
		if (got < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			return -1;
		}

		r->len += (size_t)got;
	}
}

// MSG_NOSIGNAL is load-bearing: setup_signal_handlers() leaves SIGPIPE at its
// default, so writing to a peer that vanished would kill the wizard outright
// and launch.sh would see a signal, not one of its exit codes.
static int wiz_write_all(int fd, const char* buf, size_t len) {
	size_t sent = 0;

	while (sent < len) {
		ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		sent += (size_t)n;
	}

	return 0;
}

static int wiz_send_line(int fd, const char* fmt, ...) {
	char line[WIZ_NET_LINE_MAX];
	va_list ap;

	va_start(ap, fmt);
	int len = vsnprintf(line, sizeof(line) - 1, fmt, ap);
	va_end(ap);

	if (len < 0)
		return -1;
	if (len > (int)sizeof(line) - 2)
		len = (int)sizeof(line) - 2; // truncated; the peer will reject it

	line[len] = '\n';
	line[len + 1] = '\0';
	return wiz_write_all(fd, line, (size_t)len + 1);
}

// The one wait primitive for the control channel. Runs the frame loop so the
// power button, the battery indicator and (when cancelable) B stay live while a
// line is outstanding — a blocking recv would freeze all three for its timeout.
// 0 = line in out, -1 = timeout/close/protocol error, -2 = cancelled.
static int wiz_wait_line(WizLineReader* r, char* out, size_t out_size,
						 uint32_t timeout_ms, bool cancelable, const char* status) {
	uint32_t start = SDL_GetTicks();
	bool dirty = true;

	while (1) {
		GFX_startFrame();
		PAD_poll();

		if (cancelable && (PAD_justPressed(BTN_B) || app_quit))
			return -2;

		int rc = wiz_reader_poll(r, out, out_size);
		if (rc == 1)
			return 0;
		if (rc < 0)
			return -1;

		if (SDL_GetTicks() - start >= timeout_ms)
			return -1;

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			wiz_net_render_wait_status(status, cancelable);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

// Frame-paced sleep. Same reason as wiz_wait_line: an SDL_Delay here would sit
// on the retry gap with the buttons dead. 0 = waited, -2 = cancelled.
static int wiz_wait_ms(uint32_t ms, bool cancelable, const char* status) {
	uint32_t start = SDL_GetTicks();
	bool dirty = true;

	while (SDL_GetTicks() - start < ms) {
		GFX_startFrame();
		PAD_poll();

		if (cancelable && (PAD_justPressed(BTN_B) || app_quit))
			return -2;

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			wiz_net_render_wait_status(status, cancelable);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	return 0;
}

// Non-blocking connect driven from the frame loop. A blocking connect() cannot
// be bounded portably (SO_SNDTIMEO is not specified to apply to it) and would
// hold the UI for the whole SYN retransmit window, which on a hotspot that has
// just come up is most of the interesting case.
// 0 = connected (*out_fd owned by the caller), -1 = failed, -2 = cancelled.
static int wiz_connect(const char* ip, uint16_t port, uint32_t timeout_ms,
					   const char* status, int* out_fd) {
	struct sockaddr_in addr = {0};
	uint32_t start;
	bool dirty = true;
	int flags;

	*out_fd = -1;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
		close(fd);
		return -1;
	}

	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rc < 0 && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}

	start = SDL_GetTicks();
	while (rc != 0) {
		fd_set wfds;
		struct timeval tv = {0, 0};

		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B) || app_quit) {
			close(fd);
			return -2;
		}

		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);

		int sel = select(fd + 1, NULL, &wfds, NULL, &tv);
		if (sel > 0) {
			// Writable covers both outcomes; SO_ERROR says which.
			int err = 0;
			socklen_t err_len = sizeof(err);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0 || err != 0) {
				close(fd);
				return -1;
			}
			break;
		}
		if (sel < 0 && errno != EINTR) {
			close(fd);
			return -1;
		}
		if (SDL_GetTicks() - start >= timeout_ms) {
			close(fd);
			return -1;
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			wiz_net_render_wait_status(status, true);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	// Back to blocking for the handshake: reads go through select() first, and
	// WIZ_NET_TCP_CONFIG's SO_RCVTIMEO is what covers the gap between the two.
	fcntl(fd, F_SETFL, flags);
	NET_configureTCPSocket(fd, &WIZ_NET_TCP_CONFIG);

	*out_fd = fd;
	return 0;
}

//////////////////////////////////
// Host
//////////////////////////////////

// Entries of serve_dir that --fetch-files selects, as bare names. Returns the
// count (0 when there is nothing to offer, which skips the sync phase entirely).
static int wiz_collect_serve_files(const WizArgs* a, char names[][WIZ_NET_NAME_MAX]) {
	DIR* dir = opendir(a->serve_dir);
	int n = 0;

	if (!dir)
		return 0;

	struct dirent* entry;
	while (n < WIZ_NET_SYNC_MAX_FILES && (entry = readdir(dir)) != NULL) {
		char path[512];
		struct stat st;

		if (!wiz_name_is_safe(entry->d_name))
			continue;
		if (!wiz_name_matches(entry->d_name, a->patterns, a->pattern_count))
			continue;

		// Regular files only: the protocol offers one name per file and the
		// client pulls them individually, which a directory would not survive.
		snprintf(path, sizeof(path), "%s/%s", a->serve_dir, entry->d_name);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;

		snprintf(names[n], WIZ_NET_NAME_MAX, "%s", entry->d_name);
		n++;
	}

	closedir(dir);
	return n;
}

// Offer n files and wait for the client to finish pulling them.
// 0 = pulled, 1 = give this client up and go back to waiting, -1 = fatal.
static int wiz_host_serve(const WizArgs* a, WizSession* s, int fd, WizLineReader* r,
						  char names[][WIZ_NET_NAME_MAX], int n) {
	char line[WIZ_NET_LINE_MAX];
	char escaped[WIZ_NET_TOKEN_MAX];
	bool ok;

	if (wiz_sync_serve_start(a->serve_dir, s->peer_ip) != 0) {
		// Tell the client why before dropping it; it has nothing else to go on.
		wiz_send_line(fd, "REJECT sync");
		wiz_net_error("Could not share saves.\n\nPlease try again.");
		return -1;
	}

	ok = (wiz_send_line(fd, "SYNC-READY %d", n) == 0);
	for (int i = 0; ok && i < n; i++) {
		wiz_esc_spaces(names[i], escaped, sizeof(escaped));
		ok = (wiz_send_line(fd, "FILE %s", escaped) == 0);
	}

	if (!ok) {
		wiz_sync_serve_stop();
		return 1;
	}

	// B is off for this window on purpose: the client is mid-pull and tearing
	// the server down under it would leave half a save directory. A client that
	// dropped shows up as the wait timing out.
	int rc = wiz_wait_line(r, line, sizeof(line), WIZ_NET_PULL_TIMEOUT_MS, false, "Sending saves...");
	wiz_sync_serve_stop();

	if (rc != 0 || strcmp(line, "SYNC-DONE") != 0) {
		wiz_net_notice("Save transfer failed.\n\nWaiting for another player...");
		return 1;
	}

	return 0;
}

// The host half of one accepted connection, up to and INCLUDING the HELLO reply
// and (for --serve-dir, unused by N64) the save-sync phase — but NOT the START.
// The START is sent to every retained joiner at once from the start phase in
// wiz_host_rendezvous, so a handshaked joiner's reader and IP are handed back to
// the caller through reader_out / ip_out (ip_out must hold at least 16 bytes).
// fd stays owned by the caller.
// 0 = handshaked, 1 = not our peer / dropped (keep waiting),
// -1 = fatal (message drawn), -2 = cancelled.
static int wiz_host_handshake(const WizArgs* a, WizSession* s, int fd,
							  const struct sockaddr_in* peer, int player_num,
							  WizLineReader* reader_out, char* ip_out) {
	char line[WIZ_NET_LINE_MAX];
	char status[96];
	char peer_ip[16] = {0};
	char game[WIZ_NET_TOKEN_MAX];
	char escaped[WIZ_NET_TOKEN_MAX];
	char role[16];
	int version = 0;

	// Linux does not pass O_NONBLOCK from the listening fd to the accepted one,
	// but POSIX leaves that unspecified and the listener is now non-blocking, so
	// say it outright: wiz_write_all() has no EAGAIN path, and reads here want
	// the blocking + SO_RCVTIMEO pairing that WIZ_NET_TCP_CONFIG sets up.
	int peer_flags = fcntl(fd, F_GETFL, 0);
	if (peer_flags >= 0)
		fcntl(fd, F_SETFL, peer_flags & ~O_NONBLOCK);

	NET_configureTCPSocket(fd, &WIZ_NET_TCP_CONFIG);
	wiz_reader_init(reader_out, fd);

	if (!inet_ntop(AF_INET, &peer->sin_addr, peer_ip, sizeof(peer_ip)))
		return 1;

	snprintf(status, sizeof(status), "Player connected\n%s", peer_ip);

	int rc = wiz_wait_line(reader_out, line, sizeof(line), WIZ_NET_RECV_TIMEOUT_MS, true, status);
	if (rc == -2)
		return -2;
	if (rc != 0)
		return 1; // no HELLO in time: not worth a reply

	if (wiz_parse_hello(line, &version, game, sizeof(game), role, sizeof(role), NULL) != 0)
		return 1; // not a wizard peer at all; closing says it better than REJECT

	wiz_unesc_spaces(game);

	const char* reason = wiz_hello_mismatch(version, game, role, a->game, "client");
	if (reason) {
		// Rejecting is not an error here — the host keeps waiting for the peer
		// it is actually paired with.
		wiz_send_line(fd, "REJECT %s", reason);
		return 1;
	}

	wiz_esc_spaces(a->game, escaped, sizeof(escaped));
	if (wiz_send_line(fd, "HELLO %d %s host %d", WIZ_PROTO_VERSION, escaped, player_num) != 0)
		return 1;

	snprintf(ip_out, 16, "%s", peer_ip);
	// wiz_host_serve() serves from this joiner's directory keyed on s->peer_ip;
	// set it per-joiner. On the m64p star topology (N64) the host's own
	// s->peer_ip is unused, so the last joiner's value left here is harmless.
	snprintf(s->peer_ip, sizeof(s->peer_ip), "%s", peer_ip);

	if (a->serve_dir) {
		char names[WIZ_NET_SYNC_MAX_FILES][WIZ_NET_NAME_MAX];
		int n = wiz_collect_serve_files(a, names);

		// Nothing to offer means no sync phase at all: the client reads the next
		// line either way, and starting a file server for zero files is waste.
		if (n > 0) {
			int src = wiz_host_serve(a, s, fd, reader_out, names, n);
			if (src != 0) {
				s->peer_ip[0] = '\0';
				return src;
			}
		}
	}

	return 0;
}

int wiz_host_rendezvous(const WizArgs* a, WizSession* s) {
	NET_BroadcastTimer broadcast_timer;
	char error_text[128] = {0};
	char big[64];
	bool hotspot = (strcmp(s->mode, "hotspot") == 0);
	bool dirty = true;
	int result = -1;

	// The joiner table: each handshaked joiner's fd, reader and IP are retained
	// so the host can send START to all of them at once in the start phase.
	// Host is player 1; joiners take 2..N by join order.
	struct {
		int fd;
		WizLineReader reader;
		char ip[16];
	} joiners[4];
	int njoin = 0;
	int max_join = a->max_players - 1;
	int start_now = 0;
	bool break_all = false;

	int listen_fd = NET_createListenSocket(WIZ_TCP_PORT, error_text, sizeof(error_text));
	if (listen_fd < 0) {
		char message[192];
		snprintf(message, sizeof(message), "Could not host.\n\n%s", error_text);
		wiz_net_error(message);
		return -1;
	}

	// NET_createListenSocket leaves the fd blocking, and select() reporting it
	// readable is NOT a promise that accept() will not block: if the peer resets
	// the connection in the gap between the two, Linux drops it from the queue
	// and a blocking accept() waits for the *next* one, which may never come.
	// That hangs this thread — no PAD_poll, no timeout — while wizard.c holds
	// PWR_disableSleep/Autosleep/PowerOff, i.e. a device the user can neither
	// cancel nor switch off. Non-blocking turns that case into an EAGAIN the
	// accept loop below already ignores.
	int listen_flags = fcntl(listen_fd, F_GETFL, 0);
	if (listen_flags >= 0)
		fcntl(listen_fd, F_SETFL, listen_flags | O_NONBLOCK);

	// Discovery is how the WiFi client finds us; the hotspot client already
	// knows the address. A socket we could not create is therefore not fatal in
	// hotspot mode and only costs the list entry in WiFi mode.
	int udp_fd = NET_createBroadcastSocket();
	NET_initBroadcastTimer(&broadcast_timer, WIZ_NET_BROADCAST_INTERVAL_US);

	if (hotspot) {
		snprintf(big, sizeof(big), "%s", wiz_hotspot_code[0] ? wiz_hotspot_code : "????");
	} else {
		char ip[16] = {0};
		if (WIFI_direct_getIP(ip, sizeof(ip)) != 0 || !ip[0])
			NET_getLocalIP(ip, sizeof(ip));
		snprintf(big, sizeof(big), "%s", ip[0] ? ip : "0.0.0.0");
	}

	const char* instruction = hotspot ? "Select this code on the other device"
									  : "Other device must be on the same WiFi";

	// No wall-clock ceiling: this screen exists to wait for a person, and B is
	// live every frame. wizard.c holds sleep/power-off off for the whole run,
	// so an abandoned host waits until the battery decides otherwise.
	while (1) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B) || app_quit) {
			result = -2;
			break;
		}

		if (udp_fd >= 0 && NET_shouldBroadcast(&broadcast_timer))
			NET_sendDiscoveryBroadcast(udp_fd, WIZ_MAGIC, WIZ_PROTO_VERSION, 0 /* crc unused */,
									   WIZ_TCP_PORT, WIZ_UDP_PORT, a->game, WIZ_NET_LINK_MODE);

		// Accept only while there is still a seat: past max_join, extra callers
		// are refused in the accept branch (their fd closed immediately).
		if (njoin < max_join && wiz_fd_readable(listen_fd, 0)) {
			struct sockaddr_in peer = {0};
			socklen_t peer_len = sizeof(peer);

			// A failure here is EAGAIN/EWOULDBLOCK (the reset-in-the-gap case the
			// O_NONBLOCK above exists for) or a transient errno; either way it
			// just means no client this frame.
			int peer_fd = accept(listen_fd, (struct sockaddr*)&peer, &peer_len);
			if (peer_fd >= 0) {
				int assigned = njoin + 2; // players 2..N by join order
				int rc = wiz_host_handshake(a, s, peer_fd, &peer, assigned,
											&joiners[njoin].reader, joiners[njoin].ip);
				if (rc == -2) {
					result = -2;
					break_all = true;
				} else if (rc == -1) {
					// Fatal (e.g. --serve-dir could not start; message already
					// drawn). Preserves today's DC behaviour: abort the wizard.
					close(peer_fd);
					result = -1;
					break_all = true;
				} else if (rc == 0) {
					joiners[njoin].fd = peer_fd;
					njoin++;
					dirty = true;
				} else {
					close(peer_fd); // rejected or dropped: keep waiting
					dirty = true;
				}
			}
		}

		if (break_all)
			break;

		// Start trigger. With max_players == 2 the first joiner auto-starts, no
		// A press — byte-identical to the pre-4-player flow. With room for more,
		// the host presses A once at least one joiner is in.
		if (a->max_players == 2 && njoin == 1)
			start_now = 1;
		if (njoin >= 1 && PAD_justPressed(BTN_A))
			start_now = 1;
		if (start_now)
			break;

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			char status[64];
			if (a->max_players > 2)
				// +1 for the host, who is player 1: with one joiner the lobby
				// has 2 players, not 1.
				snprintf(status, sizeof(status), "Players connected: %d / up to %d",
						 njoin + 1, a->max_players);
			else
				snprintf(status, sizeof(status), "%s",
						 njoin ? "Player connected" : "Waiting for player...");

			// The A=START affordance is a multi-join concept: only the >2 lobby
			// shows it. The 2-player screen keeps the plain B-only hint.
			if (a->max_players > 2 && njoin >= 1)
				wiz_net_render_lobby(big, instruction, status);
			else
				wiz_net_render_waiting(big, instruction, status);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	// Start phase: hand every joiner the final player count and collect its
	// plain START echo (an N-way barrier). Any send or echo failure aborts the
	// whole lobby — a partial start is out of scope.
	if (start_now && njoin >= 1) {
		s->num_players = njoin + 1;
		int ok = 1;
		for (int i = 0; i < njoin && ok; i++)
			if (wiz_send_line(joiners[i].fd, "START %d", s->num_players) != 0)
				ok = 0;
		for (int i = 0; i < njoin && ok; i++) {
			char line[WIZ_NET_LINE_MAX];
			int rc = wiz_wait_line(&joiners[i].reader, line, sizeof(line),
								   WIZ_NET_RECV_TIMEOUT_MS, true, "Starting...");
			if (rc == -2) {
				result = -2;
				ok = 0;
			} else if (rc != 0 || strcmp(line, "START") != 0) {
				ok = 0;
			}
		}
		result = ok ? 0 : (result == -2 ? -2 : 1);
	}

	for (int i = 0; i < njoin; i++)
		close(joiners[i].fd);

	if (udp_fd >= 0)
		close(udp_fd);
	close(listen_fd);
	return result;
}

//////////////////////////////////
// Client
//////////////////////////////////

// Live list of hosts broadcasting our game. 0 = ip_out filled, -1 = error
// (message drawn), -2 = cancelled.
static int wiz_client_pick_host(const WizArgs* a, char* ip_out, size_t ip_size) {
	NET_HostInfo hosts[WIZ_NET_MAX_HOSTS];
	char label_text[WIZ_NET_MAX_HOSTS][WIZ_NET_LABEL_MAX];
	const char* labels[WIZ_NET_MAX_HOSTS];
	int matches[WIZ_NET_MAX_HOSTS];
	int host_count = 0; // every host seen, our game or not
	int match_count = 0;
	int selected = 0;
	int scroll = 0;
	bool dirty = true;
	uint32_t last_poll = 0;
	uint32_t start_time = SDL_GetTicks();

	int udp_fd = NET_createDiscoveryListenSocket(WIZ_UDP_PORT);
	if (udp_fd < 0) {
		wiz_net_error("Could not listen for hosts.\n\nPlease try again.");
		return -1;
	}

	while (1) {
		uint32_t now = SDL_GetTicks();

		// Only while there is nothing to pick, as in wizard_wifi.c's hotspot
		// picker: once a host is listed the choice is the user's to take.
		if (match_count == 0 && now - start_time > WIZ_NET_LIST_TIMEOUT_MS) {
			close(udp_fd);
			wiz_net_error("No host found.\n\nMake sure the host has\nstarted hosting first.");
			return -1;
		}

		if (now - last_poll >= WIZ_NET_DISCOVERY_POLL_MS) {
			int before = match_count;

			last_poll = now;
			NET_receiveDiscoveryResponses(udp_fd, WIZ_MAGIC, hosts, &host_count, WIZ_NET_MAX_HOSTS);

			// Rebuilt from scratch rather than appended to: the dedup lives in
			// NET_receiveDiscoveryResponses, and hosts only ever grows.
			//
			// EVERY wizard host is listed, own game or not — differently-named
			// copies of the same game (region tags, cleaned-up filenames) must
			// not make a host silently invisible. The label carries the host's
			// game title, so the join is the user's informed choice; picking a
			// genuinely different game gets the host's named REJECT at the
			// HELLO handshake and returns to this list. Two passes keep
			// same-named hosts at the top.
			match_count = 0;
			for (int pass = 0; pass < 2; pass++) {
				for (int i = 0; i < host_count; i++) {
					char title[NET_MAX_GAME_NAME];
					bool same_game = wiz_game_matches_broadcast(hosts[i].game_name, a->game);

					if (strcmp(hosts[i].link_mode, WIZ_NET_LINK_MODE) != 0)
						continue;
					if (same_game != (pass == 0))
						continue;

					// The field is sent raw, but a peer that escaped anyway must
					// not put \x1f on screen.
					snprintf(title, sizeof(title), "%s", hosts[i].game_name);
					wiz_unesc_spaces(title);

					snprintf(label_text[match_count], WIZ_NET_LABEL_MAX, "%s (%s)",
							 title, hosts[i].host_ip);
					labels[match_count] = label_text[match_count];
					matches[match_count] = i;
					match_count++;
				}
			}

			if (match_count != before) {
				dirty = true;
				if (selected >= match_count)
					selected = match_count > 0 ? match_count - 1 : 0;
			}
		}

		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B) || app_quit) {
			close(udp_fd);
			return -2;
		}

		if (match_count > 0) {
			if (PAD_justRepeated(BTN_UP)) {
				selected = (selected + match_count - 1) % match_count;
				dirty = true;
			} else if (PAD_justRepeated(BTN_DOWN)) {
				selected = (selected + 1) % match_count;
				dirty = true;
			} else if (PAD_justPressed(BTN_A)) {
				snprintf(ip_out, ip_size, "%s", hosts[matches[selected]].host_ip);
				close(udp_fd);
				return 0;
			}
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			if (match_count > 0)
				wiz_net_render_list(labels, match_count, selected, &scroll);
			else
				wiz_net_render_empty("Searching for hosts...");
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

// Read the n FILE lines and, if we want them, pull them.
// 0 = the host may proceed, 1 = drop this host, -1 = fatal, -2 = cancelled.
static int wiz_client_sync(const WizArgs* a, WizSession* s, int fd, WizLineReader* r, int n) {
	char names[WIZ_NET_SYNC_MAX_FILES][WIZ_NET_NAME_MAX];
	char line[WIZ_NET_LINE_MAX];
	char token[WIZ_NET_TOKEN_MAX];

	// --fetch-files is the whitelist; without it, or without --fetch-to, we take
	// nothing. The FILE lines are still read to the end so both sides stay in
	// lockstep and the host reaches its START.
	bool fetching = (a->fetch_to != NULL && a->pattern_count > 0);

	if (n < 0 || n > WIZ_NET_SYNC_MAX_FILES) {
		wiz_send_line(fd, "SYNC-FAIL");
		wiz_net_error("The host offered more saves\nthan expected.");
		return -1;
	}

	for (int i = 0; i < n; i++) {
		int rc = wiz_wait_line(r, line, sizeof(line), WIZ_NET_RECV_TIMEOUT_MS, true, "Receiving saves...");
		if (rc == -2)
			return -2;
		if (rc != 0) {
			wiz_send_line(fd, "SYNC-FAIL");
			wiz_net_error("The host stopped responding.");
			return 1;
		}

		if (wiz_parse_file(line, token, sizeof(token)) != 0) {
			wiz_send_line(fd, "SYNC-FAIL");
			wiz_net_error("Unexpected reply from the host.");
			return -1;
		}

		wiz_unesc_spaces(token);

		// A name we would not have served ourselves is a version skew or worse;
		// either way it is not something to write into --fetch-to.
		if (!wiz_name_is_safe(token) ||
			(fetching && !wiz_name_matches(token, a->patterns, a->pattern_count))) {
			wiz_send_line(fd, "SYNC-FAIL");
			wiz_net_error("The host offered an\nunexpected file.");
			return -1;
		}

		snprintf(names[i], WIZ_NET_NAME_MAX, "%s", token);
	}

	if (!fetching) {
		// Offered and declined. SYNC-DONE, not SYNC-FAIL: nothing went wrong.
		wiz_send_line(fd, "SYNC-DONE");
		return 0;
	}

	mkdir_p(a->fetch_to);
	wiz_net_status("Receiving saves...");

	if (wiz_sync_pull(s->peer_ip, a->fetch_to, names, n) != 0) {
		wiz_send_line(fd, "SYNC-FAIL");
		wiz_net_error("Could not copy the host's saves.");
		return -1;
	}

	wiz_send_line(fd, "SYNC-DONE");
	return 0;
}

// One connected control channel, start to finish. fd stays owned by the caller.
// 0 = rendezvous complete, 1 = drop this host (message drawn; the WiFi client
// goes back to its list), -1 = fatal (message drawn), -2 = cancelled.
static int wiz_client_session(const WizArgs* a, WizSession* s, int fd, const char* host_ip) {
	WizLineReader reader;
	char line[WIZ_NET_LINE_MAX];
	char escaped[WIZ_NET_TOKEN_MAX];
	char game[WIZ_NET_TOKEN_MAX];
	char reason[32];
	char role[16];
	int version = 0;
	int n = 0;

	wiz_reader_init(&reader, fd);
	wiz_esc_spaces(a->game, escaped, sizeof(escaped));

	if (wiz_send_line(fd, "HELLO %d %s client", WIZ_PROTO_VERSION, escaped) != 0) {
		wiz_net_error("Connection failed.\n\nPlease try again.");
		return 1;
	}

	int rc = wiz_wait_line(&reader, line, sizeof(line), WIZ_NET_RECV_TIMEOUT_MS, true,
						   "Waiting for the host...");
	if (rc == -2)
		return -2;
	if (rc != 0) {
		wiz_net_error("The host did not answer.\n\nPlease try again.");
		return 1;
	}

	if (wiz_parse_reject(line, reason, sizeof(reason)) == 0) {
		wiz_net_error(wiz_reject_message(reason));
		return 1;
	}

	int assigned = 0;
	if (wiz_parse_hello(line, &version, game, sizeof(game), role, sizeof(role), &assigned) != 0) {
		wiz_net_error("That device is not waiting\nfor a netplay player.");
		return 1;
	}

	wiz_unesc_spaces(game);

	const char* mismatch = wiz_hello_mismatch(version, game, role, a->game, "host");
	if (mismatch) {
		wiz_net_error(wiz_reject_message(mismatch));
		return 1;
	}

	s->player_num = (assigned >= 1) ? assigned : 2; // older host omits it -> 2-player

	snprintf(s->peer_ip, sizeof(s->peer_ip), "%s", host_ip);

	// Now that our number is known, tell the player which seat they hold while
	// they wait. The host may sit in a full lobby for a while before pressing
	// START, so the window here is far wider than a single line's — still
	// B-cancelable every frame.
	char waitmsg[48];
	snprintf(waitmsg, sizeof(waitmsg), "You are Player %d\nWaiting for host to start...", s->player_num);

	// Next line is SYNC-READY when the host serves saves, START when it does not.
	rc = wiz_wait_line(&reader, line, sizeof(line), WIZ_NET_RECV_TIMEOUT_MS * 12, true,
					   waitmsg);
	if (rc == -2) {
		s->peer_ip[0] = '\0';
		return -2;
	}
	if (rc != 0) {
		s->peer_ip[0] = '\0';
		wiz_net_error("The host did not answer.\n\nPlease try again.");
		return 1;
	}

	if (wiz_parse_reject(line, reason, sizeof(reason)) == 0) {
		s->peer_ip[0] = '\0';
		wiz_net_error(wiz_reject_message(reason));
		return 1;
	}

	if (wiz_parse_sync_ready(line, &n) == 0) {
		int src = wiz_client_sync(a, s, fd, &reader, n);
		if (src != 0) {
			s->peer_ip[0] = '\0';
			return src;
		}

		rc = wiz_wait_line(&reader, line, sizeof(line), WIZ_NET_RECV_TIMEOUT_MS, true, "Starting...");
		if (rc == -2) {
			s->peer_ip[0] = '\0';
			return -2;
		}
		if (rc != 0) {
			s->peer_ip[0] = '\0';
			wiz_net_error("The host did not answer.\n\nPlease try again.");
			return 1;
		}
	}

	{
		int n2 = 0;
		if (sscanf(line, "START %d", &n2) >= 1)
			s->num_players = (n2 >= 2) ? n2 : 2;
		else if (strcmp(line, "START") == 0)
			s->num_players = 2;
		else {
			s->peer_ip[0] = '\0';
			wiz_net_error("Unexpected reply from the host.");
			return 1;
		}
	}

	if (wiz_send_line(fd, "START") != 0) {
		s->peer_ip[0] = '\0';
		wiz_net_error("Connection lost.\n\nPlease try again.");
		return 1;
	}

	return 0;
}

// Hotspot arm: one known address, no list to go back to.
static int wiz_client_hotspot(const WizArgs* a, WizSession* s) {
	// The association is seconds old — ARP and the default route may not be in
	// place yet, and the first SYN into that gap reads to the user as a refused
	// connection. One ping settles it. It blocks for up to 2 s, so put the
	// screen up first rather than leaving wizard_wifi.c's last frame on show.
	wiz_net_status("Connecting to host...");
	system("ping -c 1 -W 2 " WIFI_DIRECT_HOTSPOT_IP " >/dev/null 2>&1");

	for (int attempt = 1; attempt <= WIZ_NET_CONNECT_ATTEMPTS; attempt++) {
		char status[64];
		int fd = -1;

		if (attempt == 1)
			snprintf(status, sizeof(status), "Connecting to host...");
		else
			snprintf(status, sizeof(status), "Retrying connection... (%d/%d)",
					 attempt, WIZ_NET_CONNECT_ATTEMPTS);

		int rc = wiz_connect(WIFI_DIRECT_HOTSPOT_IP, WIZ_TCP_PORT,
							 WIZ_NET_CONNECT_TIMEOUT_MS, status, &fd);
		if (rc == -2)
			return -2;

		if (rc == 0) {
			int src = wiz_client_session(a, s, fd, WIFI_DIRECT_HOTSPOT_IP);
			close(fd);

			if (src == 0)
				return 0;
			if (src == -2)
				return -2;
			// Nothing to fall back to here: the message is already up.
			return -1;
		}

		if (attempt < WIZ_NET_CONNECT_ATTEMPTS && wiz_wait_ms(WIZ_NET_RETRY_DELAY_MS, true, status) == -2)
			return -2;
	}

	wiz_net_error("Could not reach the host.\n\nMake sure the host is\nwaiting for a player.");
	return -1;
}

int wiz_client_rendezvous(const WizArgs* a, WizSession* s) {
	if (strcmp(s->mode, "hotspot") == 0)
		return wiz_client_hotspot(a, s);

	// WiFi: anything short of a fatal error goes back to the list, because the
	// host that refused us may not be the only one broadcasting.
	while (1) {
		char host_ip[16] = {0};
		char status[64];
		int fd = -1;

		int rc = wiz_client_pick_host(a, host_ip, sizeof(host_ip));
		if (rc != 0)
			return rc;

		snprintf(status, sizeof(status), "Connecting to %s...", host_ip);

		rc = wiz_connect(host_ip, WIZ_TCP_PORT, WIZ_NET_CONNECT_TIMEOUT_MS, status, &fd);
		if (rc == -2)
			continue; // B during the connect: back to the list, not out
		if (rc != 0) {
			wiz_net_error("Connection failed.\n\nThe host may have stopped\nwaiting.");
			continue;
		}

		int src = wiz_client_session(a, s, fd, host_ip);
		close(fd);

		if (src == 0)
			return 0;
		if (src == -1 || src == -2)
			return src;
		// src == 1: refused or dropped, message drawn — pick again.
	}
}
