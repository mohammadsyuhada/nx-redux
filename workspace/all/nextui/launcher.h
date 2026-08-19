#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "defines.h"
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
int autoResume(void);

// Game launching
void openPak(char* path);
void openScript(char* script_path, char* arg, char* last_path);
void openRom(char* path, char* last);
void Entry_open(Entry* self);

// Spawn the Artwork Manager pak's headless fetch for one ROM, fire-and-forget.
void openArtFetch(const char* rom, const char* out, const char* tag, const char* status);

// State persistence
void saveLast(char* path);
void loadLast(void);

#endif // LAUNCHER_H
