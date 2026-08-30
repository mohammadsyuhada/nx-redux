#ifndef RECENTS_H
#define RECENTS_H

#include "types.h"
#include <stdbool.h>

#define MAX_RECENTS 24 // a multiple of all menu rows

///////////////////////////////////////
// Recent

typedef struct Recent {
	char* path; // NOTE: this is without the SDCARD_PATH prefix!
	char* alias;
	bool available;
} Recent;

// Requires hasEmu and hasM3u from content module
typedef int (*HasEmuFunc)(char* emu_name);
typedef int (*HasM3uFunc)(char* rom_path, char* m3u_path);

// Initialize/cleanup
void Recents_init(void);
void Recents_quit(void);

// Set dependency functions (must be called before use)
void Recents_setHasEmu(HasEmuFunc func);
void Recents_setHasM3u(HasM3uFunc func);

// Core API
void Recents_add(char* path, char* alias);
int Recents_load(void); // returns 1 if there are available recents

// Access
int Recents_count(void);
Recent* Recents_at(int index);
void Recents_removeAt(int index);
// Both take SDCARD-relative paths (the stored Recent form).
void Recents_removeByPath(char* path);
void Recents_updateAlias(char* path, char* alias);

// Entry conversion
Entry* Recents_entryFromRecent(Recent* recent);
Array* Recents_getEntries(void);

// Recent struct methods
void RecentArray_free(Array* self);

// Alias management (used by launcher for recent_alias)
void Recents_setAlias(char* alias);
char* Recents_getAlias(void);

#endif // RECENTS_H
