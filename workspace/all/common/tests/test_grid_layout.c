#include "../../nextui/grid_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define BAR (28.0f * 42.0f / 30.0f)
static int near(float a, float b) {
	return fabsf(a - b) < 0.1f;
}
static void make(float w, float h, int n, GridLayout* g) {
	GridLayout_compute(w, BAR, h - 2 * BAR, n, g);
}

static void device_vectors(void) {
	GridLayout g;
	make(478, 358, 20, &g); // Brick 3x
	assert(near(g.tile_h, (358 - 2 * BAR - 22) / 2) && near(g.tile_w, g.tile_h * 140 / 154) && g.cols == 3);
	make(896, 504, 20, &g); // SPS 2x: capped
	assert(near(g.tile_h, 154) && near(g.tile_w, 140) && g.cols == 5);
	make(717, 538, 20, &g); // Brick 2x
	assert(near(g.tile_h, 154) && g.cols == 4);
	assert(near(g.rows_top, BAR + ((538 - 2 * BAR) - (2 * 154 + 14)) / 2)); // centred
}

static void still_placement(void) {
	GridLayout g;
	int c, r;
	make(896, 504, 7, &g); // 5 cols: still (7 <= 10), row-major
	assert(!g.sliding);
	GridLayout_cell(&g, 4, &c, &r);
	assert(c == 4 && r == 0);
	GridLayout_cell(&g, 5, &c, &r);
	assert(c == 0 && r == 1);
	make(896, 504, 2, &g); // short row starts in the left column of the centred frame
	assert(near(g.frame_x0, (896 - (5 * 140 + 4 * 14)) / 2));
	make(896, 504, 10, &g);
	assert(!g.sliding);
	make(896, 504, 11, &g);
	assert(g.sliding);
}

static void sliding_clamps_and_last_column(void) {
	GridLayout g;
	int c, r;
	make(896, 504, 11, &g); // 6 columns, the last holds one tile
	assert(GridLayout_columnCount(&g) == 6);
	GridLayout_cell(&g, 3, &c, &r);
	assert(c == 1 && r == 1);
	assert(GridLayout_index(&g, 5, 1) == -1);
	assert(near(GridLayout_columnX(&g, 0, GridLayout_offsetFor(&g, 0)), 24)); // first column on the gutter
	float off = GridLayout_offsetFor(&g, 5);
	assert(near(GridLayout_columnX(&g, 5, off) + 140, 896 - 24)); // last column on the far gutter
	int i = 10;													  // the lone tile
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_STAY && i == 10);
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_EDGE_NEXT);
	i = 9; // bottom of column 4 → RIGHT → column 5 has only a top: go there
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_MOVED && i == 10);
	make(896, 504, 40, &g); // 20 columns: a middle column sits on the anchor (6 columns would clamp it)
	float mid = GridLayout_offsetFor(&g, 3);
	assert(near(GridLayout_columnX(&g, 3, mid), 24 + 0.55f * 140 + 14));
}

static void still_moves(void) {
	GridLayout g;
	make(896, 504, 7, &g);
	int i = 0;
	assert(GridLayout_move(&g, &i, GRID_DIR_LEFT) == GRID_MOVE_EDGE_PREV);
	i = 4;
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_MOVED && i == 5); // row order
	i = 1;
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_MOVED && i == 6);
	i = 3; // below col 3 is empty → the last tile
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_MOVED && i == 6);
	i = 6;
	assert(GridLayout_move(&g, &i, GRID_DIR_UP) == GRID_MOVE_MOVED && i == 1); // wrap within the column
	assert(GridLayout_move(&g, &i, GRID_DIR_UP) == GRID_MOVE_MOVED && i == 6);
	i = 6;
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_EDGE_NEXT);
}

static void visible_range(void) {
	GridLayout g;
	make(896, 504, 400, &g); // 200 columns
	int first, last;
	GridLayout_visibleColumns(&g, GridLayout_offsetFor(&g, 100), &first, &last);
	assert(first >= 97 && first <= 99 && last >= 105 && last <= 107); // on screen 99..105, +1 each side
}

static void empty(void) {
	GridLayout g;
	make(478, 358, 0, &g);
	int i = 0;
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_EDGE_NEXT); // nothing to move to: edge
	int first, last;
	GridLayout_visibleColumns(&g, 0, &first, &last); // a still frame: its columns exist but hold nothing
	for (int c = first; c <= last; c++)
		assert(GridLayout_index(&g, c, 0) == -1 && GridLayout_index(&g, c, 1) == -1);
	// columns all scrolled off screen: an empty range too, never the whole set
	make(478, 358, 40, &g);
	GridLayout_visibleColumns(&g, 100000.0f, &first, &last);
	assert(first == 0 && last == -1);
}

// The main menu's tab-row focus: UP from the top row enters it; UP on it wraps to the bottom.
static void tab_focus_rows(void) {
	GridLayout g;
	make(896, 504, 7, &g); // still, 5 cols: 0-4 on top, 5-6 under
	assert(GridLayout_isTopRow(&g, 0) && GridLayout_isTopRow(&g, 4));
	assert(!GridLayout_isTopRow(&g, 5) && !GridLayout_isTopRow(&g, 6));
	assert(GridLayout_bottomOf(&g, 1) == 6); // under col 1 is 6
	assert(GridLayout_bottomOf(&g, 0) == 5);
	assert(GridLayout_bottomOf(&g, 3) == 6); // empty under col 3: the last tile
	assert(GridLayout_bottomOf(&g, 6) == 6);
	make(896, 504, 3, &g); // still, one row: the bottom is the top
	assert(GridLayout_isTopRow(&g, 2) && GridLayout_bottomOf(&g, 2) == 2 && GridLayout_bottomOf(&g, 0) == 0);
	make(896, 504, 11, &g); // sliding, column-major, the last column holds one tile (10)
	assert(GridLayout_isTopRow(&g, 0) && GridLayout_isTopRow(&g, 4) && !GridLayout_isTopRow(&g, 5));
	assert(GridLayout_bottomOf(&g, 4) == 5 && GridLayout_bottomOf(&g, 0) == 1);
	assert(GridLayout_bottomOf(&g, 10) == 10 && GridLayout_isTopRow(&g, 10));
	assert(GridLayout_bottomOf(&g, 99) == 10); // a stale index clamps
	make(478, 358, 0, &g);					   // no tiles
	assert(GridLayout_isTopRow(&g, 0) && GridLayout_bottomOf(&g, 0) == 0);
}

int main(void) {
	device_vectors();
	tab_focus_rows();
	still_placement();
	sliding_clamps_and_last_column();
	still_moves();
	visible_range();
	empty();
	printf("test_grid_layout: ok\n");
	return 0;
}
