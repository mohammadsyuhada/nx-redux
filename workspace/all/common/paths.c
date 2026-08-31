#include "paths.h"
#include <stdio.h>
#include <stdlib.h>

char PATHS_SDCARD[PATHS_MAX];
char PATHS_SYSTEM[PATHS_MAX];
char PATHS_ROOT_SYSTEM[PATHS_MAX];
char PATHS_ROMS[PATHS_MAX];
char PATHS_RES[PATHS_MAX];
char PATHS_SHARED_SYSTEM[PATHS_MAX];
char PATHS_SHARED_BIN[PATHS_MAX];
char PATHS_USERDATA[PATHS_MAX];
char PATHS_SHARED_USERDATA[PATHS_MAX];
char PATHS_PAKS[PATHS_MAX];
char PATHS_BIN[PATHS_MAX];
char PATHS_TOOLS[PATHS_MAX];
char PATHS_RECENT[PATHS_MAX];
char PATHS_SHORTCUTS[PATHS_MAX];
char PATHS_SIMPLE_MODE[PATHS_MAX];
char PATHS_AUTO_RESUME[PATHS_MAX];
char PATHS_GAME_SWITCHER_PERSIST[PATHS_MAX];
char PATHS_FAUX_RECENT[PATHS_MAX];
char PATHS_COLLECTIONS[PATHS_MAX];
char PATHS_EMULIST_CACHE[PATHS_MAX];
char PATHS_ROMINDEX_CACHE[PATHS_MAX];

void PATHS_init(const char* platform_name) {
	const char* env = getenv("NXREDUX_SDCARD");
	if (env && *env)
		snprintf(PATHS_SDCARD, PATHS_MAX, "%s", env);
	else {
		const char* home = getenv("HOME");
		snprintf(PATHS_SDCARD, PATHS_MAX, "%s/NXRedux", (home && *home) ? home : ".");
	}

	env = getenv("NXREDUX_SYSTEM_ROOT");
	if (env && *env)
		snprintf(PATHS_SYSTEM, PATHS_MAX, "%s", env);
	else
		snprintf(PATHS_SYSTEM, PATHS_MAX, "%s/.system", PATHS_SDCARD);

	snprintf(PATHS_ROOT_SYSTEM, PATHS_MAX, "%s/", PATHS_SYSTEM);
	snprintf(PATHS_ROMS, PATHS_MAX, "%s/Roms", PATHS_SDCARD);
	snprintf(PATHS_RES, PATHS_MAX, "%s/res", PATHS_SYSTEM);
	snprintf(PATHS_SHARED_SYSTEM, PATHS_MAX, "%s/shared", PATHS_SYSTEM);
	snprintf(PATHS_SHARED_BIN, PATHS_MAX, "%s/shared/bin", PATHS_SYSTEM);
	snprintf(PATHS_USERDATA, PATHS_MAX, "%s/.userdata/%s", PATHS_SDCARD, platform_name);
	snprintf(PATHS_SHARED_USERDATA, PATHS_MAX, "%s/.userdata/shared", PATHS_SDCARD);
	snprintf(PATHS_PAKS, PATHS_MAX, "%s/paks", PATHS_SYSTEM);
	snprintf(PATHS_BIN, PATHS_MAX, "%s/bin", PATHS_SYSTEM);
	snprintf(PATHS_TOOLS, PATHS_MAX, "%s/Tools", PATHS_SDCARD);
	snprintf(PATHS_RECENT, PATHS_MAX, "%s/.minui/recent.txt", PATHS_SHARED_USERDATA);
	snprintf(PATHS_SHORTCUTS, PATHS_MAX, "%s/.minui/shortcuts.txt", PATHS_SHARED_USERDATA);
	snprintf(PATHS_SIMPLE_MODE, PATHS_MAX, "%s/enable-simple-mode", PATHS_SHARED_USERDATA);
	snprintf(PATHS_AUTO_RESUME, PATHS_MAX, "%s/.minui/auto_resume.txt", PATHS_SHARED_USERDATA);
	snprintf(PATHS_GAME_SWITCHER_PERSIST, PATHS_MAX, "%s/.minui/game_switcher.txt", PATHS_SHARED_USERDATA);
	snprintf(PATHS_FAUX_RECENT, PATHS_MAX, "%s/Recently Played", PATHS_SDCARD);
	snprintf(PATHS_COLLECTIONS, PATHS_MAX, "%s/Collections", PATHS_SDCARD);
	snprintf(PATHS_EMULIST_CACHE, PATHS_MAX, "%s/emulist_cache.txt", PATHS_USERDATA);
	snprintf(PATHS_ROMINDEX_CACHE, PATHS_MAX, "%s/romindex_cache.txt", PATHS_USERDATA);
}
