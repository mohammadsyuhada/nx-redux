#ifndef GAMEINFO_TEXT_H
#define GAMEINFO_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// "Today", "Yesterday", "3 days ago", "Last week", "2 weeks ago", "Last month", "4 months ago", "Last year",
// "2 years ago": whole 24 h spans since `then`; a future `then` is Today.
void GameInfo_whenText(time_t now, time_t then, char* out, size_t size);
// "1h 5m" / "3m 12s" / "45s"; "" when seconds < 0.
void GameInfo_durationText(int seconds, char* out, size_t size);

typedef enum { INFO_SEG_TIME,
			   INFO_SEG_ACH,
			   INFO_SEG_NEXT,
			   INFO_SEG_COUNT } InfoSegKind;
typedef struct {
	InfoSegKind kind;
	char text[160];
} InfoSeg;

// The game's segments in order (time, "n of m", "Next: …"), each left out without data:
// last <= 0 and seconds <= 0 → no time segment; total <= 0 → no achievement segments; full=false drops NEXT.
// Returns the count (0..3).
int GameInfo_segments(time_t now, time_t last, int seconds, int unlocked, int total, const char* next, bool full,
					  InfoSeg out[3]);
// "1 game" / "N games"; "" for n < 0.
void GameInfo_gamesLabel(int n, char* out, size_t size);
// Cut `text` to at most `max_bytes` bytes without splitting a UTF-8 sequence; returns the kept byte count.
size_t GameInfo_utf8Cut(const char* text, size_t max_bytes);

#endif
