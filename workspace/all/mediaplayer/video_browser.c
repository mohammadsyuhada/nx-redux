#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "vp_defines.h"
#include "api.h"
#include "video_browser.h"

// Case-insensitive extension check helper
static int ext_match(const char* ext, const char* target) {
	return strcasecmp(ext, target) == 0;
}

// Detect video format from filename extension
VideoFormat VideoBrowser_detectFormat(const char* filename) {
	if (!filename)
		return VIDEO_FORMAT_UNKNOWN;

	const char* dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return VIDEO_FORMAT_UNKNOWN;

	const char* ext = dot + 1;

	if (ext_match(ext, VIDEO_EXT_MP4))
		return VIDEO_FORMAT_MP4;
	if (ext_match(ext, VIDEO_EXT_MKV))
		return VIDEO_FORMAT_MKV;
	if (ext_match(ext, VIDEO_EXT_AVI))
		return VIDEO_FORMAT_AVI;
	if (ext_match(ext, VIDEO_EXT_WEBM))
		return VIDEO_FORMAT_WEBM;
	if (ext_match(ext, VIDEO_EXT_MOV))
		return VIDEO_FORMAT_MOV;
	if (ext_match(ext, VIDEO_EXT_TS))
		return VIDEO_FORMAT_TS;
	if (ext_match(ext, VIDEO_EXT_FLV))
		return VIDEO_FORMAT_FLV;
	if (ext_match(ext, VIDEO_EXT_M4V))
		return VIDEO_FORMAT_M4V;
	if (ext_match(ext, VIDEO_EXT_WMV))
		return VIDEO_FORMAT_WMV;
	if (ext_match(ext, VIDEO_EXT_MPG))
		return VIDEO_FORMAT_MPG;
	if (ext_match(ext, VIDEO_EXT_MPEG))
		return VIDEO_FORMAT_MPG;
	if (ext_match(ext, VIDEO_EXT_3GP))
		return VIDEO_FORMAT_3GP;

	return VIDEO_FORMAT_UNKNOWN;
}

// Check if file is a supported video format
bool VideoBrowser_isVideoFile(const char* filename) {
	return VideoBrowser_detectFormat(filename) != VIDEO_FORMAT_UNKNOWN;
}

// Free browser entries
void VideoBrowser_freeEntries(VideoBrowserContext* ctx) {
	if (ctx->entries) {
		free(ctx->entries);
		ctx->entries = NULL;
	}
	ctx->entry_count = 0;
}

// Compare function for sorting entries (directories first, then alphabetical)
static int compare_entries(const void* a, const void* b) {
	const VideoFileEntry* ea = (const VideoFileEntry*)a;
	const VideoFileEntry* eb = (const VideoFileEntry*)b;

	// Directories come first
	if (ea->is_dir && !eb->is_dir)
		return -1;
	if (!ea->is_dir && eb->is_dir)
		return 1;

	// Alphabetical (case-insensitive)
	return strcasecmp(ea->name, eb->name);
}

// Load directory contents (video files + directories)
void VideoBrowser_loadDirectory(VideoBrowserContext* ctx, const char* path, const char* root) {
	VideoBrowser_freeEntries(ctx);

	strncpy(ctx->current_path, path, sizeof(ctx->current_path) - 1);
	ctx->current_path[sizeof(ctx->current_path) - 1] = '\0';
	ctx->selected = 0;
	ctx->scroll_offset = 0;

	// Create video folder if it doesn't exist and we're at root
	if (strcmp(path, root) == 0) {
		mkdir(path, 0755);
	}

	DIR* dir = opendir(path);
	if (!dir) {
		LOG_error("Failed to open directory: %s\n", path);
		return;
	}

	// First pass: count entries
	int dir_count = 0;
	int video_count = 0;
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
		} else if (VideoBrowser_isVideoFile(ent->d_name)) {
			video_count++;
		}
	}

	int count = dir_count + video_count;

	// Add parent directory entry if not at root
	bool has_parent = (strcmp(path, root) != 0);
	if (has_parent)
		count++;

	// Allocate entries
	ctx->entries = malloc(sizeof(VideoFileEntry) * (count > 0 ? count : 1));
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
		ctx->entries[idx].format = VIDEO_FORMAT_UNKNOWN;
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
		VideoFormat fmt = VIDEO_FORMAT_UNKNOWN;

		if (!is_dir) {
			fmt = VideoBrowser_detectFormat(ent->d_name);
			if (fmt == VIDEO_FORMAT_UNKNOWN)
				continue;
		}

		strncpy(ctx->entries[idx].name, ent->d_name, sizeof(ctx->entries[idx].name) - 1);
		ctx->entries[idx].name[sizeof(ctx->entries[idx].name) - 1] = '\0';
		strncpy(ctx->entries[idx].path, full_path, sizeof(ctx->entries[idx].path) - 1);
		ctx->entries[idx].path[sizeof(ctx->entries[idx].path) - 1] = '\0';
		ctx->entries[idx].is_dir = is_dir;
		ctx->entries[idx].format = fmt;
		idx++;
	}

	closedir(dir);

	// Sort entries (keep ".." at top if present)
	int sort_start = has_parent ? 1 : 0;
	if (idx > sort_start + 1) {
		qsort(&ctx->entries[sort_start], idx - sort_start,
			  sizeof(VideoFileEntry), compare_entries);
	}

	ctx->entry_count = idx;
}

// Get display name for file (without extension)
void VideoBrowser_getDisplayName(const char* filename, char* out, int max_len) {
	strncpy(out, filename, max_len - 1);
	out[max_len - 1] = '\0';

	// Remove extension for video files
	char* dot = strrchr(out, '.');
	if (dot && dot != out) {
		*dot = '\0';
	}
}

// Subtitle extensions used by both single and multi-subtitle discovery
static const char* sub_exts[] = {
	SUB_EXT_SRT,
	SUB_EXT_ASS,
	SUB_EXT_SSA,
	SUB_EXT_SUB,
	NULL};

// Check if a filename extension is a subtitle extension
static bool is_subtitle_ext(const char* ext) {
	for (int i = 0; sub_exts[i] != NULL; i++) {
		if (strcasecmp(ext, sub_exts[i]) == 0)
			return true;
	}
	return false;
}

// Find all subtitle files matching a video.
// First pass: exact base name matches (movie.srt, movie.ass, etc.)
// Second pass: scan directory for language-tagged files (movie.en.srt, movie.ja.ass, etc.)
void VideoBrowser_findSubtitles(const char* video_path, SubtitleList* list) {
	list->count = 0;
	if (!video_path)
		return;

	// Extract directory and base name (without extension)
	char dir_path[512];
	char base_name[256];
	strncpy(dir_path, video_path, sizeof(dir_path) - 1);
	dir_path[sizeof(dir_path) - 1] = '\0';

	char* last_slash = strrchr(dir_path, '/');
	if (!last_slash)
		return;

	// Split into directory and filename
	*last_slash = '\0';
	const char* filename = last_slash + 1;

	// Get base name without video extension
	strncpy(base_name, filename, sizeof(base_name) - 1);
	base_name[sizeof(base_name) - 1] = '\0';
	char* dot = strrchr(base_name, '.');
	if (dot)
		*dot = '\0';
	int base_len = strlen(base_name);

	// First pass: exact base name matches (movie.srt, movie.ass, etc.)
	for (int i = 0; sub_exts[i] != NULL && list->count < MAX_SUBTITLE_FILES; i++) {
		char sub_path[1024];
		int n = snprintf(sub_path, sizeof(sub_path), "%s/%s.%s", dir_path, base_name, sub_exts[i]);
		if (n < 0 || n >= (int)sizeof(sub_path))
			continue;
		if (access(sub_path, F_OK) == 0) {
			strncpy(list->entries[list->count].path, sub_path, sizeof(list->entries[0].path) - 1);
			list->entries[list->count].path[sizeof(list->entries[0].path) - 1] = '\0';
			strncpy(list->entries[list->count].label, sub_exts[i], sizeof(list->entries[0].label) - 1);
			list->entries[list->count].label[sizeof(list->entries[0].label) - 1] = '\0';
			list->count++;
		}
	}

	// Second pass: scan directory for any subtitle file starting with base name
	// Supports patterns like: movie.en.srt, movie.ja.ass, movie(Malay).srt, movie_english.sub
	DIR* dir = opendir(dir_path);
	if (!dir)
		return;

	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL && list->count < MAX_SUBTITLE_FILES) {
		if (ent->d_name[0] == '.')
			continue;

		// Must start with base name (case-insensitive)
		if (strncasecmp(ent->d_name, base_name, base_len) != 0)
			continue;

		// The character after base name must not be alphanumeric
		// (prevents "movieExtra.srt" matching "movie")
		char after = ent->d_name[base_len];
		if (after == '\0')
			continue; // Just the base name, no extension
		if (isalnum((unsigned char)after))
			continue;

		// Get the subtitle extension
		const char* sub_dot = strrchr(ent->d_name, '.');
		if (!sub_dot || sub_dot == ent->d_name)
			continue;
		if (!is_subtitle_ext(sub_dot + 1))
			continue;

		// Build full path
		char sub_path[1024];
		int n = snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, ent->d_name);
		if (n < 0 || n >= (int)sizeof(sub_path))
			continue;

		// Check for duplicates (exact matches found in first pass)
		bool dup = false;
		for (int j = 0; j < list->count; j++) {
			if (strcmp(list->entries[j].path, sub_path) == 0) {
				dup = true;
				break;
			}
		}
		if (dup)
			continue;

		// Extract label from the part between base name and subtitle extension
		// e.g. "movie.en.srt" -> "en", "movie(Malay).srt" -> "Malay", "movie.bm.srt" -> "bm"
		const char* middle_start = ent->d_name + base_len;
		int middle_len = sub_dot - middle_start;
		char label[32];

		if (middle_len > 0) {
			// Strip leading separators (.  _  -  ( ) from label
			const char* lbl_start = middle_start;
			const char* lbl_end = sub_dot;

			// Skip leading punctuation/separators
			while (lbl_start < lbl_end && !isalnum((unsigned char)*lbl_start))
				lbl_start++;
			// Skip trailing punctuation/separators
			while (lbl_end > lbl_start && !isalnum((unsigned char)*(lbl_end - 1)))
				lbl_end--;

			int lbl_len = lbl_end - lbl_start;
			if (lbl_len <= 0) {
				// No meaningful label, use the subtitle extension
				strncpy(label, sub_dot + 1, sizeof(label) - 1);
				label[sizeof(label) - 1] = '\0';
			} else {
				int copy_len = lbl_len < (int)(sizeof(label) - 1) ? lbl_len : (int)(sizeof(label) - 1);
				strncpy(label, lbl_start, copy_len);
				label[copy_len] = '\0';
			}
		} else {
			strncpy(label, sub_dot + 1, sizeof(label) - 1);
			label[sizeof(label) - 1] = '\0';
		}

		strncpy(list->entries[list->count].path, sub_path, sizeof(list->entries[0].path) - 1);
		list->entries[list->count].path[sizeof(list->entries[0].path) - 1] = '\0';
		strncpy(list->entries[list->count].label, label, sizeof(list->entries[0].label) - 1);
		list->entries[list->count].label[sizeof(list->entries[0].label) - 1] = '\0';
		list->count++;
	}

	closedir(dir);
}

// Detect if a video file uses HEVC/H.265 codec by scanning the container header.
// MKV stores "V_MPEGH/ISO/HEVC" as ASCII; MP4 uses "hev1"/"hvc1" FourCC tags.
// Reads up to 256KB — BluRay rips can have large metadata (chapters, cover art,
// tags) before the track entries, so 16KB isn't always enough.
#define HEVC_PROBE_SIZE (256 * 1024)
bool VideoBrowser_isHEVC(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;

	unsigned char* buf = malloc(HEVC_PROBE_SIZE);
	if (!buf) {
		fclose(f);
		return false;
	}

	size_t n = fread(buf, 1, HEVC_PROBE_SIZE, f);
	fclose(f);

	bool found = false;
	for (size_t i = 0; i + 4 <= n; i++) {
		// MKV: "V_MPEGH/ISO/HEVC" — check for "HEVC" substring
		if (buf[i] == 'H' && buf[i + 1] == 'E' && buf[i + 2] == 'V' && buf[i + 3] == 'C') {
			found = true;
			break;
		}
		// MP4: "hev1" or "hvc1" FourCC
		if (buf[i] == 'h' && buf[i + 1] == 'e' && buf[i + 2] == 'v' && buf[i + 3] == '1') {
			found = true;
			break;
		}
		if (buf[i] == 'h' && buf[i + 1] == 'v' && buf[i + 2] == 'c' && buf[i + 3] == '1') {
			found = true;
			break;
		}
	}
	free(buf);
	return found;
}
