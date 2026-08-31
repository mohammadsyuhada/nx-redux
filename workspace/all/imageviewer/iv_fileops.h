#ifndef IV_FILEOPS_H
#define IV_FILEOPS_H
#include <stdbool.h>

// Rename a file to new_base + the ORIGINAL extension, in the same directory.
// If new_base itself ends with the original extension (case-insensitive), that
// suffix is stripped first (typing "photo.jpg" for a .jpg is not an error).
// Refuses: empty new_base, names containing '/', target already exists.
// On success writes the new full path to out_path. Returns false on any failure.
//
// On the "target already exists" refusal specifically, out_path is ALSO
// written with the path that already exists, so a caller that pre-clears
// out_path can distinguish that refusal from any other failure with a plain
// access(out_path, F_OK) check (see imageviewer.c's rename wiring). All other
// refusals leave out_path untouched.
bool IvFileops_renameFileKeepExt(const char* path, const char* new_base,
								 char* out_path, int out_sz);

// Rename a directory (no extension logic). Same refusals, and the same
// already-exists out_path behavior as IvFileops_renameFileKeepExt above.
bool IvFileops_renameDir(const char* path, const char* new_name,
						 char* out_path, int out_sz);

// Delete one file. Returns false on failure.
bool IvFileops_deleteFile(const char* path);

// Recursively delete a directory tree (opendir recursion, not nftw).
// Returns false if anything could not be removed (best-effort continues,
// but the final rmdir failing = false).
bool IvFileops_deleteTree(const char* path);

#endif
