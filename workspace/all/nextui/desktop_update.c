#include "desktop_update.h"
// defines.h self-includes platform.h, which is what actually defines
// HAS_RUNTIME_PATHS -- it must be pulled in unconditionally, before the gate
// below is evaluated, or the gate always sees HAS_RUNTIME_PATHS undefined
// (nothing else in this translation unit has included platform.h yet) and
// this compiles as the device no-op stub even on desktop builds.
#include "defines.h"
#ifdef HAS_RUNTIME_PATHS
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_confirmdialog.h"

#ifndef BUILD_TAG
#define BUILD_TAG "untagged"
#endif

static volatile int update_ready = 0; // set by the check thread
static char update_tag[64];
static char update_url[512];
static int offered = 0;

static void* check_thread(void* arg) {
	(void)arg;
	char cmd[MAX_PATH + 64];
	snprintf(cmd, sizeof(cmd), "%s/check-update.sh %s", BIN_PATH, BUILD_TAG);
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
			update_ready = 1;
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
	if (!update_ready || offered)
		return;
	offered = 1;
	char title[96];
	snprintf(title, sizeof(title), "Update available: %s", update_tag);
	if (!UI_confirmModal(screen, title, "Download and restart NXRedux?", NULL, true, false))
		return;
	char cmd[MAX_PATH + 600];
	snprintf(cmd, sizeof(cmd), "\"%s/self-update.sh\" \"%s\"", BIN_PATH, update_url);
	if (system(cmd) == 0)
		exit(0); // backend spawned the relaunch
}
#else
void DesktopUpdate_startCheck(void) {}
void DesktopUpdate_offerIfReady(SDL_Surface* screen) {
	(void)screen;
}
#endif
