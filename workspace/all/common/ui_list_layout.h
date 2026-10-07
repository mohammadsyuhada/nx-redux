// List block geometry (LIST-LAYOUT §10.2 / §10.6): header-only, SDL-free, host-tested by
// tests/test_list_layout.c.
//
// A list is a block of whole rows with an arrow strip of equal height directly above the first row and below
// the last one (half a row on pill and rich lists). The up arrow is centred in the top strip, the down arrow in
// the bottom one, so the up arrow sits as far above the first row as the down arrow below the last. The block
// (strips included) is centred between avail_top (under the page title's letters: UI_pageTitleBandTop) and avail_bottom (the
// hint bar's icons' top), not between the boxes, which would read bottom-heavy.
#ifndef UI_LIST_LAYOUT_H
#define UI_LIST_LAYOUT_H

typedef struct {
	int top;	// y of the first row
	int rows;	// whole rows in the block (>= 0)
	int strip;	// height of each arrow strip
	int up_y;	// centre y of the strip above the first row (the up arrow's centre)
	int down_y; // centre y of the strip below the last row (the down arrow's centre)
} UIListBlock;

// The block for rows of row_h with strips of `strip` px, centred in [avail_top, avail_bottom). rows_wanted > 0
// caps the row count (a page that reserves space for something else); <= 0 means as many as fit.
static inline UIListBlock UI_listBlockStrip(int avail_top, int avail_bottom, int row_h, int rows_wanted,
											int strip) {
	UIListBlock b = {avail_top, 0, strip > 0 ? strip : 0, avail_top, avail_top};
	int avail = avail_bottom - avail_top;
	if (row_h <= 0 || avail <= 0) {
		b.up_y = b.down_y = avail_top + (avail > 0 ? avail / 2 : 0);
		return b;
	}
	int rows = (avail - 2 * b.strip) / row_h;
	if (rows < 0)
		rows = 0;
	if (rows_wanted > 0 && rows > rows_wanted)
		rows = rows_wanted;
	int block_h = rows * row_h + 2 * b.strip;
	int block_top = avail_top + (avail - block_h) / 2;
	b.rows = rows;
	b.top = block_top + b.strip;
	// both arrows strip / 2 from their row, so an odd strip still keeps them symmetric
	b.up_y = b.top - b.strip / 2;
	b.down_y = b.top + rows * row_h + b.strip / 2;
	return b;
}

// The standard block: half-row strips.
static inline UIListBlock UI_listBlock(int avail_top, int avail_bottom, int row_h, int rows_wanted) {
	return UI_listBlockStrip(avail_top, avail_bottom, row_h, rows_wanted, row_h > 0 ? row_h / 2 : 0);
}

// A main list tuned to show `rows` rows (ui_text_sizes.h MENU_LIST_ROWS): rows no taller than `pitch`, short enough that
// `rows` of them plus the two half-row strips (one row) fill avail_h; rows <= 0 (untuned) keeps the pitch.
static inline int UI_mainListRowHeight(int avail_h, int pitch, int rows) {
	if (rows <= 0 || avail_h <= 0)
		return pitch;
	int h = avail_h / (rows + 1);
	return h < pitch ? h : pitch;
}

// Rich lists (§10.6: the pitch is rounded to whole rows): the row height nearest to row_h at which a whole number
// of rows plus the two half-row strips (one row) fill avail_h exactly; at least one row. Rows drawn with fixed
// assets (the pill) keep their nominal height instead.
static inline int UI_listFitRowHeight(int avail_h, int row_h) {
	if (avail_h <= 0 || row_h <= 0)
		return row_h;
	int pitches = (avail_h + row_h / 2) / row_h; // rows + 1 (the strips)
	if (pitches < 2)
		pitches = 2;
	return avail_h / pitches;
}

// Options pages ("Label: Value" rows, UI_renderSettingsPageEx; LIST-LAYOUT §10.2): rows at 0.75 x the pill list
// pitch.
static inline int UI_optionsRowHeight(int list_pitch) {
	return list_pitch * 3 / 4;
}

// An options page's arrow strip when it scrolls: the arrow's height plus air above and below it (2 dp each), not
// the pill list's half row, so the strips don't cost an options row.
static inline int UI_optionsArrowStrip(int arrow_h, int air) {
	return arrow_h + 2 * air;
}

// An options page's block for `count` rows. Titled (untitled_top < 0): a list that fits has no strips and takes only
// its rows' height, centred in [avail_top, avail_bottom); one that scrolls has `strip` strips (UI_listBlockStrip).
// Untitled: the first row stays on untitled_top (the pill list's first row, its top gutter), the rows run down to
// avail_bottom (less the bottom strip when it scrolls), and the up strip sits in the gutter above.
static inline UIListBlock UI_optionsBlock(int avail_top, int avail_bottom, int row_h, int count, int strip,
										  int untitled_top) {
	UIListBlock b = {avail_top, 0, 0, avail_top, avail_top};
	if (row_h <= 0 || count < 0)
		return b;
	int top = untitled_top >= 0 ? untitled_top : avail_top;
	int space = avail_bottom - top;
	if (space < 0)
		space = 0;
	if (count * row_h <= space) {
		b.rows = count;
		b.top = untitled_top >= 0 ? top : top + (space - count * row_h) / 2;
		b.up_y = b.top;
		b.down_y = b.top + count * row_h;
		return b;
	}
	if (untitled_top < 0)
		return UI_listBlockStrip(avail_top, avail_bottom, row_h, 0, strip);
	b.strip = strip > 0 ? strip : 0;
	b.rows = (space - b.strip) / row_h;
	if (b.rows < 0)
		b.rows = 0;
	b.top = top;
	b.up_y = top - b.strip / 2;
	b.down_y = top + b.rows * row_h + b.strip / 2;
	return b;
}

// The description area under a settings page's down strip: desc_rows - 0.5 rows (none for desc_rows <= 1).
static inline int UI_settingsDescHeight(int row_h, int desc_rows) {
	if (desc_rows <= 1 || row_h <= 0)
		return 0;
	return row_h * (2 * desc_rows - 1) / 2;
}

// Rich rows (thumbnail rows, §10.2 / §10.6): the title is the secondary role (0.8 x the list label) capped at
// row / 2.16 so both lines fit, the second line is 0.8 x the title (the caption role, 0.64 x the label when
// uncapped). Each line's box is 1.2 x its size; the two boxes are stacked and centred in the row. Offsets are
// relative to the row's top; glyphs are centred in their box by the caller.
typedef struct {
	int title_px, second_px;   // font sizes, px
	int title_box, second_box; // line box heights, px
	int title_top, second_top; // line box tops, relative to the row top
} UIRichRowText;

static inline UIRichRowText UI_richRowText(int row_h, int label_px) {
	UIRichRowText t;
	t.title_px = (int)(label_px * 0.8f + 0.5f);
	int cap = (int)(row_h / 2.16f);
	if (t.title_px > cap)
		t.title_px = cap;
	if (t.title_px < 1)
		t.title_px = 1;
	t.second_px = (int)(t.title_px * 0.8f + 0.5f);
	if (t.second_px < 1)
		t.second_px = 1;
	t.title_box = (int)(t.title_px * 1.2f + 0.5f);
	t.second_box = (int)(t.second_px * 1.2f + 0.5f);
	t.title_top = (row_h - t.title_box - t.second_box) / 2;
	t.second_top = t.title_top + t.title_box;
	return t;
}

// Achievement detail page (§10.6): the text keeps its size and the badge takes the height left (avail_h minus
// the text column's text_h), clamped to [min_px, max_px] (40..96 dp). At min_px the page may not fit, and
// scrolls.
static inline int UI_detailBadgeSize(int avail_h, int text_h, int min_px, int max_px) {
	int s = avail_h - text_h;
	if (s > max_px)
		s = max_px;
	if (s < min_px)
		s = min_px;
	return s;
}

#endif // UI_LIST_LAYOUT_H
