#ifndef HOME_STATS_MODEL_H
#define HOME_STATS_MODEL_H

// SDL-free math for the Home stats strip (host-tested): this calendar month's play time, achievements and most played.

#include <stdbool.h>
#include <stddef.h>

#define HOME_DAYS 35

typedef struct {
	int group; // rom.id
	char title[128];
	long long start, end; // end <= 0 → open
} HomeSession;

typedef struct {
	int group;
	char title[128];
	int seconds;
	long long last; // latest session end in the window
} HomeTop;

typedef struct {
	bool ready;				// false until the first result
	long long window_start; // local midnight on the 1st of this month
	int today;				// index of today in days[] (0 = the 1st)
	int days[HOME_DAYS];	// seconds per local day; days[0] = the 1st (a month has at most 31)
	int total;
	int ntop;
	HomeTop top[3];
	int unlocks; // -1 = signed out
} HomeStats;

// Local midnight on the 1st of now's month (localtime_r/mktime, TZ-aware: calendar days, so a DST day is 23 or 25 h).
long long HomeStats_windowStart(long long now);
// Sessions are clipped to [window_start, now]; open sessions end at now; sessions with a
// non-positive or > 24 h length are ignored; a session is split at every local midnight it crosses.
// out->unlocks is left at -1 (the caller fills it).
void HomeStats_compute(const HomeSession* s, int n, long long now, HomeStats* out);
// What the worker's result depends on, for the on-disk cache. A file is (mtime, size); a directory is
// (mtime, entry count) since exFAT doesn't bump a directory's mtime for every change below it; missing = -1.
typedef struct {
	long long a, b;
} HomeStatsSig;

typedef struct {
	int day;				// local yyyymmdd of now: the window and today's column follow the date
	int signed_in;			// RA enabled and authenticated (unlocks are -1 otherwise)
	HomeStatsSig db;		// game_logs.sqlite
	HomeStatsSig journal;	// .ra/pending/unlocks.jsonl
	HomeStatsSig confirmed; // .ra/pending/confirmed.jsonl
	HomeStatsSig games;		// .ra/cache/games (directory)
	HomeStatsSig sessions;	// .ra/cache/sessions (directory)
} HomeStatsKey;

// Local yyyymmdd of now.
int HomeStats_localDay(long long now);
bool HomeStats_keyEqual(const HomeStatsKey* x, const HomeStatsKey* y);
// The cache file's text for key + st into buf; returns its length, 0 when buf is too small or st isn't ready.
size_t HomeStats_format(const HomeStatsKey* key, const HomeStats* st, char* buf, size_t size);
// Parses a cache file's text. False (out untouched) when it is malformed or was written for another key.
bool HomeStats_parse(const char* text, const HomeStatsKey* key, HomeStats* out);

// Heatmap cell alpha: 20 / 51 / 115 / 204 for the 8% / 20% / 45% / 80% steps (none, < 30 min, < 2 h, ≥ 2 h).
int HomeStats_shadeAlpha(int seconds);

#endif // HOME_STATS_MODEL_H
