#ifndef CAPTION_FIT_H
#define CAPTION_FIT_H

#include <stdbool.h>

#include "gameinfo_text.h"
#include "infoband_layout.h" // InfoBand_measureFn

// caption_fit.h — the long "Next:" split in game-list captions (LIST-LAYOUT §8c.4): the Carousel and the Backdrop,
// Horizontal and Vertical. SDL-free; the caller measures text (px) through `measure`.
//
// A row that ends in "Next: …" (its last segment INFO_SEG_NEXT) and is too wide for the caption on one line splits in
// two: line 1 is the row without Next ("🏆 n of m" in the Backdrop's trophy row, "time · 🏆 n of m" in the Carousel's
// info line), then "Next: …" on its own lines, up to CAPTION_FIT_NEXT_LINES, the last ellipsized ("..."). The row is
// measured as drawn: its texts, the separators and the inline trophy with its gap (trophy_w, on the INFO_SEG_ACH
// segment). Reserves are unchanged: Next may only use `free_lines`, the whole free lines the caller has below the
// caption's reserve (none: no split, the row is drawn and cut as before). Rows that fit, rows without Next and a row
// with nothing before Next are not split. The Game Switcher, Home's Continue card, the List band and Grid tiles don't
// use it.

#define CAPTION_FIT_NEXT_LINES 2

typedef struct {
	int head_n;		// the segments drawn on the row's own line: all of them when not split, else all but Next
	int next_lines; // 0: not split; else Next's own lines in next[0 .. next_lines)
	char next[CAPTION_FIT_NEXT_LINES][sizeof(((InfoSeg*)0)->text)];
} CaptionFitRow;

// The row's width as drawn: the texts, a separator (sep_w) between segments, trophy_w on the INFO_SEG_ACH segment.
int CaptionFit_rowWidth(const InfoSeg* segs, int n, int sep_w, int trophy_w, InfoBand_measureFn measure, void* ctx);

// Split the row (segs[0 .. n)) for a caption avail px wide with free_lines whole free lines. Next's text wraps at
// spaces (a word wider than the line breaks between code points) onto min(free_lines, CAPTION_FIT_NEXT_LINES) lines;
// what's left after the last one is cut with "..." (Next keeps its "Next: " and one title character at least).
// Returns out->next_lines > 0 when it split.
bool CaptionFit_split(const InfoSeg* segs, int n, int avail, int sep_w, int trophy_w, int free_lines,
					  InfoBand_measureFn measure, void* ctx, CaptionFitRow* out);

#endif
