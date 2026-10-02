#ifndef EMULIST_MODEL_H
#define EMULIST_MODEL_H

#include <stdbool.h>
#include <stddef.h>

// The Consoles list cache (EMULIST_CACHE_PATH) after its "#fp=" header: one row per console,
// "path\tname\tcount\n", where count is how many games opening the row lists (its rom-index rows over
// every folder the row collates). No SDL, no file I/O: content.c does the reading and writing.

// The collation rule shared by the console listing, the rom index and the counts: a console row merges
// every Roms folder whose path starts with its own path cut after the last '(' ("Roms/Game Boy (GB)" ->
// "Roms/Game Boy ("), case-insensitively. A path with no '(' is its own prefix, so "Roms/Ports" also
// collates "Roms/Ports2" -- a long-standing quirk the counts keep so they match the listing.
void Emulist_collatedPrefix(const char* path, char* out, size_t size);
// True when folder_path is collated by the console row at row_path (the rule above).
bool Emulist_collates(const char* row_path, const char* folder_path);
// Splits one row in place: exactly "path\tname\tcount" with a non-empty path and name and a decimal
// count >= 0 with nothing after it. False (outputs untouched) on anything else, e.g. a pre-count
// two-column row.
bool Emulist_parseRow(char* line, char** path, char** name, int* count);
// "path\tname\tcount\n" into out; returns what snprintf returns.
int Emulist_formatRow(const char* path, const char* name, int count, char* out, size_t size);
// out[i] = the sum of folder_counts[j] over every folder j that console i collates.
void Emulist_collateCounts(const char* const* console_paths, int n, const char* const* folder_paths,
						   const int* folder_counts, int m, int* out);
// A delete dropped `dropped` rom-index rows from the folder at removed_folder_path: lower the row's
// count when it collates that folder, never below 0.
void Emulist_adjustCount(const char* row_path, const char* removed_folder_path, int dropped, int* count);

#endif
