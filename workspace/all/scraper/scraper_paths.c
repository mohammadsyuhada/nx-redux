// Art-path helpers and the Reset artwork delete (per .media, and across Roms), split into their own TU so host tests
// can link them without the ScreenScraper API/network stack that scraper_core.c pulls in.
#include "scraper_core.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

void Scraper_variantPath(const char* out_png, const char* variant,
						 char* out, size_t out_size) {
	if (!variant || !variant[0]) {
		snprintf(out, out_size, "%s", out_png);
		return;
	}

	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", out_png);
	char* slash = strrchr(dir, '/');
	if (!slash) {
		snprintf(out, out_size, "%s/%s", variant, out_png);
		return;
	}
	*slash = '\0';
	snprintf(out, out_size, "%s/%s/%s", dir, variant, slash + 1);
}

// Delete every PNG directly inside one variant folder. Returns the count deleted.
static int deletePngsIn(const char* dir_path) {
	int deleted = 0;
	DIR* dir = opendir(dir_path);
	if (!dir)
		return 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		size_t n = strlen(entry->d_name);
		if (entry->d_name[0] == '.' || n <= 4 || strcasecmp(entry->d_name + n - 4, ".png") != 0)
			continue;
		char png_path[MAX_PATH + 256];
		snprintf(png_path, sizeof(png_path), "%s/%s", dir_path, entry->d_name);
		if (remove(png_path) == 0)
			deleted++;
	}
	closedir(dir);
	return deleted;
}

int Scraper_deleteMediaArtwork(const char* media_path) {
	static const char* variants[] = {"screenshot", "boxart", "boxart2d", "wheel", "mix"};
	int deleted = 0;
	for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
		char variant_path[MAX_PATH];
		snprintf(variant_path, sizeof(variant_path), "%s/%s", media_path, variants[v]);
		deleted += deletePngsIn(variant_path);
		rmdir(variant_path); // no-op unless it is now empty
	}
	return deleted;
}

// Same depth cap as the game scan and Optimize images (scraper_scan.h SCAN_MAX_DEPTH).
#define RESET_MAX_DEPTH 8

static bool isDirEntry(const char* path, const struct dirent* e) {
	if (e->d_type == DT_DIR)
		return true;
	if (e->d_type != DT_UNKNOWN)
		return false;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// <dir>/.media, then every sub-folder's, the way Optimize images walks: nested game folders keep their own .media.
// Hidden names (".", "..", .media itself, dot-folders) are skipped.
static int deleteArtworkUnder(const char* dir_path, int depth) {
	char media[MAX_PATH];
	snprintf(media, sizeof(media), "%s/.media", dir_path);
	int deleted = Scraper_deleteMediaArtwork(media);
	if (depth >= RESET_MAX_DEPTH)
		return deleted;
	DIR* dir = opendir(dir_path);
	if (!dir)
		return deleted;
	struct dirent* e;
	while ((e = readdir(dir)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char sub[MAX_PATH];
		snprintf(sub, sizeof(sub), "%s/%s", dir_path, e->d_name);
		if (isDirEntry(sub, e))
			deleted += deleteArtworkUnder(sub, depth + 1);
	}
	closedir(dir);
	return deleted;
}

int Scraper_deleteAllArtwork(const char* roms_root) {
	int deleted = 0;
	DIR* roms = opendir(roms_root);
	if (!roms)
		return 0;
	struct dirent* e;
	while ((e = readdir(roms)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char sys[MAX_PATH];
		snprintf(sys, sizeof(sys), "%s/%s", roms_root, e->d_name);
		if (isDirEntry(sys, e))
			deleted += deleteArtworkUnder(sys, 1);
	}
	closedir(roms);
	return deleted;
}
