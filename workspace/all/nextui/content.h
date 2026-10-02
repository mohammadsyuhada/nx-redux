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
int hasCollections(void);
// How many rows the Consoles tab lists. The emulist is validated against the card once per process and kept
// in memory (cleared by Content_invalidateEmulist / Content_forgetRom), so this costs no I/O after the first call.
int Content_consoleCount(void);
// Bumped each time that in-memory emulist is dropped or refilled: a Consoles root built at an older generation is
// stale.
unsigned Content_libraryGen(void);
// True when the Consoles tab has anything to list (Content_consoleCount() > 0).
int Content_hasConsoles(void);
int canPinEntry(Entry* entry);
int isConsoleDir(char* path);

// Content retrieval
void Content_invalidateEmulist(void);
// Delete Rom bookkeeping (UI thread). Content_romCachesFresh, called just before the delete, says whether
// the rom caches match the card; Content_forgetRom, called after the delete and its map.txt clean-up, then
// drops the removed rows (removed_path itself or anything below it) from the rom index and restamps both
// caches, so console counts and Search follow at once without a full rescan. When the caches were not
// fresh, the delete empties its console folder, or removed_path still exists (the delete failed), it falls
// back to invalidating them.
bool Content_romCachesFresh(void);
void Content_forgetRom(const char* removed_path, bool caches_were_fresh);
Array* getCollections(void);
int getFirstDisc(char* m3u_path, char* disc_path);
Array* getTools(void);

// What opening this Consoles row lists, over every folder the row merges (the emulist's count column, a table
// lookup); a folder the Consoles tab doesn't list counts as the row that collates it, else 0. -1 for a row
// that isn't a console folder.
int Content_consoleGameCount(const Entry* console_row);
// The rom caches' fp (16 hex digits); "empty" when the Consoles tab lists nothing (an empty library is still
// a library).
const char* Content_libraryFingerprint(void);
// Calls cb (may be NULL) with the SD path of each line of the collection file at `path` that resolves to an
// existing file -- the rows opening the collection lists -- until cb returns false. Returns how many resolved,
// or -1 when the file can't be opened. Thread-safe (file I/O only).
int Content_forEachCollectionGame(const char* path, bool (*cb)(const char* sd_path, void* ctx), void* ctx);

// Search
Array* Content_searchRoms(const char* query);
// Byte length of the rom-label part of an indexed search name (before " (TAG)").
int Content_romLabelLen(const char* indexed_name);

#endif // CONTENT_H
