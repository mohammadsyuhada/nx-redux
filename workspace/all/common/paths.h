#ifndef __PATHS_H__
#define __PATHS_H__

// Runtime path roots for desktop builds (HAS_RUNTIME_PATHS). Device
// platforms never include this; their roots stay compile-time literals in
// defines.h. Deliberately SDL-free and defines.h-free so it host-tests.
//
// Resolution: sdcard = $NXREDUX_SDCARD else $HOME/NXRedux;
//             system = $NXREDUX_SYSTEM_ROOT else <sdcard>/.system.

#define PATHS_MAX 512 // keep equal to MAX_PATH in defines.h

void PATHS_init(const char* platform_name); // must run before any path macro is read

extern char PATHS_SDCARD[PATHS_MAX];
extern char PATHS_SYSTEM[PATHS_MAX];
extern char PATHS_ROOT_SYSTEM[PATHS_MAX]; // trailing slash, like ROOT_SYSTEM_PATH
extern char PATHS_ROMS[PATHS_MAX];
extern char PATHS_RES[PATHS_MAX];
extern char PATHS_SHARED_SYSTEM[PATHS_MAX];
extern char PATHS_SHARED_BIN[PATHS_MAX];
extern char PATHS_USERDATA[PATHS_MAX];
extern char PATHS_SHARED_USERDATA[PATHS_MAX];
extern char PATHS_PAKS[PATHS_MAX];
extern char PATHS_BIN[PATHS_MAX];
extern char PATHS_TOOLS[PATHS_MAX];
extern char PATHS_RECENT[PATHS_MAX];
extern char PATHS_SHORTCUTS[PATHS_MAX];
extern char PATHS_SIMPLE_MODE[PATHS_MAX];
extern char PATHS_AUTO_RESUME[PATHS_MAX];
extern char PATHS_GAME_SWITCHER_PERSIST[PATHS_MAX];
extern char PATHS_FAUX_RECENT[PATHS_MAX];
extern char PATHS_COLLECTIONS[PATHS_MAX];
extern char PATHS_EMULIST_CACHE[PATHS_MAX];
extern char PATHS_ROMINDEX_CACHE[PATHS_MAX];

#endif
