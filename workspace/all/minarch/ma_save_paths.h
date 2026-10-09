#ifndef MA_SAVE_PATHS_H
#define MA_SAVE_PATHS_H
// Pure path and session-mode helpers for save states, kept free of minarch
// globals so the host tests (tests/test_save_paths.c) can cover them.
#include <stddef.h>

// strip a 1-4 letter extension (plus dot), matching getDisplayName's rule
void SavePaths_stripExtension(char* name);

// Writes the state file path for `slot` (AUTO_RESUME_SLOT = the auto-resume
// state) in `format` (STATE_FORMAT_*, config.h) into out. Returns 0, or -1 if
// it does not fit.
int SavePaths_state(char* out, size_t size, const char* states_dir, const char* alt_name, int format, int slot);

// Which save data a launch plays on, from the env the pre-launch scripts set.
typedef enum {
	NETPLAY_SAVES_NONE, // plain launch: no session
	NETPLAY_SAVES_REAL, // session on the device's own save (link sessions, lockstep host)
	NETPLAY_SAVES_COPY, // session on a copy of the host's save (NETPLAY_SAVES_DIR)
} NetplaySavesMode;
NetplaySavesMode SavePaths_netplayMode(const char* netplay_role, const char* netplay_saves_dir);

// Whether the auto-resume marker (AUTO_RESUME_PATH: the game path below the
// SD card root) names game_path.
int SavePaths_autoResumeIsGame(const char* marker, const char* game_path, const char* sdcard_path);
#endif
