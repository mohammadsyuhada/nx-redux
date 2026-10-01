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
// True when the Consoles tab has anything to list (reads the cached emulist).
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

// What opening this Consoles row lists, over every folder the row merges; -1 when unknown (no rom index yet).
// Counted from the rom index cache (one read on first use, then a table lookup).
int Content_consoleGameCount(const Entry* console_row);
// The romindex fp (16 hex digits); "empty" when the index has no rows (or can't be read: an empty library is
// still a library); "" only when the index has rows but no fp header.
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
