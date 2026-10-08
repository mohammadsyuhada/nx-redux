#ifndef HOME_LIST_LAYOUT_H
#define HOME_LIST_LAYOUT_H

#include <stdbool.h>

// Home's List layout (Layouts > Home layout: List; the mockup's buildHomeList, nx-showcase.html): the stats strip on
// left-aligned lines under the tab row, then one pill list in the main menu's List style: Continue, the pinned games
// in pin order, the pinned tools. What the rows are and where the strip and the list start; the rows themselves are
// gamelist.c's List. Pure, host-tested (common/tests/test_home_list_layout.c). Units are screen px.

typedef enum { HOMELIST_CONTINUE,
			   HOMELIST_GAME,
			   HOMELIST_TOOL } HomeListKind;

typedef struct {
	HomeListKind kind;
	int ref; // the game's or the tool's index (0 for Continue)
} HomeListItem;

// The rows: Continue, then ngames pinned games (Continue's own excluded by the caller), then ntools tools.
int HomeList_count(int ngames, int ntools);
// Row i (clamped into the list) as its kind and index.
HomeListItem HomeList_item(int i, int ngames, int ntools);
// A selection kept on a row after a rebuild: the last row when the list got shorter, never below 0.
int HomeList_clampSel(int sel, int ngames, int ntools);

// What HomeList_compute takes beyond the strip's line count.
typedef struct {
	int bar;			// the tab row's height (its bottom)
	int strip_gap;		// between the tab row and the strip's first line's top (the mockup's 8 dp)
	int list_gap;		// between the strip's last line's bottom and the first row (the mockup's 12 dp)
	int list_top_plain; // the first row's top with no strip (the main menu List's: the bar + 12 dp)
	int asc, desc;		// the strip font's ascent and descent (px, desc >= 0)
	int line_step;		// between the strip's baselines
} HomeListOpts;

typedef struct {
	int strip_lines;   // 0 (fresh, or Layouts > Extra info hidden), 1 or 2
	int strip_base[2]; // the lines' baselines
	int list_top;	   // the first row's top
} HomeListGeom;

// Top to bottom: the tab row, strip_gap, the strip's lines (each line_step under the last), list_gap, the rows. No
// strip: the rows at list_top_plain, as the main menu's List.
void HomeList_compute(int strip_lines, const HomeListOpts* o, HomeListGeom* out);

// The "Continue" tag before Continue's name (the mockup's .ptag: 12 sp text on a 16 sp line, 2 x 8 dp padding, 10 dp
// before the name), from its text's height th: its padding and the gap to the name, and its whole width for a text
// text_w wide (the room the row's text gives it: the tag, then the gap).
typedef struct {
	int pad_x, pad_y, gap;
	int w, h; // the tag's own size
} HomeListTag;
HomeListTag HomeList_tag(int th, int text_w);

#endif
