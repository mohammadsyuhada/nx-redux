#include "scraper_scan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool suffix(const char* s, const char* suf) {
	size_t ls = strlen(s), lf = strlen(suf);
	return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

static bool is_dir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool Scan_hidden(const char* name) {
	return name[0] == '.' || suffix(name, ".disabled") || strcmp(name, "map.txt") == 0;
}

bool Scan_folderGameFile(const char* dir_path, char* out, size_t out_size) {
	const char* name = strrchr(dir_path, '/');
	name = name ? name + 1 : dir_path;
	snprintf(out, out_size, "%s/%s.cue", dir_path, name);
	if (is_file(out))
		return true;
	snprintf(out, out_size, "%s/%s.m3u", dir_path, name);
	if (is_file(out))
		return true;
	out[0] = '\0';
	return false;
}

// "Sub/Game.gba" -> "Sub/Game" (only the basename's last extension goes).
static void strip_ext(char* label) {
	char* base = strrchr(label, '/');
	base = base ? base + 1 : label;
	char* dot = strrchr(base, '.');
	if (dot && dot != base)
		*dot = '\0';
}

// <dir>/<base>.<ext> -> <dir>/.media/<base>.png; a folder game passes its
// folder path here, so its art lands beside the folder in the parent's .media.
static void art_path(const char* entry_path, char* out, size_t out_size) {
	char base[SCAN_NAME_MAX];
	const char* slash = strrchr(entry_path, '/');
	snprintf(base, sizeof(base), "%s", slash ? slash + 1 : entry_path);
	char* dot = strrchr(base, '.');
	if (dot && dot != base)
		*dot = '\0';
	int dir_len = slash ? (int)(slash - entry_path) : 1;
	snprintf(out, out_size, "%.*s/.media/%s.png", dir_len, slash ? entry_path : ".", base);
}

static int name_cmp(const void* a, const void* b) {
	return strcasecmp(*(const char* const*)a, *(const char* const*)b);
}

typedef struct {
	const char* root;
	ScanGameCb cb;
	void* userdata;
	int count;
	bool stopped;
} Walk;

static void emit(Walk* w, const char* entry_path, const char* game_file, bool folder_game) {
	ScanGame g;
	memset(&g, 0, sizeof(g));
	snprintf(g.path, sizeof(g.path), "%s", game_file);
	const char* fn = strrchr(game_file, '/');
	snprintf(g.filename, sizeof(g.filename), "%s", fn ? fn + 1 : game_file);
	size_t root_len = strlen(w->root);
	const char* rel = entry_path;
	if (strncmp(entry_path, w->root, root_len) == 0 && entry_path[root_len] == '/')
		rel = entry_path + root_len + 1;
	snprintf(g.label, sizeof(g.label), "%s", rel);
	if (!folder_game)
		strip_ext(g.label);
	art_path(entry_path, g.art_png, sizeof(g.art_png));
	g.folder_game = folder_game;
	w->count++;
	if (!w->cb(&g, w->userdata))
		w->stopped = true;
}

static void walk_dir(Walk* w, const char* dir, int depth) {
	if (depth > SCAN_MAX_DEPTH || w->stopped)
		return;
	DIR* dh = opendir(dir);
	if (!dh)
		return;
	// Sorted so results are stable across filesystems (vfat readdir order is
	// creation order).
	char** names = NULL;
	int n = 0, cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (Scan_hidden(dp->d_name))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 64;
			char** grown = realloc(names, cap * sizeof(*names));
			if (!grown)
				break;
			names = grown;
		}
		names[n++] = strdup(dp->d_name);
	}
	closedir(dh);
	if (n > 1)
		qsort(names, n, sizeof(*names), name_cmp);

	for (int i = 0; i < n && !w->stopped; i++) {
		char full[SCAN_PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", dir, names[i]);
		if (is_dir(full)) {
			char game_file[SCAN_PATH_MAX];
			if (Scan_folderGameFile(full, game_file, sizeof(game_file)))
				emit(w, full, game_file, true);
			else
				walk_dir(w, full, depth + 1);
		} else if (is_file(full)) {
			emit(w, full, full, false);
		}
	}
	for (int i = 0; i < n; i++)
		free(names[i]);
	free(names);
}

int Scan_walk(const char* root, ScanGameCb cb, void* userdata) {
	Walk w = {.root = root, .cb = cb, .userdata = userdata, .count = 0, .stopped = false};
	walk_dir(&w, root, 0);
	return w.count;
}
