#ifndef __LYRICS_H__
#define __LYRICS_H__

#include <stdbool.h>

// A single timestamped lyric line
typedef struct {
	int time_ms;	// Timestamp in milliseconds
	char text[256]; // Lyric text
} LyricLine;

// Maximum number of lyric lines
#define LYRICS_MAX_LINES 512

// Fetch lyrics for a track (non-blocking, runs in background thread): lyrics
// embedded in filepath first, then the cache / LRCLIB by artist+title
void Lyrics_fetch(const char* filepath, const char* artist, const char* title, int duration_sec);

// Clear current lyrics and reset state
void Lyrics_clear(void);

// Get the current lyric line for the given playback position
// Returns pointer to lyric text, or NULL if no lyrics available/still fetching
const char* Lyrics_getCurrentLine(int position_ms);

// Get the next lyric line after the current one (call after Lyrics_getCurrentLine)
// Returns pointer to next lyric text, or NULL if no next line
const char* Lyrics_getNextLine(void);

// Get the lyric line k lines after the current one (k = 1 is the next line;
// call after Lyrics_getCurrentLine). Returns NULL past the last line.
const char* Lyrics_getLineAfter(int k);

// Index of the current lyric line (call after Lyrics_getCurrentLine), -1 when
// before the first line or no lyrics
int Lyrics_getCurrentIndex(void);

// Get total size of lyrics cache on disk (in bytes)
long Lyrics_getCacheSize(void);

// Clear all cached lyrics files from disk
void Lyrics_clearCache(void);

#endif
