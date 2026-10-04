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
	float page_bottom = bottom;
	// the Small UI scale with tools: the glyph at the Large size, the squares sized from it (the glyph 58% of a side)
	bool small = strip_k > 1.0f && ntools > 0;
	float small_glyph = 46 * strip_k, small_sq = roundf(small_glyph / 0.58f);

	if (!l->wide) {
		// the Brick: one row of two pins over the hint bar (more go below, the page scrolls), the top section down to
		// 30 above it; no pins: the top section runs on down
		l->k = 2;
		float row_y = bottom - HOME_PIN_H;
		float h = (ngames > 0 ? row_y - HOME_GAP : bottom) - y0;
		if (small) { // the column of squares (20 Large px apart) sets the top's height; the pin row takes the rest
			h = 3 * small_sq + 2 * 20 * strip_k;
			if (ngames > 0) {
				row_y = y0 + h + HOME_GAP;
				l->pin_h = bottom - row_y;
			}
		}
		l->top_h = h;
		// a column of 3 squares flush right: side round((h − 40) / 3), the gaps sharing the rest; more than 3 tools:
		// the first two and "+N"
		float cont_w = R - L;
		if (ntools > 0) {
			float sq = small ? small_sq : roundf((h - 40) / 3), gap = (h - 3 * sq) / 2, x = R - sq;
			l->square = sq;
			l->glyph = small ? small_glyph : minf(46, roundf(sq * 0.58f));
			cont_w = x - HOME_GAP - L;
			addTop(l, HOME_TILE_CONTINUE, 0, rect(L, y0, cont_w, h));
			int shown = ntools > 3 ? 2 : ntools;
			for (int i = 0; i < shown; i++)
				addTop(l, HOME_TILE_TOOL, i, rect(x, y0 + i * (sq + gap), sq, sq));
			if (ntools > 3)
				addTop(l, HOME_TILE_MORE, ntools - 2, rect(x, y0 + 2 * (sq + gap), sq, sq));
		} else {
			addTop(l, HOME_TILE_CONTINUE, 0, rect(L, y0, cont_w, h));
		}
		addRows(l, row_y, (R - L - HOME_GAP) / 2, 0, ngames);
	} else {
		// the Smart Pro S: 8 columns; two rows of four 2-column pins over the hint bar, the top section what they leave
		// (at most 372; a short screen keeps one row)
		l->k = 4;
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
		// Continue from the left edge to 30 before the squares (no tools: the full width); the squares flush right in
		// reading order: up to 4 tools a 2 x 2 block, 5 or more a 3 x 2 block (its third column taken from Continue),
		// more than 6 tools five and "+N". Every pinned game goes in the rows (docs/home-b2.md: no large first game).
		float cont_w = R - L;
		// The Small UI scale: three rows of squares, filled a column at a time (one column for up to 3 tools, two for
		// up to 6, more: five and "+N").
		if (ntools > 0) {
			int srows = small ? 3 : 2;
			int cols = small ? (ntools <= 3 ? 1 : 2) : (ntools <= 4 ? 2 : 3), slots = srows * cols;
			float sq = small ? small_sq : (h - HOME_GAP) / 2, bx = R - cols * sq - (cols - 1) * HOME_GAP;
			l->square = sq;
			l->glyph = small ? small_glyph : minf(46, roundf(sq * 0.58f));
			cont_w = bx - HOME_GAP - L;
			addTop(l, HOME_TILE_CONTINUE, 0, rect(L, y0, cont_w, h));
			int shown = ntools > slots ? slots - 1 : ntools;
			for (int i = 0; i <= shown && i < slots; i++) {
				int c = small ? i / srows : i % cols, rr = small ? i % srows : i / cols;
				HomeRect r = rect(bx + (float)c * (sq + HOME_GAP), y0 + (float)rr * (sq + HOME_GAP), sq, sq);
				if (i < shown)
					addTop(l, HOME_TILE_TOOL, i, r);
				else if (ntools > slots)
					addTop(l, HOME_TILE_MORE, ntools - shown, r);
			}
		} else {
			addTop(l, HOME_TILE_CONTINUE, 0, rect(L, y0, cont_w, h));
		}
		addRows(l, rows_y, SPAN(2), 0, ngames);
#undef SPAN
	}
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
