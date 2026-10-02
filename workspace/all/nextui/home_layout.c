// Home's landscape split: Continue + stats card on top, pins (games 1x2 first, tools 1x1) in a grid below.
// All values in dp. Pure, host-tested.

#include "home_layout.h"
#include <string.h>

#define GUTTER 24.0f
#define GAP 14.0f
#define RING 3.0f
#define PAD_TOP 52.0f
#define PAD_BOTTOM 52.0f
#define TOP_FLOOR 165.0f
#define MIN_COL 90.0f
#define START_COLS 6
#define MIN_TILE 40.0f
#define SCROLL_PAD 8.0f

static float maxf(float a, float b) {
	return a > b ? a : b;
}
static float minf(float a, float b) {
	return a < b ? a : b;
}

void HomeLayout_compute(float screen_w, float screen_h, bool has_continue, const HomePinKind* kinds, int npins,
						HomeLayout* out) {
	memset(out, 0, sizeof(*out));
	if (npins < 0 || !kinds)
		npins = 0;
	if (npins > HOME_MAX_PINS)
		npins = HOME_MAX_PINS;
	out->gutter = GUTTER;
	out->gap = GAP;

	float inner = screen_w - 2 * GUTTER;
	int cols = START_COLS;
	float tw = (inner - (cols - 1) * GAP) / cols;
	while (tw < MIN_COL && cols > 2) {
		cols--;
		tw = (inner - (cols - 1) * GAP) / cols;
	}
	out->cols = cols;
	out->tw = tw;

	if (!has_continue && npins == 0) { // nothing played, no pins: one wide Pick-a-game card
		out->mode = HOME_MODE_FRESH;
		out->top_h = screen_h - (PAD_TOP + PAD_BOTTOM);
		out->cont = (HomeRect){GUTTER, PAD_TOP, inner, out->top_h};
		out->page_h = screen_h;
		return;
	}

	float room = screen_h - (PAD_TOP + GAP + RING + PAD_BOTTOM);
	float tile = minf(tw, maxf(tw * 0.75f, 0.4f * room));
	float top = maxf(TOP_FLOOR, room - tile);
	tile = maxf(MIN_TILE, room - top);
	if (npins == 0) {
		out->mode = HOME_MODE_NO_PINS;
		top = screen_h - (PAD_TOP + PAD_BOTTOM);
	} else {
		out->mode = HOME_MODE_FULL;
	}
	out->top_h = top;
	out->tile_h = tile;

	float card_w = 2 * tw + GAP;
	out->card = (HomeRect){screen_w - GUTTER - card_w, PAD_TOP, card_w, top};
	out->cont = (HomeRect){GUTTER, PAD_TOP, out->card.x - GAP - GUTTER, top};

	if (npins == 0) {
		out->page_h = screen_h;
		return;
	}

	// Games take two columns and start a new row when they don't fit (the gap stays empty); tools take one.
	float y0 = PAD_TOP + top + GAP;
	int row = 0, col = 0;
	for (int i = 0; i < npins; i++) {
		int span = kinds[i] == HOME_PIN_GAME ? 2 : 1;
		if (col + span > cols) {
			row++;
			col = 0;
		}
		HomePinSlot* s = &out->pins[i];
		s->row = row;
		s->col = col;
		s->span = span;
		s->r = (HomeRect){GUTTER + col * (tw + GAP), y0 + row * (tile + GAP), span * tw + (span - 1) * GAP, tile};
		col += span;
	}
	out->npins = npins;
	out->rows = row + 1;
	out->page_h = PAD_TOP + top + GAP + out->rows * tile + (out->rows - 1) * GAP + RING + PAD_BOTTOM;
}

static void rememberFocus(const HomeFocus* f, HomeFocusMemory* mem) {
	if (f->area == HOME_FOCUS_PIN)
		mem->last_pin = f->pin;
	else
		mem->last_top = f->area;
}

// The pin in `row` covering `col`, else that row's last pin; -1 if the row has no pins.
static int pinInRow(const HomeLayout* l, int row, int col) {
	int last = -1;
	for (int i = 0; i < l->npins; i++) {
		const HomePinSlot* s = &l->pins[i];
		if (s->row != row)
			continue;
		if (col >= s->col && col < s->col + s->span)
			return i;
		last = i;
	}
	return last;
}

HomeMoveResult HomeLayout_move(const HomeLayout* l, HomeFocus* f, HomeFocusMemory* mem, HomeDir dir) {
	*f = HomeLayout_clampFocus(l, *f);
	rememberFocus(f, mem);
	HomeMoveResult res = HOME_MOVE_STAY;

	if (f->area != HOME_FOCUS_PIN) {
		bool on_card = f->area == HOME_FOCUS_CARD;
		switch (dir) {
		case HOME_DIR_LEFT:
			if (on_card) {
				f->area = HOME_FOCUS_CONTINUE;
				res = HOME_MOVE_MOVED;
			} else {
				res = HOME_MOVE_EDGE_PREV;
			}
			break;
		case HOME_DIR_RIGHT:
			if (!on_card && l->mode != HOME_MODE_FRESH) {
				f->area = HOME_FOCUS_CARD;
				res = HOME_MOVE_MOVED;
			} else {
				res = HOME_MOVE_EDGE_NEXT;
			}
			break;
		case HOME_DIR_DOWN:
			if (l->npins > 0) {
				int p = mem->last_pin;
				if (p < 0)
					p = 0;
				if (p >= l->npins)
					p = l->npins - 1;
				f->area = HOME_FOCUS_PIN;
				f->pin = p;
				res = HOME_MOVE_MOVED;
			}
			break;
		case HOME_DIR_UP:
			break;
		}
	} else {
		const HomePinSlot* s = &l->pins[f->pin];
		switch (dir) {
		case HOME_DIR_LEFT:
			if (f->pin > 0 && l->pins[f->pin - 1].row == s->row) {
				f->pin--;
				res = HOME_MOVE_MOVED;
			} else {
				res = HOME_MOVE_EDGE_PREV;
			}
			break;
		case HOME_DIR_RIGHT:
			if (f->pin + 1 < l->npins && l->pins[f->pin + 1].row == s->row) {
				f->pin++;
				res = HOME_MOVE_MOVED;
			} else {
				res = HOME_MOVE_EDGE_NEXT;
			}
			break;
		case HOME_DIR_DOWN: {
			int p = s->row + 1 < l->rows ? pinInRow(l, s->row + 1, s->col) : -1;
			if (p >= 0) {
				f->pin = p;
				res = HOME_MOVE_MOVED;
			}
			break;
		}
		case HOME_DIR_UP:
			if (s->row == 0) {
				f->area = mem->last_top;
				if (f->area == HOME_FOCUS_CARD && l->mode == HOME_MODE_FRESH)
					f->area = HOME_FOCUS_CONTINUE;
				res = HOME_MOVE_MOVED;
			} else {
				int p = pinInRow(l, s->row - 1, s->col);
				if (p >= 0) {
					f->pin = p;
					res = HOME_MOVE_MOVED;
				}
			}
			break;
		}
	}

	rememberFocus(f, mem);
	return res;
}

HomeFocus HomeLayout_bottomFrom(const HomeLayout* l, HomeFocus from, HomeFocusMemory* mem) {
	from = HomeLayout_clampFocus(l, from);
	if (l->npins <= 0)
		return from;
	if (from.area != HOME_FOCUS_PIN)
		mem->last_top = from.area; // UP from the first pin row comes back to the item the tab row was entered from
	HomeFocus f = {HOME_FOCUS_PIN, l->npins - 1};
	mem->last_pin = f.pin;
	return f;
}

HomeFocus HomeLayout_clampFocus(const HomeLayout* l, HomeFocus f) {
	if (f.area == HOME_FOCUS_PIN) {
		if (l->npins <= 0) {
			f.area = HOME_FOCUS_CONTINUE;
			f.pin = 0;
		} else if (f.pin >= l->npins) {
			f.pin = l->npins - 1;
		} else if (f.pin < 0) {
			f.pin = 0;
		}
	} else if (f.area == HOME_FOCUS_CARD && l->mode == HOME_MODE_FRESH) {
		f.area = HOME_FOCUS_CONTINUE;
	}
	return f;
}

float HomeLayout_scrollFor(const HomeLayout* l, HomeFocus f, float screen_h, float bar_dp, float current) {
	f = HomeLayout_clampFocus(l, f);
	if (f.area != HOME_FOCUS_PIN)
		return 0;
	HomeRect r = l->pins[f.pin].r;
	float margin = RING + SCROLL_PAD;
	float lo = r.y + r.h + margin - (screen_h - bar_dp);
	float hi = r.y - margin - bar_dp;
	float off = current;
	if (lo > hi)
		off = hi;
	else
		off = minf(maxf(off, lo), hi);
	float max_off = maxf(0, l->page_h - screen_h);
	return minf(maxf(off, 0), max_off);
}
