#ifndef GRID_LAYOUT_H
#define GRID_LAYOUT_H

#include <stdbool.h>

// grid_layout.h — Grid geometry (dp), still/sliding placement, slide offset, moves. SDL-free.
typedef struct {
	float screen_w, body_top, body_h; // body = between the header and the hint bar
	float tile_w, tile_h, gap, gutter;
	int cols, n;
	bool sliding;
	float rows_top; // y of the top row (rows centred in the body)
	float frame_x0; // still: x of column 0 (frame centred); sliding: unused
} GridLayout;
void GridLayout_compute(float screen_w, float body_top, float body_h, int n, GridLayout* out);
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

#endif
