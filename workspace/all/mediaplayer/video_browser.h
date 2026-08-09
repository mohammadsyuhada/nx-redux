#ifndef __VIDEO_BROWSER_H__
#define __VIDEO_BROWSER_H__

#include <stdbool.h>
#include "vp_defines.h"

typedef struct {
	char name[256];
	char path[512];
	bool is_dir;
	VideoFormat format;
} VideoFileEntry;

typedef struct {
	char current_path[512];
	VideoFileEntry* entries;
	int entry_count;
	int selected;
	int scroll_offset;
	int items_per_page;
} VideoBrowserContext;

// Detect video format from filename extension
VideoFormat VideoBrowser_detectFormat(const char* filename);

// Check if file is a supported video format
bool VideoBrowser_isVideoFile(const char* filename);

// Free browser entries
void VideoBrowser_freeEntries(VideoBrowserContext* ctx);

// Load directory contents (video files + directories)
void VideoBrowser_loadDirectory(VideoBrowserContext* ctx, const char* path, const char* root);

// Get display name for file (without extension)
void VideoBrowser_getDisplayName(const char* filename, char* out, int max_len);

// Multi-subtitle support
#define MAX_SUBTITLE_FILES 8

typedef struct {
	char path[512];
	char label[32]; // e.g. "srt", "en", "ja"
} SubtitleEntry;

typedef struct {
	SubtitleEntry entries[MAX_SUBTITLE_FILES];
	int count;
} SubtitleList;

// Find all subtitle files matching a video (exact match + language-tagged)
void VideoBrowser_findSubtitles(const char* video_path, SubtitleList* list);

// Detect if a video file uses HEVC/H.265 codec (reads first 16KB of file header)
bool VideoBrowser_isHEVC(const char* path);

#endif
