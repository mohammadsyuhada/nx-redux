#ifndef GRID_LAYOUT_H
#define GRID_LAYOUT_H

#include <stdbool.h>

// grid_layout.h — Grid geometry (dp), still/sliding placement, slide offset, moves. SDL-free.
typedef struct {
	float screen_w, body_top, body_h; // body = between the header and the hint bar
	float tile_w, tile_h, gap, gutter;
	float width_mul; // tile_w over the spec shape's (Consoles and Collections 2, game lists 1.5); text scales by the spec
	int cols, n;
	bool sliding;
	int rows;		// the rows in use: a sliding grid 2; a still one 1 (n <= cols) or 2
	float rows_top; // y of the top row: the rows in use centred in the body as a block
	float frame_x0; // still: x of column 0 (frame centred); sliding: unused
} GridLayout;
void GridLayout_compute(float screen_w, float body_top, float body_h, int n, GridLayout* out);
// The same with tiles width_mul times the spec shape's width (height unchanged).
void GridLayout_computeWide(float screen_w, float body_top, float body_h, int n, float width_mul, GridLayout* out);
void GridLayout_cell(const GridLayout* g, int index, int* col, int* row); // still: row-major; sliding: column-major
int GridLayout_index(const GridLayout* g, int col, int row);			  // -1 when empty
int GridLayout_columnCount(const GridLayout* g);						  // sliding: ceil(n/2); still: cols
// Sliding: the content x-offset (dp) that anchors `col`, clamped to the ends. Still: 0.
float GridLayout_offsetFor(const GridLayout* g, int col);
// x (dp, screen) of a column's left edge for a given content offset.
float GridLayout_columnX(const GridLayout* g, int col, float offset);
// The on-screen columns +1 each side, clamped; loop `for (c = first; c <= last; c++)`. Empty (0, -1) when none.
void GridLayout_visibleColumns(const GridLayout* g, float offset, int* first, int* last);
typedef enum { GRID_DIR_UP,
			   GRID_DIR_DOWN,
			   GRID_DIR_LEFT,
			   GRID_DIR_RIGHT } GridDir;
typedef enum { GRID_MOVE_STAY,
			   GRID_MOVE_MOVED,
			   GRID_MOVE_EDGE_PREV,
			   GRID_MOVE_EDGE_NEXT } GridMove;
GridMove GridLayout_move(const GridLayout* g, int* index, GridDir dir);
// The tile is in the top row (row 0; a sliding grid's column top). True with no tiles: the main menu's UP from the top of
// a tab's content focuses the tab row.
bool GridLayout_isTopRow(const GridLayout* g, int index);
// UP on the tab row wraps here: the bottom row in the tile's column, else the last tile (still); the bottom of its
// column (sliding). A one-row grid keeps the tile. Clamps a stale index; 0 with no tiles.
int GridLayout_bottomOf(const GridLayout* g, int index);

// The main-menu tiles' selected look (sub-project 8, "Logo"): the tile stays dark with a 1.5 dp outline at 70% of the
// accent, its logo / tool icon / name in the accent; no fill. Game tiles keep their 3 dp accent ring.
#define GRID_SEL_OUTLINE_DP 1.5f
#define GRID_SEL_OUTLINE_ALPHA 179 // 70%
// "N games" on the selected tile, in the accent: Consoles 13 sp, 6 dp under the logo as drawn; Collections 14 sp, 4 dp
// under the name. Sizes × the tile scale (GridLayout_countSp); the gaps are unscaled (as the Carousel's).
#define GRID_LOGO_COUNT_SP 13.0f
#define GRID_LOGO_COUNT_GAP_DP 6.0f
#define GRID_COLL_COUNT_SP 14.0f
#define GRID_COLL_COUNT_GAP_DP 4.0f
#define GRID_COUNT_MIN_SP 10.0f // never below the caption floor
// A collection's name: full white, 20 sp × the tile scale, shrinking (whole sp) to max(0.75 × start, 1.25 × count)
// until its longest word fits (Row_collNameSp's rule), then wrapping to at most 3 lines with "…"; line height 1.15.
#define GRID_COLL_NAME_SP 20.0f
#define GRID_COLL_LINE 1.15f
#define GRID_COLL_LINES 3
// The tile scale k: tile_w / 140 (the reference tile), never above 1. Insets and text scale by it.
float GridLayout_tileK(const GridLayout* g);
// A count's size (sp) at tile scale k: max(10, spec_sp × k).
float GridLayout_countSp(float spec_sp, float tile_k);
// Consoles: the count line's top, gap under what's drawn (drawn_h) centred in the tile at tile_y..tile_y + tile_h
// (any unit, the logo stays centred).
float GridLayout_logoCountY(float tile_y, float tile_h, float drawn_h, float gap);
// Collections: a name of `lines` lines (clamped to 1..GRID_COLL_LINES) at line_h each, then gap, then the count line's
// reserved count_h, centred in a tile_h tile as one group. The line is reserved whether or not it shows, so the name
// sits at the same y plain and selected (px; tops from the tile's top).
typedef struct {
	int name_y, name_h, count_y, block_h;
} GridCollText;
GridCollText GridLayout_collText(int tile_h, int lines, int line_h, int gap, int count_h);
// The name lines that fit avail_h px over the reserved gap + count line: 1..GRID_COLL_LINES.
int GridLayout_collMaxLines(int avail_h, int line_h, int gap, int count_h);
// A console without a logo (px): its name, centred in the tile at line_h a line, takes the lines that fit inside the
// pads (1..max_lines); with a count under it (count_h > 0) also only as many as leave room for gap + the count line
// above the tile's bottom edge (edge: the selected outline's width), so the count never leaves the tile.
int GridLayout_wordMaxLines(int tile_h, int pad, int line_h, int gap, int count_h, int edge, int max_lines);
// The name's top in the tile for `lines` lines (px from the tile's top); the count's top is that + lines × line_h + gap.
int GridLayout_wordTop(int tile_h, int lines, int line_h);

#endif
