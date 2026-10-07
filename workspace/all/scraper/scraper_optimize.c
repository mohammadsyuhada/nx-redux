#include "scraper_optimize.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "api.h" // LOG_warn
#include "png_palette.h"
#include "sdl.h"

// Same depth cap as the game scan (scraper_scan.h SCAN_MAX_DEPTH).
#define MAX_DEPTH 8
// Larger pictures than this are not game art the menus draw; leave them be
// rather than spend tens of MB on one file (the device has ~1 GB).
#define MAX_PIXELS (4096 * 4096)

// Folder backgrounds nextui draws (bg.png; bglist.png and resolution
// variants like bg-1024.png from older themes) are never scraper art.
static bool isBackground(const char* name) {
	return strcasecmp(name, "bg.png") == 0 || strcasecmp(name, "bglist.png") == 0 ||
		   strncasecmp(name, "bg-", 3) == 0;
}

static bool isPng(const char* name) {
	size_t n = strlen(name);
	return n > 4 && strcasecmp(name + n - 4, ".png") == 0;
}

static bool isDir(const char* path, const struct dirent* e) {
	if (e->d_type == DT_DIR)
		return true;
	if (e->d_type != DT_UNKNOWN)
		return false;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

typedef struct {
	OptimizeList* list;
	int cap;
	bool oom;
} Collector;

static void addPath(Collector* c, const char* path) {
	if (c->oom)
		return;
	if (c->list->count == c->cap) {
		int ncap = c->cap ? c->cap * 2 : 256;
		char** grown = realloc(c->list->paths, sizeof(char*) * ncap);
		if (!grown) {
			c->oom = true;
			return;
		}
		c->list->paths = grown;
		c->cap = ncap;
	}
	char* dup = strdup(path);
	if (!dup) {
		c->oom = true;
		return;
	}
	c->list->paths[c->list->count++] = dup;
}

// The PNGs directly inside one directory (no recursion).
static void addPngsIn(Collector* c, const char* dir_path, bool skip_backgrounds) {
	DIR* dir = opendir(dir_path);
	if (!dir)
		return;
	struct dirent* e;
	while ((e = readdir(dir)) != NULL) {
		if (e->d_name[0] == '.' || !isPng(e->d_name))
			continue;
		if (skip_backgrounds && isBackground(e->d_name))
			continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir_path, e->d_name);
		if (isDir(path, e))
			continue;
		addPath(c, path);
	}
	closedir(dir);
}

// <dir>/.media/*.png plus its screenshot/ and boxart/ variants, then the same
// for every sub-folder (nested game folders keep their own .media).
static void walk(Collector* c, const char* dir_path, int depth) {
	char media[1024];
	snprintf(media, sizeof(media), "%s/.media", dir_path);
	addPngsIn(c, media, true);
	static const char* variants[] = {"screenshot", "boxart"};
	for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
		char variant[1100];
		snprintf(variant, sizeof(variant), "%s/%s", media, variants[v]);
		addPngsIn(c, variant, false);
	}
	if (depth >= MAX_DEPTH)
		return;
	DIR* dir = opendir(dir_path);
	if (!dir)
		return;
	struct dirent* e;
	while ((e = readdir(dir)) != NULL) {
		if (e->d_name[0] == '.')
			continue; // hidden, ".", "..", and .media itself (done above)
		char sub[1024];
		snprintf(sub, sizeof(sub), "%s/%s", dir_path, e->d_name);
		if (isDir(sub, e))
			walk(c, sub, depth + 1);
	}
	closedir(dir);
}

static int comparePaths(const void* a, const void* b) {
	return strcasecmp(*(char* const*)a, *(char* const*)b);
}

bool Optimize_collect(const char* roms_root, OptimizeList* out) {
	out->paths = NULL;
	out->count = 0;
	Collector c = {.list = out};
	DIR* roms = opendir(roms_root);
	if (!roms)
		return true;
	struct dirent* e;
	while ((e = readdir(roms)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char sys[1024];
		snprintf(sys, sizeof(sys), "%s/%s", roms_root, e->d_name);
		if (isDir(sys, e))
			walk(&c, sys, 1);
	}
	closedir(roms);
	if (c.oom) {
		Optimize_freeList(out);
		return false;
	}
	if (out->count > 1)
		qsort(out->paths, out->count, sizeof(char*), comparePaths);
	return true;
}

void Optimize_freeList(OptimizeList* list) {
	for (int i = 0; i < list->count; i++)
		free(list->paths[i]);
	free(list->paths);
	list->paths = NULL;
	list->count = 0;
}

OptimizeResult Optimize_file(const char* path, long long* saved_bytes) {
	if (PngPalette_isIndexedFile(path))
		return OPTIMIZE_ALREADY;
	struct stat st;
	if (stat(path, &st) != 0)
		return OPTIMIZE_FAILED;

	SDL_Surface* raw = IMG_Load(path);
	if (!raw) {
		LOG_warn("Optimize: could not read %s\n", path);
		return OPTIMIZE_FAILED;
	}
	if ((long long)raw->w * raw->h > MAX_PIXELS) {
		SDL_FreeSurface(raw);
		return OPTIMIZE_NO_GAIN;
	}
	// ARGB8888 for the quantizer, whatever the PNG held (RGB, RGBA, grey)
	SDL_Surface* img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!img)
		return OPTIMIZE_FAILED;

	PngPaletteImage pal;
	bool ok = false;
	if (SDL_LockSurface(img) == 0) {
		ok = PngPalette_quantize((const uint32_t*)img->pixels, img->w, img->h, img->pitch / 4, &pal);
		SDL_UnlockSurface(img);
	}
	SDL_FreeSurface(img);
	if (!ok)
		return OPTIMIZE_FAILED;

	uint8_t* data = NULL;
	size_t len = 0;
	ok = PngPalette_encode(&pal, &data, &len);
	PngPalette_free(&pal);
	if (!ok)
		return OPTIMIZE_FAILED;

	// Only replace when it actually saves space (tiny or already-flat art can
	// come out the same or larger); the original stays untouched otherwise.
	OptimizeResult result = OPTIMIZE_NO_GAIN;
	if ((long long)len < (long long)st.st_size) {
		if (PngPalette_writeFileAtomic(path, data, len)) {
			*saved_bytes = (long long)st.st_size - (long long)len;
			result = OPTIMIZE_DONE;
		} else {
			LOG_warn("Optimize: could not write %s\n", path);
			result = OPTIMIZE_FAILED;
		}
	}
	free(data);
	return result;
}
