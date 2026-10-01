#include "../../nextui/grid_layout.h"
#include "../../nextui/row_model.h"
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

// The two screens in dp (px / (FIXED_SCALE × 30/42)): the Brick 1024×768 at 3, the Smart Pro S 1280×720 at 2.
#define BRICK_W (1024.0f / (3 * 30.0f / 42.0f))
#define BRICK_H (768.0f / (3 * 30.0f / 42.0f))
#define SPS_W (1280.0f / (2 * 30.0f / 42.0f))
#define SPS_H (720.0f / (2 * 30.0f / 42.0f))

static float rowsCentre(const GridLayout* g) {
	return g->rows_top + (g->rows * g->tile_h + (g->rows - 1) * g->gap) / 2;
}

// Sub-project 8: a still grid centres the rows it uses; a sliding grid keeps both rows as before. The same geometry
// serves the main menu and the game lists (GridView_render computes every Grid through GridLayout_compute).
static void still_centring(void) {
	GridLayout g;
	float brick_mid = BAR + (BRICK_H - 2 * BAR) / 2, sps_mid = BAR + (SPS_H - 2 * BAR) / 2; // 179.2, 252.0
	// Brick: tile 117.3 × 129, 3 columns
	make(BRICK_W, BRICK_H, 3, &g); // one full row (3 tools)
	assert(g.cols == 3 && !g.sliding && g.rows == 1);
	assert(near(g.tile_h, 129) && near(g.rows_top, 114.7f) && near(rowsCentre(&g), brick_mid));
	make(BRICK_W, BRICK_H, 1, &g); // one tile
	assert(g.rows == 1 && near(g.rows_top, 114.7f));
	make(BRICK_W, BRICK_H, 4, &g); // 4 tools: 3 + 1, the two rows centred as a block
	assert(!g.sliding && g.rows == 2 && near(g.rows_top, 43.2f) && near(rowsCentre(&g), brick_mid));
	make(BRICK_W, BRICK_H, 7, &g); // sliding: unchanged (both rows, ring room above)
	assert(g.sliding && g.rows == 2 && near(g.rows_top, BAR + 4) && near(g.rows_top, 43.2f));
	// SPS: tile 140 × 154, 5 columns
	make(SPS_W, SPS_H, 4, &g); // 4 collections: one row at the body's centre
	assert(g.cols == 5 && g.rows == 1 && near(g.rows_top, 175.0f) && near(rowsCentre(&g), sps_mid));
	make(SPS_W, SPS_H, 5, &g);
	assert(g.rows == 1 && near(g.rows_top, 175.0f));
	make(SPS_W, SPS_H, 7, &g); // 5 + 2
	assert(!g.sliding && g.rows == 2 && near(g.rows_top, 91.0f) && near(rowsCentre(&g), sps_mid));
	make(SPS_W, SPS_H, 11, &g); // sliding: as before
	assert(g.sliding && g.rows == 2 && near(g.rows_top, 91.0f));
	make(SPS_W, SPS_H, 40, &g);
	assert(g.sliding && near(g.rows_top, 91.0f));
}

// The selected tile's "N games" and a collection's name, at the Grid tile scale k (tile_w / 140).
static void counts_and_names(void) {
	GridLayout g;
	make(BRICK_W, BRICK_H, 9, &g);
	float kb = GridLayout_tileK(&g);
	assert(fabsf(kb - 117.2727f / 140) < 0.001f); // 0.838
	make(SPS_W, SPS_H, 9, &g);
	float ks = GridLayout_tileK(&g);
	assert(near(ks, 1.0f));
	make(717, 538, 9, &g); // capped tile: never above 1
	assert(near(GridLayout_tileK(&g), 1.0f));
	// Consoles 13 sp, Collections 14 sp, × k, floored at 10 sp
	assert(fabsf(GridLayout_countSp(GRID_LOGO_COUNT_SP, kb) - 10.89f) < 0.01f);
	assert(fabsf(GridLayout_countSp(GRID_COLL_COUNT_SP, kb) - 11.73f) < 0.01f);
	assert(near(GridLayout_countSp(GRID_LOGO_COUNT_SP, ks), 13) && near(GridLayout_countSp(GRID_COLL_COUNT_SP, ks), 14));
	assert(near(GridLayout_countSp(GRID_LOGO_COUNT_SP, 0.5f), 10)); // a small tile keeps the floor
	// the name: 20 sp × k, its floor max(0.75 × start, 1.25 × count) (Row_collNameSp's rule)
	float sb = GRID_COLL_NAME_SP * kb, ss = GRID_COLL_NAME_SP * ks; // 16.75, 20
	float cb = GridLayout_countSp(GRID_COLL_COUNT_SP, kb), cs = GridLayout_countSp(GRID_COLL_COUNT_SP, ks);
	assert(fabsf(Row_collNameFloor(sb, cb) - 14.66f) < 0.01f && near(Row_collNameFloor(ss, cs), 17.5f));
	assert(near(Row_collNameSp(sb, 150, 200, cb), sb));				  // a word that fits keeps the start
	assert(near(Row_collNameSp(ss, 180, 170, cs), 18.0f));			  // 20 → 19 (171 px) → 18 (162 px)
	assert(fabsf(Row_collNameSp(sb, 400, 200, cb) - 14.66f) < 0.01f); // too wide: the floor, then it wraps
	assert(Row_collNameSp(sb, 400, 200, cb) >= 1.25f * cb - 0.001f);  // the name never reads smaller than its count
	// Consoles: the count's top 6 dp under the logo as drawn, the logo centred in the tile (Brick, dp)
	assert(near(GridLayout_logoCountY(43.2f, 129, 20, 6), 43.2f + 64.5f + 10 + 6));
	assert(GridLayout_logoCountY(0, 129, 40, 6) > GridLayout_logoCountY(0, 129, 20, 6));
}

// A collection tile's text block (px): name lines + 4 dp + the reserved count line, centred in the tile.
static void collection_text(void) {
	// Brick: tile 276 px, name 43 px (16.75 sp) step 49, gap 9, count 30 px (11.73 sp), pad 25 px
	assert(GridLayout_collMaxLines(276 - 2 * 25, 49, 9, 30) == 3);
	GridCollText t = GridLayout_collText(276, 2, 49, 9, 30);
	assert(t.name_y == 69 && t.name_h == 98 && t.count_y == 176 && t.block_h == 137);
	t = GridLayout_collText(276, 1, 49, 9, 30);
	assert(t.name_y == 94 && t.count_y == 152);
	// SPS: tile 220 px, name 34 px (20 sp) step 39, gap 6, count 24 px (14 sp), pad 20 px
	assert(GridLayout_collMaxLines(220 - 2 * 20, 39, 6, 24) == 3);
	t = GridLayout_collText(220, 3, 39, 6, 24);
	assert(t.name_y == 36 && t.name_h == 117 && t.count_y == 159 && t.block_h == 147);
	t = GridLayout_collText(220, 5, 39, 6, 24); // clamps to 3 lines
	assert(t.name_h == 117);
	t = GridLayout_collText(220, 1, 39, 6, 24);
	assert(t.name_y == 75 && t.count_y == 120);
	// the reserved line keeps the group inside the tile; a cramped tile still shows one line
	assert(t.name_y >= 0 && t.count_y + 24 <= 220);
	assert(GridLayout_collMaxLines(40, 39, 6, 24) == 1);
}

// A game list's Grid centres in the body under the game-list header (GameList listTopAt(false): the strip
// SCALE1(BUTTON_SIZE + 2 × BUTTON_MARGIN) = 28 logical px, in dp at each device scale), not only under BAR/BAR: the
// rows' centre follows body_top + body_h / 2 for any body, symmetric or not.
static void game_list_centring(void) {
	GridLayout g;
	const int scales[2] = {3, 2};
	const float sw[2] = {BRICK_W, SPS_W}, sh[2] = {BRICK_H, SPS_H};
	for (int i = 0; i < 2; i++) {
		float px_per_dp = scales[i] * 30.0f / 42.0f;
		float header = (16 + 2 * 6) * scales[i] / px_per_dp; // the game-list header strip, dp
		assert(near(header, BAR));
		float body_h = sh[i] - header - BAR; // header above, hint bar below
		make(sw[i], sh[i], 2, &g);
		GridLayout h;
		GridLayout_compute(sw[i], header, body_h, 2, &h); // one still row of two games
		assert(h.rows == 1 && near(rowsCentre(&h), header + body_h / 2) && near(h.rows_top, g.rows_top));
		GridLayout_compute(sw[i], header, body_h, h.cols + 1, &h); // two still rows
		assert(h.rows == 2 && near(rowsCentre(&h), header + body_h / 2));
		// an asymmetric body (header 39.2 dp on top, 60 dp off the bottom): still centred on its own middle
		float abody = sh[i] - header - 60;
		GridLayout_compute(sw[i], header, abody, 2, &h);
		assert(h.rows == 1 && near(rowsCentre(&h), header + abody / 2));
		assert(h.rows_top >= header + 4 - 0.01f); // the ring room under the header holds
	}
}

// The selected Consoles tile's "N games" stays inside the tile, clear of the 1.5 dp outline, at both scales: under a
// logo (the logo box's tallest fit) and under a logo-less name (4 lines unless the count needs the room) (px).
static void count_fits_tile(void) {
	struct {
		int tile_h, pad, word_h, gap, count_h, edge, inset_y;
	} dev[2] = {
		// Brick 3x: tile 129 dp = 276 px, k 0.838: pad 14 dp×k = 25, 17 sp×k = 37 px font (height 50), gap 6 dp = 13,
		// count 10.89 sp = 28 px font (height 38), outline 1.5 dp = 3, logo inset 30 dp×k = 54
		{276, 25, 50, 13, 38, 3, 54},
		// SPS 2x: tile 154 dp = 220 px, k 1: pad 20, 17 sp = 29 px font (height 39), gap 9, 13 sp = 22 px (height 30),
		// outline 2, inset 43
		{220, 20, 39, 9, 30, 2, 43},
	};
	for (int i = 0; i < 2; i++) {
		int th = dev[i].tile_h, lh = dev[i].word_h, gap = dev[i].gap, ch = dev[i].count_h, edge = dev[i].edge;
		// the logo case: the tallest logo the box allows, centred; the count 6 dp under it
		int box_h = th - 2 * dev[i].inset_y;
		int cy = (int)floorf(GridLayout_logoCountY(0, (float)th, (float)box_h, (float)gap) + 0.5f);
		assert(cy + ch <= th - edge);
		// the no-logo case: plain (no count) keeps 4 lines; with the count the lines drop until it fits
		assert(GridLayout_wordMaxLines(th, dev[i].pad, lh, gap, 0, edge, 4) == 4);
		int n = GridLayout_wordMaxLines(th, dev[i].pad, lh, gap, ch, edge, 4);
		assert(n == 3);
		int top = GridLayout_wordTop(th, n, lh);
		int count_top = top + n * lh + gap;
		assert(top >= edge && count_top + ch <= th - edge);
		// before the cap: 4 lines put the count past the tile's edge
		int top4 = GridLayout_wordTop(th, 4, lh);
		assert(top4 + 4 * lh + gap + ch > th);
		// a short name (1 line) is never capped below 1 and fits
		assert(GridLayout_wordMaxLines(th, dev[i].pad, lh, gap, ch, edge, 1) == 1);
	}
	// a cramped tile still shows one line
	assert(GridLayout_wordMaxLines(60, 10, 50, 13, 38, 3, 4) == 1);
}

int main(void) {
	device_vectors();
	still_centring();
	counts_and_names();
	collection_text();
	game_list_centring();
	count_fits_tile();
	tab_focus_rows();
	still_placement();
	sliding_clamps_and_last_column();
	still_moves();
	visible_range();
	empty();
	printf("test_grid_layout: ok\n");
	return 0;
}
