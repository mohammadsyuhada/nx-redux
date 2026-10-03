#ifndef HOME_STRIP_H
#define HOME_STRIP_H

// Home's stats strip (docs/home-b2.md, The stats strip): its lines as runs of text, each in one tone. SDL-free,
// host-tested by common/tests/test_home_strip.c; home.c measures and draws them.

#include <stdbool.h>
#include <stddef.h>

typedef enum { STRIP_GREY,			  // labels and plain words (the hint grey)
			   STRIP_WHITE,			  // values
			   STRIP_DOT } StripTone; // the middle dot, dimmer, 0.28 em each side
typedef struct {
	StripTone tone;
	char text[160];
	float pad_l, pad_r; // em
	bool gives_way;		// the one run cut with "…" when the line is too long (line 2's title)
} StripRun;
typedef struct {
	int n;
	StripRun runs[8];
} StripLine;

typedef struct {
	bool fresh;		// nothing played, nothing pinned: no strip
	bool ready;		// the month is computed
	bool signed_in; // RetroAchievements
	int total;		// seconds played this month
	int unlocks;	// achievements this month
	const char* top_title;
	int top_seconds;
} StripInput;

// The strip's line count: 0 (fresh), 1 (nothing played this month) or 2 (also before the first computation, so nothing
// moves when the values arrive).
int HomeStrip_lineCount(const StripInput* in);
// Line 1 ("This month 12h 38m · 2 achievements") and line 2 ("Most played <title> 3h 55m"); line 2 is empty (n 0)
// with one line.
void HomeStrip_build(const StripInput* in, StripLine* l1, StripLine* l2);
// A title without its region, version and dump tags: every (…) and […] group goes, then the spaces are squeezed.
void HomeStrip_cleanTitle(const char* in, char* out, size_t size);

#endif
