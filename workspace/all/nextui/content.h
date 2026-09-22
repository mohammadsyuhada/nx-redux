#ifndef CONTENT_H
#define CONTENT_H

#include "types.h"
#include <stdbool.h>

// Set simple_mode for content functions
void Content_setSimpleMode(bool mode);

// Directory construction
Directory* Directory_new(char* path, int selected);

// Content query helpers
int hasEmu(char* emu_name);
int hasCue(char* dir_path, char* cue_path);
int hasFolderM3u(char* dir_path, char* m3u_path);
int hasM3u(char* rom_path, char* m3u_path);
int dirGameFile(const char* dir_path, char* out_path);
int hasTools(void);
int canPinEntry(Entry* entry);
int isConsoleDir(char* path);

// Content retrieval
void Content_invalidateEmulist(void);
Array* getCollections(void);
int getFirstDisc(char* m3u_path, char* disc_path);
Array* getTools(void);

// Search
Array* Content_searchRoms(const char* query);
// Byte length of the rom-label part of an indexed search name (before " (TAG)").
int Content_romLabelLen(const char* indexed_name);

#endif // CONTENT_H
