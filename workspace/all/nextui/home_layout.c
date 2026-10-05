// Home B2's geometry and D-pad (see home_layout.h). Pure, host-tested by common/tests/test_home_layout.c. The numbers
// are the mockup's (nx-mobile .local/main-menu-mockup/brick-home-stats.html, variant 3; the D-pad from
// nx-showcase.html's pressHome), in Brick px.

#include "home_layout.h"

#include <math.h>
#include <string.h>

static float minf(float a, float b) {
	return a < b ? a : b;
}

static HomeRect rect(float x, float y, float w, float h) {
	return (HomeRect){x, y, w, h};
}

static void addTop(HomeLayout* l, HomeTileKind kind, int ref, HomeRect r) {
	if (l->ntop < HOME_MAX_TOP)
		l->top[l->ntop++] = (HomeTile){kind, ref, r};
}

// The pins' rows from y: k a row, pw wide, games first..ngames−1.
static void addRows(HomeLayout* l, float y, float pw, int first, int ngames) {
	for (int g = first; g < ngames && l->npins < HOME_MAX_PINS; g++) {
		int i = l->npins, r = i / l->k, c = i % l->k;
		l->pins[l->npins++] =
			(HomeTile){HOME_TILE_GAME, g, rect(HOME_EDGE + c * (pw + HOME_GAP), y + r * (l->pin_h + HOME_GAP), pw, l->pin_h)};
	}
}

// The tool squares, sq a side: `rows` tall from y0 (vgap apart down, HOME_GAP across), the block's left edge at bx, in
// cols columns, filled a column at a time (col_first) or in reading order. Tools past the slots are left off: Pin Tool
// keeps within them (shortcuts.h, MAX_PINNED_TOOLS). Into out[] (n of them).
static void placeTools(HomeLayout* l, HomeTile* out, int* n, float bx, float y0, float sq, float vgap, int rows, int cols,
					   bool col_first, int ntools) {
	int slots = rows * cols, shown = ntools < slots ? ntools : slots;
	for (int i = 0; i < shown && *n < HOME_MAX_TOP; i++) {
		int c = col_first ? i / rows : i % cols, r = col_first ? i % rows : i / cols;
		out[(*n)++] = (HomeTile){HOME_TILE_TOOL, i, rect(bx + (float)c * (sq + HOME_GAP), y0 + (float)r * (sq + vgap), sq, sq)};
	}
}

// Columns for ntools in `rows`-tall columns: as many as they fill, 1 to HOME_TOOL_COLS.
static int toolCols(int ntools, int rows) {
	int c = (ntools + rows - 1) / rows;
	return c < 1 ? 1 : c > HOME_TOOL_COLS ? HOME_TOOL_COLS
										  : c;
}

void HomeLayout_compute(float W, float H, float bar, int strip_lines, int ngames, int ntools, HomeLayout* out) {
	HomeLayout_computeStrip(W, H, bar, strip_lines, 1.0f, ngames, ntools, out);
}

void HomeLayout_computeStrip(float W, float H, float bar, int strip_lines, float strip_k, int ngames, int ntools,
							 HomeLayout* out) {
	memset(out, 0, sizeof(*out));
	if (strip_k <= 0)
		strip_k = 1.0f;
	HomeLayout* l = out;
	float L = HOME_EDGE, R = W - HOME_EDGE, bottom = H - bar - HOME_BOTTOM;
	if (ngames < 0)
		ngames = 0;
	if (ntools < 0)
		ntools = 0;
	l->wide = W >= 1.6f * H;
	l->strip_lines = strip_lines < 0 ? 0 : strip_lines > 2 ? 2
														   : strip_lines;
	// the strip: 27 px text from x 54, baselines 121 / 157 (two lines) or 124 (one) on the Brick (bar 84); the top
	// section from 187 / 155, or 111 without a strip. The strip's own offsets × strip_k (its text keeps one size)
	l->strip_x = L + 3;
	l->strip_right = R - 1;
	l->strip_base[0] = bar + (l->strip_lines == 2 ? 37 : 40) * strip_k;
	l->strip_base[1] = bar + 73 * strip_k;
	float y0 = bar + (l->strip_lines == 2 ? 103 * strip_k : l->strip_lines == 1 ? 71 * strip_k
																				: 27);
	l->top_y = y0;
	l->pin_h = HOME_PIN_H;
	l->k = l->wide ? 4 : 2;
	float page_bottom = bottom;
	// the Small UI scale with tools: the glyph at the Large size, the squares sized from it (the glyph 58% of a side)
	bool small = strip_k > 1.0f && ntools > 0;
	float small_glyph = 46 * strip_k, small_sq = roundf(small_glyph / 0.58f);
	HomeTile tiles[HOME_MAX_TOP]; // the squares, added after Continue (and the game column)
	int nt = 0;

	if (!l->wide || ngames == 0 || ngames <= HOME_WIDE_COL_GAMES) {
		// the top section down to the bottom: no pin rows (no games; on a wide screen up to 2), or the Brick, whose pin
		// rows sit below it, past the screen's end (the page scrolls to them). The tools four rows of squares filling
		// it, a column per 4, flush right; on a wide screen the games a column three squares wide left of them,
		// stacked; Continue the rest, from the left edge
		const int srows = 4;
		int colgames = l->wide ? ngames : 0;
		float h = bottom - y0;
		l->top_h = h;
		float sq = roundf((h - (srows - 1) * HOME_GAP) / srows), vgap = (h - srows * sq) / (srows - 1);
		float x = R; // the next column's right edge, right to left
		if (ntools > 0) {
			int cols = toolCols(ntools, srows);
			float bx = R - cols * sq - (cols - 1) * HOME_GAP;
			l->square = sq;
			l->glyph = roundf(sq * 0.58f);
			placeTools(l, tiles, &nt, bx, y0, sq, vgap, srows, cols, true, ntools);
			x = bx - HOME_GAP;
		}
		float gw = 3 * sq + 2 * HOME_GAP;
		if (colgames > 0)
			x -= gw;
		addTop(l, HOME_TILE_CONTINUE, 0, rect(L, y0, (colgames > 0 ? x - HOME_GAP : x) - L, h));
		float gh = (h - (colgames - 1) * HOME_GAP) / (colgames > 0 ? colgames : 1);
		for (int g = 0; g < colgames; g++)
			addTop(l, HOME_TILE_GAME, g, rect(x, y0 + g * (gh + HOME_GAP), gw, gh));
		if (!l->wide && ngames > 0) {
			// the Brick's pins: rows of two from the hint bar's top (HOME_BOTTOM under the top section), where Home's page
			// clip ends, so none of them peeks in under it. Their height as when one row shared the
			// screen with the top: at the Small scale, what three squares (20 Large px apart) leave of it
			if (strip_k > 1.0f)
				l->pin_h = bottom - (y0 + 3 * small_sq + 2 * 20 * strip_k + HOME_GAP);
			addRows(l, bottom + HOME_BOTTOM, (R - L - HOME_GAP) / 2, 0, ngames);
		}
	} else {
		// the Smart Pro S: 8 columns; two rows of four 2-column pins over the hint bar, the top section what they leave
		// (at most 372; a short screen keeps one row)
		float col = (R - L - 7 * HOME_GAP) / 8;
#define SPAN(n) ((n) * col + ((n) - 1) * HOME_GAP)
		int rows = 2;
		float h = minf(HOME_TOP_MAX, bottom - rows * HOME_PIN_H - rows * HOME_GAP - y0);
		if (h < 2 * HOME_PIN_H) {
			rows = 1;
			h = minf(HOME_TOP_MAX, bottom - HOME_PIN_H - HOME_GAP - y0);
		}
		float rows_y = y0 + h + HOME_GAP;
		if (small) { // the squares three rows tall set the top's height; one pin row takes the rest
			h = 3 * small_sq + 2 * HOME_GAP;
			rows_y = y0 + h + HOME_GAP;
			l->pin_h = bottom - rows_y;
		}
		l->top_h = h;
		// Continue from the left edge to 30 before the squares (no tools: the full width); the squares flush right: at
		// the Large scale in reading order, up to 4 tools a 2 x 2 block, 5 or 6 a 3 x 2 block (its third column taken
		// from Continue); at the Small scale three rows, filled a column at a time, a column per 3 tools.
		float cont_w = R - L;
		if (ntools > 0) {
			int srows = small ? 3 : 2;
			int cols = small ? toolCols(ntools, 3) : (ntools <= 4 ? 2 : 3);
			float sq = small ? small_sq : (h - HOME_GAP) / 2, bx = R - cols * sq - (cols - 1) * HOME_GAP;
			l->square = sq;
			l->glyph = small ? small_glyph : minf(46, roundf(sq * 0.58f));
			cont_w = bx - HOME_GAP - L;
			placeTools(l, tiles, &nt, bx, y0, sq, HOME_GAP, srows, cols, small, ntools);
		}
		addTop(l, HOME_TILE_CONTINUE, 0, rect(L, y0, cont_w, h));
		addRows(l, rows_y, SPAN(2), 0, ngames);
#undef SPAN
	}
	for (int i = 0; i < nt; i++)
		addTop(l, tiles[i].kind, tiles[i].ref, tiles[i].r);
	if (l->npins > 0) {
		HomeRect last = l->pins[l->npins - 1].r;
		if (last.y + last.h > page_bottom)
			page_bottom = last.y + last.h;
	}
	l->page_h = page_bottom + HOME_BOTTOM + bar;
	if (l->page_h < H)
		l->page_h = H;
}

///////////////////////////////////////
// The D-pad

#define EPS 0.5f

// The nearest top tile on that side overlapping the current one across the move; ties to prev, else the top-left.
static int topMove(const HomeLayout* l, int cur, int prev, HomeDir dir) {
	if (cur < 0 || cur >= l->ntop)
		return -1;
	HomeRect c = l->top[cur].r;
	bool side = dir == HOME_DIR_LEFT || dir == HOME_DIR_RIGHT;
	float best = 0;
	int n = 0, cand[HOME_MAX_TOP];
	float dist[HOME_MAX_TOP];
	for (int i = 0; i < l->ntop; i++) {
		if (i == cur)
			continue;
		HomeRect t = l->top[i].r;
		float ov = side ? minf(c.y + c.h, t.y + t.h) - fmaxf(c.y, t.y) : minf(c.x + c.w, t.x + t.w) - fmaxf(c.x, t.x);
		float d = dir == HOME_DIR_RIGHT	 ? t.x - (c.x + c.w)
				  : dir == HOME_DIR_LEFT ? c.x - (t.x + t.w)
				  : dir == HOME_DIR_DOWN ? t.y - (c.y + c.h)
										 : c.y - (t.y + t.h);
		if (ov <= EPS || d <= -EPS)
			continue;
		if (n == 0 || d < best)
			best = d;
		cand[n] = i, dist[n] = d, n++;
	}
	int pick = -1;
	for (int j = 0; j < n; j++) {
		if (dist[j] >= best + EPS)
			continue;
		int i = cand[j];
		if (i == prev)
			return i;
		if (pick < 0 || l->top[i].r.y < l->top[pick].r.y ||
			(l->top[i].r.y == l->top[pick].r.y && l->top[i].r.x < l->top[pick].r.x))
			pick = i;
	}
	return pick;
}

// The first-row pin whose centre is nearest the tile's centre across.
static int rowUnder(const HomeLayout* l, HomeRect c) {
	float mid = c.x + c.w / 2;
	int best = 0;
	for (int j = 1; j < l->k && j < l->npins; j++) {
		HomeRect p = l->pins[j].r, b = l->pins[best].r;
		if (fabsf(p.x + p.w / 2 - mid) < fabsf(b.x + b.w / 2 - mid))
			best = j;
	}
	return best;
}

HomeMoveResult HomeLayout_move(const HomeLayout* l, HomeFocus* f, HomeDir dir) {
	*f = HomeLayout_clampFocus(l, *f);
	if (f->sec == HOME_SEC_PINS) {
		int k = l->k, n = l->npins, i = f->pin, c = i % k;
		switch (dir) {
		case HOME_DIR_LEFT:
			if (c == 0)
				return HOME_MOVE_EDGE_PREV;
			f->pin = i - 1;
			return HOME_MOVE_MOVED;
		case HOME_DIR_RIGHT:
			if (c == k - 1 || i + 1 >= n)
				return HOME_MOVE_EDGE_NEXT;
			f->pin = i + 1;
			return HOME_MOVE_MOVED;
		case HOME_DIR_DOWN:
			if (i + k < n)
				f->pin = i + k;
			else if (i / k < (n - 1) / k)
				f->pin = n - 1; // a shorter last row: its last pin
			else
				return HOME_MOVE_STAY;
			return HOME_MOVE_MOVED;
		case HOME_DIR_UP:
			if (i >= k)
				f->pin = i - k;
			else
				f->sec = HOME_SEC_TOP; // the top tile last on
			return HOME_MOVE_MOVED;
		}
		return HOME_MOVE_STAY;
	}
	int m = topMove(l, f->top, f->prev, dir);
	if (m >= 0) {
		f->prev = f->top;
		f->top = m;
		return HOME_MOVE_MOVED;
	}
	switch (dir) {
	case HOME_DIR_LEFT:
		return HOME_MOVE_EDGE_PREV;
	case HOME_DIR_RIGHT:
		return HOME_MOVE_EDGE_NEXT;
	case HOME_DIR_UP:
		return HOME_MOVE_TABS;
	case HOME_DIR_DOWN:
		if (l->npins <= 0)
			return HOME_MOVE_STAY;
		f->prev = f->top;
		f->sec = HOME_SEC_PINS;
		f->pin = rowUnder(l, l->top[f->top].r);
		return HOME_MOVE_MOVED;
	}
	return HOME_MOVE_STAY;
}

void HomeLayout_fromTabs(const HomeLayout* l, HomeFocus* f) {
	*f = HomeLayout_clampFocus(l, *f);
	if (l->npins <= 0) {
		f->sec = HOME_SEC_TOP;
		return;
	}
	int n = l->npins, k = l->k;
	f->prev = f->top;
	f->pin = f->top == 0 ? (n - 1) / k * k : n - 1;
	f->sec = HOME_SEC_PINS;
}

HomeFocus HomeLayout_clampFocus(const HomeLayout* l, HomeFocus f) {
	if (f.top < 0)
		f.top = 0;
	if (f.top >= l->ntop)
		f.top = l->ntop > 0 ? l->ntop - 1 : 0;
	if (f.prev >= l->ntop)
		f.prev = -1;
	if (f.pin < 0)
		f.pin = 0;
	if (f.pin >= l->npins)
		f.pin = l->npins > 0 ? l->npins - 1 : 0;
	if (l->npins <= 0)
		f.sec = HOME_SEC_TOP;
	return f;
}

HomeRect HomeLayout_focusRect(const HomeLayout* l, HomeFocus f) {
	f = HomeLayout_clampFocus(l, f);
	if (f.sec == HOME_SEC_PINS)
		return l->pins[f.pin].r;
	return l->ntop > 0 ? l->top[f.top].r : rect(0, 0, 0, 0);
}

float HomeLayout_scrollFor(const HomeLayout* l, HomeFocus f, float H, float bar, float current) {
	float max = l->page_h - H;
	if (max < 0)
		max = 0;
	float s = current;
	if (HomeLayout_clampFocus(l, f).sec == HOME_SEC_TOP) {
		s = 0;
	} else {
		HomeRect r = HomeLayout_focusRect(l, f);
		// scrolled further, the pin's ring would pass under the tab row; scrolled less, its bottom would sit closer to
		// the hint bar than the first row does (a scrolled-to row lands where the rows rest)
		float lo = r.y - (HOME_RING + 8) - bar;
		float hi = r.y + r.h + HOME_BOTTOM - (H - bar);
		if (s > lo)
			s = lo;
		if (s < hi)
			s = hi;
	}
	return s < 0 ? 0 : s > max ? max
							   : s;
}
