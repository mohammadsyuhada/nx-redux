#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "iv_fileops.h"

// Split `path` into its parent directory (without trailing slash; "" if
// `path` has no '/') and its basename. Both out buffers are the caller's
// 512-byte scratch, matching iv_browser.c's path sizing. Returns false (and
// leaves the out buffers unwritten) on truncation.
static bool split_path(const char* path, char* dir_out, int dir_sz,
					   char* base_out, int base_sz) {
	const char* slash = strrchr(path, '/');
	if (slash) {
		int dlen = (int)(slash - path);
		if (dlen >= dir_sz)
			return false;
		memcpy(dir_out, path, dlen);
		dir_out[dlen] = '\0';
		int n = snprintf(base_out, base_sz, "%s", slash + 1);
		if (n < 0 || n >= base_sz)
			return false;
	} else {
		dir_out[0] = '\0';
		int n = snprintf(base_out, base_sz, "%s", path);
		if (n < 0 || n >= base_sz)
			return false;
	}
	return true;
}

// Pointer to the chars after the last '.' in `basename`, or "" when there's
// no extension (no dot, or the dot is the first character).
static const char* find_ext(const char* basename) {
	const char* dot = strrchr(basename, '.');
	if (!dot || dot == basename)
		return "";
	return dot + 1;
}

// Shared by IvFileops_renameFileKeepExt/renameDir: builds dir/new_name (or
// dir/new_name.ext when ext[0]), refuses on empty/'/'-containing new_name or
// truncation, and on an already-exists collision ALSO writes the colliding
// path to out_path before returning false (see iv_fileops.h) so the caller
// can tell that refusal apart from a real rename() failure. All validation,
// including the out_path capacity check, happens BEFORE rename() is called -
// a guard must never report failure for a rename that already happened - so
// out_path is written and re-checked first, and only a rename() that
// actually runs and succeeds returns true.
static bool do_rename(const char* path, const char* new_name, const char* ext,
					  char* out_path, int out_sz) {
	if (!path || !new_name || !out_path || out_sz <= 0)
		return false;
	if (new_name[0] == '\0') {
		fprintf(stderr, "iv_fileops: empty name refused\n");
		return false;
	}
	if (strchr(new_name, '/')) {
		fprintf(stderr, "iv_fileops: name contains '/': %s\n", new_name);
		return false;
	}

	char dir[512], base[512];
	if (!split_path(path, dir, sizeof(dir), base, sizeof(base))) {
		fprintf(stderr, "iv_fileops: path too long: %s\n", path);
		return false;
	}

	char new_path[512];
	int n = ext[0] ? snprintf(new_path, sizeof(new_path), "%s%s%s.%s",
							  dir, dir[0] ? "/" : "", new_name, ext)
				   : snprintf(new_path, sizeof(new_path), "%s%s%s",
							  dir, dir[0] ? "/" : "", new_name);
	if (n < 0 || n >= (int)sizeof(new_path)) {
		fprintf(stderr, "iv_fileops: new path too long\n");
		return false;
	}

	if (access(new_path, F_OK) == 0) {
		fprintf(stderr, "iv_fileops: target already exists: %s\n", new_path);
		// Lets the caller identify this refusal via access(out_path, F_OK)
		// (see iv_fileops.h). On truncation, clear out_path instead of
		// leaving a partial path in it, so the wiring's access() check
		// misses and falls back to a plain "Rename failed".
		int on = snprintf(out_path, out_sz, "%s", new_path);
		if (on < 0 || on >= out_sz)
			out_path[0] = '\0';
		return false;
	}

	// Validate BEFORE mutating anything: a guard must never report failure
	// for a rename() that already happened. Write the result into out_path
	// first, refusing on truncation without ever calling rename().
	int on = snprintf(out_path, out_sz, "%s", new_path);
	if (on < 0 || on >= out_sz) {
		fprintf(stderr, "iv_fileops: out_path buffer too small\n");
		out_path[0] = '\0';
		return false;
	}

	if (rename(path, new_path) != 0) {
		fprintf(stderr, "iv_fileops: rename(%s, %s) failed\n", path, new_path);
		out_path[0] = '\0';
		return false;
	}
	return true;
}

bool IvFileops_renameFileKeepExt(const char* path, const char* new_base,
								 char* out_path, int out_sz) {
	if (!path)
		return false;

	char dir[512], base[512];
	if (!split_path(path, dir, sizeof(dir), base, sizeof(base))) {
		fprintf(stderr, "iv_fileops: path too long: %s\n", path);
		return false;
	}
	const char* ext = find_ext(base); // "" if the original file has no extension

	// A typed name that already ends with the original extension
	// (case-insensitive) is the whole desired name, not a request to double
	// it up - strip that suffix before ext gets re-appended below.
	char stripped[512];
	if (new_base) {
		int n = snprintf(stripped, sizeof(stripped), "%s", new_base);
		if (n < 0 || n >= (int)sizeof(stripped))
			stripped[0] = '\0'; // treated as empty -> refused by do_rename
		size_t nl = strlen(stripped), el = strlen(ext);
		if (el > 0 && nl > el + 1 && stripped[nl - el - 1] == '.' &&
			strcasecmp(stripped + nl - el, ext) == 0) {
			stripped[nl - el - 1] = '\0';
		}
	} else {
		stripped[0] = '\0';
	}

	return do_rename(path, stripped, ext, out_path, out_sz);
}

bool IvFileops_renameDir(const char* path, const char* new_name,
						 char* out_path, int out_sz) {
	if (!path)
		return false;
	return do_rename(path, new_name ? new_name : "", "", out_path, out_sz);
}

bool IvFileops_deleteFile(const char* path) {
	if (!path)
		return false;
	if (unlink(path) != 0) {
		fprintf(stderr, "iv_fileops: unlink(%s) failed\n", path);
		return false;
	}
	return true;
}

// opendir recursion: recurse into subdirectories depth-first, unlink files,
// rmdir on the way back out. Best-effort - one failed child doesn't stop the
// rest of the tree from being cleaned up - but any failure anywhere (or the
// final rmdir failing) makes the overall call report false.
static bool delete_tree_rec(const char* path) {
	struct stat st;
	if (lstat(path, &st) != 0) {
		fprintf(stderr, "iv_fileops: lstat(%s) failed\n", path);
		return false;
	}
	if (!S_ISDIR(st.st_mode))
		return IvFileops_deleteFile(path);

	DIR* d = opendir(path);
	if (!d) {
		fprintf(stderr, "iv_fileops: opendir(%s) failed\n", path);
		return false;
	}
	bool ok = true;
	struct dirent* ent;
	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		char child[512];
		int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
		if (n < 0 || n >= (int)sizeof(child)) {
			fprintf(stderr, "iv_fileops: child path too long under %s\n", path);
			ok = false;
			continue;
		}
		if (!delete_tree_rec(child))
			ok = false;
	}
	closedir(d);
	if (rmdir(path) != 0) {
		fprintf(stderr, "iv_fileops: rmdir(%s) failed\n", path);
		ok = false;
	}
	return ok;
}

bool IvFileops_deleteTree(const char* path) {
	if (!path)
		return false;
	return delete_tree_rec(path);
}
