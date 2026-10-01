// Grid style geometry (dp floats): tile size, still (row-major, centred frame) vs sliding (column-major,
// two per column) placement, the slide offset with its anchor and end clamps, and D-pad moves. Pure, host-tested.

#include "grid_layout.h"
#include <math.h>

#define TILE_W_CAP 140.0f
#define TILE_H_CAP 154.0f
#define GAP 14.0f
#define GUTTER 24.0f
#define RING_ROOM 4.0f
#define ANCHOR_FRAC 0.55f

static float maxf(float a, float b) {
	return a > b ? a : b;
}
static float minf(float a, float b) {
	return a < b ? a : b;
}

void GridLayout_compute(float screen_w, float body_top, float body_h, int n, GridLayout* out) {
	GridLayout* g = out;
	g->screen_w = screen_w;
	g->body_top = body_top;
	g->body_h = body_h;
	g->gap = GAP;
	g->gutter = GUTTER;
	g->n = n < 0 ? 0 : n;
	g->tile_h = maxf(1.0f, minf(TILE_H_CAP, (body_h - 2 * RING_ROOM - GAP) / 2));
	g->tile_w = g->tile_h * TILE_W_CAP / TILE_H_CAP;
	int cols = (int)floorf((screen_w - 2 * GUTTER + GAP) / (g->tile_w + GAP));
	g->cols = cols < 1 ? 1 : cols;
	g->rows_top = maxf(body_top + RING_ROOM, body_top + (body_h - (2 * g->tile_h + GAP)) / 2);
	g->sliding = g->n > 2 * g->cols;
	g->frame_x0 = g->sliding ? 0 : (screen_w - (g->cols * g->tile_w + (g->cols - 1) * GAP)) / 2;
}

void GridLayout_cell(const GridLayout* g, int index, int* col, int* row) {
	if (g->sliding) {
		*col = index / 2;
		*row = index % 2;
	} else {
		*col = index % g->cols;
		*row = index / g->cols;
	}
}

int GridLayout_index(const GridLayout* g, int col, int row) {
	if (col < 0 || row < 0 || row > 1)
		return -1;
	int i;
	if (g->sliding)
		i = col * 2 + row;
	else {
		if (col >= g->cols)
			return -1;
		i = row * g->cols + col;
	}
	return i < g->n ? i : -1;
}

int GridLayout_columnCount(const GridLayout* g) {
	return g->sliding ? (g->n + 1) / 2 : g->cols;
}

float GridLayout_offsetFor(const GridLayout* g, int col) {
	if (!g->sliding)
		return 0;
	float step = g->tile_w + GAP;
	float raw = col * step - (ANCHOR_FRAC * g->tile_w + GAP);
	int c = GridLayout_columnCount(g);
	float hi = maxf(0, (c - 1) * step + g->tile_w - (g->screen_w - 2 * GUTTER));
	return minf(hi, maxf(0, raw));
}

float GridLayout_columnX(const GridLayout* g, int col, float offset) {
	float base = g->sliding ? GUTTER : g->frame_x0;
	return base + col * (g->tile_w + GAP) - offset;
}

void GridLayout_visibleColumns(const GridLayout* g, float offset, int* first, int* last) {
	int c = GridLayout_columnCount(g);
	int f = c, l = -1;
	for (int i = 0; i < c; i++) {
		float x = GridLayout_columnX(g, i, offset);
		if (x + g->tile_w > 0 && x < g->screen_w) {
			if (i < f)
				f = i;
			l = i;
		}
	}
	if (l < 0) { // nothing on screen (or no columns): an empty range
		*first = 0;
		*last = -1;
		return;
	}
	*first = f > 0 ? f - 1 : 0;
	*last = l < c - 1 ? l + 1 : c - 1;
}

GridMove GridLayout_move(const GridLayout* g, int* index, GridDir dir) {
	int n = g->n;
	if (n <= 0) {
		if (dir == GRID_DIR_LEFT)
			return GRID_MOVE_EDGE_PREV;
		if (dir == GRID_DIR_RIGHT)
			return GRID_MOVE_EDGE_NEXT;
		return GRID_MOVE_STAY;
	}
	int i = *index;
	if (i < 0)
		i = 0;
	if (i >= n)
		i = n - 1;
	int col, row;
	GridLayout_cell(g, i, &col, &row);

	if (dir == GRID_DIR_UP || dir == GRID_DIR_DOWN) {
		int t = GridLayout_index(g, col, 1 - row);
		if (t < 0) {
			// still with two rows: the empty cell below falls back to the last tile; otherwise nothing to swap with
			if (!g->sliding && n > g->cols && row == 0)
				t = n - 1;
			else
				return GRID_MOVE_STAY;
		}
		if (t == i)
			return GRID_MOVE_STAY;
		*index = t;
		return GRID_MOVE_MOVED;
	}

	int step = dir == GRID_DIR_LEFT ? -1 : 1;
	if (!g->sliding) {
		int t = i + step;
		if (t < 0)
			return GRID_MOVE_EDGE_PREV;
		if (t >= n)
			return GRID_MOVE_EDGE_NEXT;
		*index = t;
		return GRID_MOVE_MOVED;
	}
	int tc = col + step;
	if (tc < 0)
		return GRID_MOVE_EDGE_PREV;
	if (tc >= GridLayout_columnCount(g))
		return GRID_MOVE_EDGE_NEXT;
	int t = GridLayout_index(g, tc, row);
	if (t < 0)
		t = GridLayout_index(g, tc, 0);
	*index = t;
	return GRID_MOVE_MOVED;
}

static int clampIndex(const GridLayout* g, int index) {
	if (index >= g->n)
		index = g->n - 1;
	return index < 0 ? 0 : index;
}

bool GridLayout_isTopRow(const GridLayout* g, int index) {
	if (g->n <= 0)
		return true;
	int col, row;
	GridLayout_cell(g, clampIndex(g, index), &col, &row);
	return row == 0;
}

int GridLayout_bottomOf(const GridLayout* g, int index) {
	if (g->n <= 0)
		return 0;
	int i = clampIndex(g, index);
	int col, row;
	GridLayout_cell(g, i, &col, &row);
	int t = GridLayout_index(g, col, 1);
	if (t >= 0)
		return t;
	// still with two rows: the empty cell under a short bottom row falls back to the last tile
	if (!g->sliding && g->n > g->cols)
		return g->n - 1;
	return i;
}
