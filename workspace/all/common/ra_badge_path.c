#include "ra_badge_path.h"

#include "ra_hash_cache.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define RA_BADGE_MAX_PATH 1024

static bool fits(int len, size_t n) {
	return len > 0 && (size_t)len < n;
}

bool RA_BadgePath_dir(const char* fallback_dir, const char* rom_path, uint32_t game_id, char* buf, size_t n) {
	if (!buf || n == 0)
		return false;
	buf[0] = '\0';
	if (game_id == 0)
		return false;
	char folder[RA_BADGE_MAX_PATH];
	int len;
	if (rom_path && rom_path[0] && RA_HashCache_folderDir(rom_path, folder, sizeof(folder)))
		len = snprintf(buf, n, "%s/%s/%u", folder, RA_BADGE_FOLDER, game_id);
	else if (fallback_dir && fallback_dir[0])
		len = snprintf(buf, n, "%s/%u", fallback_dir, game_id);
	else
		return false;
	if (!fits(len, n)) {
		buf[0] = '\0';
		return false;
	}
	return true;
}

bool RA_BadgePath_inDir(const char* game_dir, const char* badge_name, bool locked, char* buf, size_t n) {
	if (!buf || n == 0)
		return false;
	buf[0] = '\0';
	// the name comes from the server: never let it leave the game folder
	if (!badge_name || !badge_name[0] || strchr(badge_name, '/') || strcmp(badge_name, "..") == 0 ||
		strcmp(badge_name, ".") == 0)
		return false;
	if (!game_dir || !game_dir[0])
		return false;
	if (!fits(snprintf(buf, n, "%s/%s%s.png", game_dir, badge_name, locked ? "_lock" : ""), n)) {
		buf[0] = '\0';
		return false;
	}
	return true;
}

bool RA_BadgePath_file(const char* fallback_dir, const char* rom_path, uint32_t game_id, const char* badge_name,
					   bool locked, char* buf, size_t n) {
	char dir[RA_BADGE_MAX_PATH];
	if (!RA_BadgePath_dir(fallback_dir, rom_path, game_id, dir, sizeof(dir))) {
		if (buf && n)
			buf[0] = '\0';
		return false;
	}
	return RA_BadgePath_inDir(dir, badge_name, locked, buf, n);
}

static bool is_dir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool RA_BadgePath_ensureDir(const char* game_dir) {
	if (!game_dir || !game_dir[0])
		return false;
	if (is_dir(game_dir))
		return true;
	char path[RA_BADGE_MAX_PATH];
	if (!fits(snprintf(path, sizeof(path), "%s", game_dir), sizeof(path)))
		return false;
	// the game folder's parent and grandparent, outermost first
	char* cut[2] = {NULL, NULL};
	cut[0] = strrchr(path, '/');
	if (cut[0] && cut[0] != path) {
		*cut[0] = '\0';
		cut[1] = strrchr(path, '/');
		*cut[0] = '/';
	}
	for (int i = 1; i >= 0; i--) {
		if (!cut[i] || cut[i] == path)
			continue;
		*cut[i] = '\0';
		mkdir(path, 0755);
		*cut[i] = '/';
	}
	if (mkdir(path, 0755) == 0 || errno == EEXIST)
		return is_dir(path);
	return false;
}
