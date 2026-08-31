#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "iv_browser.h"

// Supported image extensions
static const char* img_exts[] = {
	"png",
	"jpg",
	"jpeg",
	"bmp",
	"gif",
	NULL};

// Check if file is a supported image format
bool ImageBrowser_isImageFile(const char* filename) {
	if (!filename)
		return false;

	const char* dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return false;

	const char* ext = dot + 1;
	for (int i = 0; img_exts[i] != NULL; i++) {
		if (strcasecmp(ext, img_exts[i]) == 0)
			return true;
	}
	return false;
}

// Free browser entries
void ImageBrowser_freeEntries(ImageBrowserContext* ctx) {
	if (ctx->entries) {
		free(ctx->entries);
		ctx->entries = NULL;
	}
	ctx->entry_count = 0;
}

// Compare function for sorting entries (directories first, then alphabetical)
static int compare_entries(const void* a, const void* b) {
	const ImageEntry* ea = (const ImageEntry*)a;
	const ImageEntry* eb = (const ImageEntry*)b;

	// Directories come first
	if (ea->is_dir && !eb->is_dir)
		return -1;
	if (!ea->is_dir && eb->is_dir)
		return 1;

	// Alphabetical (case-insensitive)
	return strcasecmp(ea->name, eb->name);
}

// Load directory contents (image files + directories)
void ImageBrowser_loadDirectory(ImageBrowserContext* ctx, const char* path, const char* root) {
	ImageBrowser_freeEntries(ctx);

	strncpy(ctx->current_path, path, sizeof(ctx->current_path) - 1);
	ctx->current_path[sizeof(ctx->current_path) - 1] = '\0';

	// Create image folder if it doesn't exist and we're at root
	if (strcmp(path, root) == 0) {
		mkdir(path, 0755);
	}

	DIR* dir = opendir(path);
	if (!dir) {
		fprintf(stderr, "iv_browser: failed to open %s\n", path);
		return;
	}

	// First pass: count entries
	int dir_count = 0;
	int image_count = 0;
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue; // Skip hidden files

		char full_path[1024];
		int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
		if (path_len < 0 || path_len >= (int)sizeof(full_path)) {
			continue; // Path too long, skip
		}

		struct stat st;
		if (stat(full_path, &st) != 0)
			continue;

		if (S_ISDIR(st.st_mode)) {
			dir_count++;
		} else if (ImageBrowser_isImageFile(ent->d_name)) {
			image_count++;
		}
	}

	int count = dir_count + image_count;

	// Add parent directory entry if not at root
	bool has_parent = (strcmp(path, root) != 0);
	if (has_parent)
		count++;

	// Allocate entries
	ctx->entries = malloc(sizeof(ImageEntry) * (count > 0 ? count : 1));
	if (!ctx->entries) {
		closedir(dir);
		return;
	}

	int idx = 0;

	// Add parent directory entry
	if (has_parent) {
		strncpy(ctx->entries[idx].name, "..", sizeof(ctx->entries[idx].name) - 1);
		ctx->entries[idx].name[sizeof(ctx->entries[idx].name) - 1] = '\0';
		char* last_slash = strrchr(ctx->current_path, '/');
		if (last_slash) {
			strncpy(ctx->entries[idx].path, ctx->current_path, last_slash - ctx->current_path);
			ctx->entries[idx].path[last_slash - ctx->current_path] = '\0';
		} else {
			strncpy(ctx->entries[idx].path, root, sizeof(ctx->entries[idx].path) - 1);
			ctx->entries[idx].path[sizeof(ctx->entries[idx].path) - 1] = '\0';
		}
		ctx->entries[idx].is_dir = true;
		ctx->entries[idx].size_bytes = 0;
		idx++;
	}

	// Second pass: fill entries
	rewinddir(dir);
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char full_path[1024];
		int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
		if (path_len < 0 || path_len >= (int)sizeof(full_path)) {
			continue;
		}

		struct stat st;
		if (stat(full_path, &st) != 0)
			continue;

		bool is_dir = S_ISDIR(st.st_mode);

		if (!is_dir && !ImageBrowser_isImageFile(ent->d_name))
			continue;

		strncpy(ctx->entries[idx].name, ent->d_name, sizeof(ctx->entries[idx].name) - 1);
		ctx->entries[idx].name[sizeof(ctx->entries[idx].name) - 1] = '\0';
		strncpy(ctx->entries[idx].path, full_path, sizeof(ctx->entries[idx].path) - 1);
		ctx->entries[idx].path[sizeof(ctx->entries[idx].path) - 1] = '\0';
		ctx->entries[idx].is_dir = is_dir;
		ctx->entries[idx].size_bytes = is_dir ? 0 : st.st_size;
		idx++;
	}

	closedir(dir);

	// Sort entries (keep ".." at top if present)
	int sort_start = has_parent ? 1 : 0;
	if (idx > sort_start + 1) {
		qsort(&ctx->entries[sort_start], idx - sort_start,
			  sizeof(ImageEntry), compare_entries);
	}

	ctx->entry_count = idx;
}

// Get display name for file (without extension)
void ImageBrowser_getDisplayName(const char* filename, char* out, int max_len) {
	strncpy(out, filename, max_len - 1);
	out[max_len - 1] = '\0';

	// Remove extension for image files
	char* dot = strrchr(out, '.');
	if (dot && dot != out) {
		*dot = '\0';
	}
}
