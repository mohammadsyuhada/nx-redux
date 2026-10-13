#include "ratools_reset.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"
#include "ra_paths.h"

static const char* ra_root(void) {
	return RA_USER_DIR;
}

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
	char path[MAX_PATH];
	// offline journals (pending + confirmed + last-sync)
	snprintf(path, sizeof(path), "%s/pending", ra_root());
	rat_rm_rf(path);
	// per-game server unlock state
	snprintf(path, sizeof(path), "%s/cache/sessions", ra_root());
	rat_rm_rf(path);
	// cached login (points, token validation)
	snprintf(path, sizeof(path), "%s/cache/login.json", ra_root());
	unlink(path);
}

void RATReset_clearAll(void) {
	// nuke the whole tree (game data + fallback badges included); a later
	// online play / prefetch rebuilds it. The console folders'
	// .retroachievements (hashes.json, badges) stay: rom fingerprints and
	// images, nothing account-related.
	rat_rm_rf(ra_root());
}
