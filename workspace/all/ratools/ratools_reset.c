#include "ratools_reset.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"

#define RA_ROOT SHARED_USERDATA_PATH "/.ra"

// Recursively delete a directory's contents and the directory itself.
static void rat_rm_rf(const char* path) {
	struct stat st;
	if (lstat(path, &st) != 0)
		return;

	if (S_ISDIR(st.st_mode)) {
		DIR* d = opendir(path);
		if (d) {
			struct dirent* ent;
			while ((ent = readdir(d))) {
				if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
					continue;
				char child[1024];
				snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
				rat_rm_rf(child);
			}
			closedir(d);
		}
		rmdir(path);
	} else {
		unlink(path);
	}
}

void RATReset_clearAccountData(void) {
	// offline journals (pending + confirmed + last-sync)
	rat_rm_rf(RA_ROOT "/pending");
	// per-game server unlock state
	rat_rm_rf(RA_ROOT "/cache/sessions");
	// cached login (points, token validation)
	unlink(RA_ROOT "/cache/login.json");
}

void RATReset_clearAll(void) {
	// nuke the whole tree; a later online play / prefetch rebuilds it
	rat_rm_rf(RA_ROOT);
}
