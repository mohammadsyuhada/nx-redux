#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "defines.h"
#include "menutabs_model.h"
#include "types.h"
#include <stdbool.h>

// Globals shared between launcher and main loop
// Owned by nextui.c, accessed by launcher.c
extern Directory* top;
extern Array* stack;
extern bool quit;
extern bool startgame;

typedef struct {
	bool can_resume;
	bool should_resume;
	bool has_preview;
	bool has_boxart;
	char slot_path[MAX_PATH];
	char preview_path[MAX_PATH];
	char boxart_path[MAX_PATH];
} ResumeState;

typedef struct {
	int depth;
	int relative;
	int selected;
	int start;
	int end;
	// path of the directory this state was saved from: depth+relative alone
	// can't tell "re-entered the same folder" from "opened a different one
	// without moving the parent cursor" (context-menu Tools does the latter)
	char path[MAX_PATH];
} RestoreState;

extern ResumeState resume;
extern RestoreState restore;

// Navigation
void openDirectory(char* path, int auto_launch);
void closeDirectory(void);

// Resume
void readyResume(Entry* entry);
// Bumped by every readyResume call, from any screen: a caller that skips a repeat probe for the same entry
// compares it to tell whether something else (Search, the switcher, Home) overwrote `resume` since.
unsigned readyResumeCount(void);
// The state readyResume would give `entry`, into *out, leaving the shared `resume` and its count as they are: for a
// look ahead (the Game Switcher List's neighbours' resume frames).
void readyResumeInto(Entry* entry, ResumeState* out);
int autoResume(void);
// Launch Options hand-off (PENDING_LAUNCH_PATH, written by a pak's launch_options.sh on Launch): consume the
// file and launch its ROM as an A press on it would (Recents, gametime, the list position kept for the return),
// always fresh (no save state). Returns 1 when a launch was queued (quit set); a missing or stale request (the
// ROM or its emu pak is gone) is removed and returns 0, so the menu boots as usual. Call after Menu_init.
int pendingLaunch(void);

// Game launching
void openPak(char* path);
// Launch a pak but return the user to the directory they were browsing
// (F1/F2 tool shortcuts fire from anywhere in the list).
void openPakInPlace(char* path);
void openScript(char* script_path, char* arg, char* last_path);
void openRom(char* path, char* last);
void Entry_open(Entry* self);

// Spawn the Artwork Manager pak's headless fetch for one ROM, fire-and-forget.
void openArtFetch(const char* rom, const char* out, const char* tag, const char* status);

// Push a hidden tab's list (Consoles, Collections or Tools) over the current tab with select_path's row selected
// (NULL: the first), so B comes back to the tab.
void pushTabOverTab(MenuTabId id, const char* select_path);

// State persistence
void saveLast(char* path);
void loadLast(void);

#endif // LAUNCHER_H
