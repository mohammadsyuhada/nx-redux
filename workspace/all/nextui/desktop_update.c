#include "desktop_update.h"
// defines.h self-includes platform.h, which is what actually defines
// HAS_RUNTIME_PATHS -- it must be pulled in unconditionally, before the gate
// below is evaluated, or the gate always sees HAS_RUNTIME_PATHS undefined
// (nothing else in this translation unit has included platform.h yet) and
// this compiles as the device no-op stub even on desktop builds.
#include "defines.h"
#ifdef HAS_RUNTIME_PATHS
#include <pthread.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "ui_confirmdialog.h"

extern char** environ;

#ifndef BUILD_TAG
#define BUILD_TAG "untagged"
#endif

static atomic_int update_ready = 0; // set by the check thread, release/acquire-paired with the buffer writes below
static char update_tag[64];
static char update_url[512];
static int offered = 0; // main-thread-only, no cross-thread access

// Defense-in-depth for update_tag/update_url: both ultimately derive from a
// network response (check-update.sh's stdout, itself derived from a "Location"
// redirect header). Reject anything outside a strict allowlist before ever
// trusting it -- update_ready is only set when both pass -- so even if the
// no-shell spawn below were ever bypassed or reused elsewhere, a value with
// shell metacharacters (quotes, backticks, $, ;, parens, spaces, ...) never
// reaches update_ready in the first place.
static int is_safe(const char* s) {
	for (; *s; s++) {
		char c = *s;
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
			  c == '.' || c == '_' || c == '~' || c == ':' || c == '/' || c == '?' || c == '=' ||
			  c == '&' || c == '%' || c == '+' || c == '-'))
			return 0;
	}
	return 1;
}

static void* check_thread(void* arg) {
	(void)arg;
	char cmd[MAX_PATH + 64];
	// BIN_PATH quoted: an unquoted path word-splits under a macOS username
	// with a space (e.g. "/Users/John Doe/..."), silently no-op-ing the
	// check. BUILD_TAG is a compile-time constant (never attacker-controlled),
	// so it doesn't need quoting, but the whole command still runs through a
	// shell here -- unlike the self-update spawn below, nothing in this
	// command is network-derived, so popen is fine.
	snprintf(cmd, sizeof(cmd), "\"%s/check-update.sh\" %s", BIN_PATH, BUILD_TAG);
	FILE* p = popen(cmd, "r");
	if (!p)
		return NULL;
	char line[600] = {0};
	// pclose() must run exactly once: it fclose()s p as part of reaping the
	// child, so calling it a second time on an already-closed FILE* (as the
	// brief's `fgets(...) && pclose(p) == 0` / `else pclose(p)` shape does
	// whenever the script exits 1 "current" or 2 "error" -- the common
	// case) is a double-close/use-after-free. Read the got-a-line result
	// first, close once, then branch on both.
	char* got = fgets(line, sizeof(line), p);
	int rc = pclose(p);
	if (got && rc == 0) {
		char* tab = strchr(line, '\t');
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		if (tab) {
			*tab = '\0';
			snprintf(update_tag, sizeof(update_tag), "%s", line);
			snprintf(update_url, sizeof(update_url), "%s", tab + 1);
			if (is_safe(update_tag) && is_safe(update_url))
				// release: publish the buffer writes above before the flag
				// that gates every reader of them (offerIfReady's acquire
				// load below) -- plain `volatile` gives no such ordering
				// guarantee on arm64, the primary desktop target.
				atomic_store_explicit(&update_ready, 1, memory_order_release);
			// else: silently drop. Buffers hold the rejected value but are
			// never read -- every consumer gates on update_ready first --
			// and nothing about the rejected content is logged.
		}
	}
	return NULL;
}

void DesktopUpdate_startCheck(void) {
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&t, &attr, check_thread, NULL);
	pthread_attr_destroy(&attr);
}

void DesktopUpdate_offerIfReady(SDL_Surface* screen) {
	// acquire: pairs with the check thread's release store, guaranteeing
	// update_tag/update_url are fully visible here once this observes 1.
	if (!atomic_load_explicit(&update_ready, memory_order_acquire) || offered)
		return;
	offered = 1;
	char title[96];
	snprintf(title, sizeof(title), "Update available: %s", update_tag);
	if (!UI_confirmModal(screen, title, "Download and restart NXRedux?", NULL, true, false))
		return;
	// No-shell spawn: update_url comes from the network (via check-update.sh)
	// and must never be re-parsed by a shell. posix_spawn hands it to
	// self-update.sh as a single argv element -- there is no command string
	// for shell metacharacters to break out of, so the is_safe() allowlist
	// above is defense-in-depth, not the only thing stopping injection here.
	char path[MAX_PATH + 32];
	snprintf(path, sizeof(path), "%s/self-update.sh", BIN_PATH);
	char* argv[] = {(char*)"self-update.sh", update_url, NULL};
	pid_t pid;
	if (posix_spawn(&pid, path, NULL, NULL, argv, environ) != 0)
		return;
	int status = 0;
	// self-update.sh backgrounds the actual relaunch and returns 0 once it
	// has kicked that off, so waiting for *this* process to exit 0 is the
	// right success signal -- matches the brief's system(cmd)==0 semantics.
	if (waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0)
		exit(0);
}
#else
void DesktopUpdate_startCheck(void) {}
void DesktopUpdate_offerIfReady(SDL_Surface* screen) {
	(void)screen;
}
#endif
