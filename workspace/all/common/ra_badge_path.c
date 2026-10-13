#include "ra_badge_path.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool fits(int len, size_t n) {
	return len > 0 && (size_t)len < n;
}

bool RA_BadgePath_dir(const char* badges_dir, uint32_t game_id, char* buf, size_t n) {
	if (!buf || n == 0)
		return false;
	buf[0] = '\0';
	if (!badges_dir || !badges_dir[0] || game_id == 0)
		return false;
	if (!fits(snprintf(buf, n, "%s/%u", badges_dir, game_id), n)) {
		buf[0] = '\0';
		return false;
	}
	return true;
}

bool RA_BadgePath_file(const char* badges_dir, uint32_t game_id, const char* badge_name, bool locked,
					   char* buf, size_t n) {
	if (!buf || n == 0)
		return false;
	buf[0] = '\0';
	// the name comes from the server: never let it leave the game folder
	if (!badge_name || !badge_name[0] || strchr(badge_name, '/') || strcmp(badge_name, "..") == 0 ||
		strcmp(badge_name, ".") == 0)
		return false;
	if (!badges_dir || !badges_dir[0] || game_id == 0)
		return false;
	if (!fits(snprintf(buf, n, "%s/%u/%s%s.png", badges_dir, game_id, badge_name, locked ? "_lock" : ""), n)) {
		buf[0] = '\0';
		return false;
	}
	return true;
}

bool RA_BadgePath_ensureDir(const char* badges_dir, uint32_t game_id) {
	char dir[512];
	if (!RA_BadgePath_dir(badges_dir, game_id, dir, sizeof(dir)))
		return false;
	mkdir(badges_dir, 0755);
	if (mkdir(dir, 0755) == 0 || errno == EEXIST) {
		struct stat st;
		return stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
	}
	return false;
}
