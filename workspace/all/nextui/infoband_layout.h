#ifndef INFOBAND_LAYOUT_H
#define INFOBAND_LAYOUT_H

typedef struct {
	int list_top;	 // y of the first row
	int rows;		 // whole PILL rows that fit (>= 1)
	int band_top;	 // y where the band's fade starts
	int text_top;	 // y of the text line
	int text_h;		 // text line height
	int band_bottom; // == screen_h - bar_h (top of the hint bar)
	int arrow_x;	 // the arrows' x: the rows' text start (0 = the 14 dp list inset)
} InfoBandLayout;

// The List geometry for main-menu tabs and game lists. Rows end at the top of the text line (landscape rule):
// rows = (text_top - list_top) / row_h, floored, at least 1.
InfoBandLayout InfoBand_layout(int screen_h, int bar_h, int list_top, int row_h, int text_h, int dp2, int dp12);

#include <stddef.h>

#include "gameinfo_text.h"

// The pixel width of a NUL-terminated UTF-8 string.
typedef int (*InfoBand_measureFn)(void* ctx, const char* text);

// Cut `text` (in place; `size` is its buffer) so "<kept>..." measures <= room. The cut steps back by whole
// UTF-8 code points and trims trailing spaces before the "...", and must keep more than `min_bytes` bytes
// (min_bytes = strlen("Next: ") keeps at least one title character). Returns the cut width, or -1 with
// `text` untouched when no cut fits.
int InfoBand_cutText(char* text, size_t size, int room, size_t min_bytes, InfoBand_measureFn measure, void* ctx);

// Fit one line of segments into avail px (sep_w: one separator; trophy_w: the trophy + its gap, added to the
// ACH segment), cutting texts in place. Returns the kept count; kept segments are segs[0..n). In order:
// - the last segment is cut (whole code points, "...") when it may be: Next keeps at least one title character and
//   a plain count line one character;
// - Next that can't be cut is dropped;
// - a line of only the time and the achievement count (a tile's count-only info, or what's left of a full line):
//   the time ("Today - 18m 25s") shortens to its duration ("18m 25s"), then drops, so "n of m" stays when it fits;
// - anything else is dropped from the end.
// The time and "n of m" read wrong cut, so they are never cut.
int InfoBand_fitSegments(InfoSeg* segs, int n, int avail, int sep_w, int trophy_w, InfoBand_measureFn measure,
						 void* ctx);

#endif
