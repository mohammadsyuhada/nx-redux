// Host test for paths.c resolution. Compile + run via run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../paths.h"

static void reset_env(void) {
	unsetenv("NXREDUX_SDCARD");
	unsetenv("NXREDUX_SYSTEM_ROOT");
	setenv("HOME", "/home/tester", 1);
}

int main(void) {
	// 1. defaults: card = $HOME/NXRedux, system = <card>/.system
	reset_env();
	PATHS_init("desktop");
	assert(!strcmp(PATHS_SDCARD, "/home/tester/NXRedux"));
	assert(!strcmp(PATHS_SYSTEM, "/home/tester/NXRedux/.system"));
	assert(!strcmp(PATHS_ROMS, "/home/tester/NXRedux/Roms"));
	assert(!strcmp(PATHS_USERDATA, "/home/tester/NXRedux/.userdata/desktop"));
	assert(!strcmp(PATHS_ROOT_SYSTEM, "/home/tester/NXRedux/.system/"));
	assert(!strcmp(PATHS_RECENT, "/home/tester/NXRedux/.userdata/shared/.minui/recent.txt"));

	// 2. NXREDUX_SDCARD override moves both roots
	reset_env();
	setenv("NXREDUX_SDCARD", "/var/tmp/nxredux/sdcard", 1);
	PATHS_init("desktop");
	assert(!strcmp(PATHS_SDCARD, "/var/tmp/nxredux/sdcard"));
	assert(!strcmp(PATHS_SYSTEM, "/var/tmp/nxredux/sdcard/.system"));

	// 3. NXREDUX_SYSTEM_ROOT overrides system root independently (bundle case)
	reset_env();
	setenv("NXREDUX_SDCARD", "/home/tester/NXRedux", 1);
	setenv("NXREDUX_SYSTEM_ROOT", "/Applications/NXRedux.app/Contents/Resources/system", 1);
	PATHS_init("desktop");
	assert(!strcmp(PATHS_SYSTEM, "/Applications/NXRedux.app/Contents/Resources/system"));
	assert(!strcmp(PATHS_PAKS, "/Applications/NXRedux.app/Contents/Resources/system/paks"));
	assert(!strcmp(PATHS_BIN, "/Applications/NXRedux.app/Contents/Resources/system/bin"));
	assert(!strcmp(PATHS_RES, "/Applications/NXRedux.app/Contents/Resources/system/res"));
	// card-side paths unaffected by the system override
	assert(!strcmp(PATHS_ROMS, "/home/tester/NXRedux/Roms"));

	printf("test_paths: OK\n");
	return 0;
}
