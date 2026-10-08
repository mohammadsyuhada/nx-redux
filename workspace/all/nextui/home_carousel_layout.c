// home_carousel_layout.c — Home's Carousel geometry and D-pad (home_carousel_layout.h). SDL-free.
#include "home_carousel_layout.h"

#include <math.h>
#include <string.h>

#include "row_model.h"

static float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

void HomeCar_compute(float W, float H, float bar, int strip_lines, const HomeCarOpts* opts, int ngames, int ntools,
					 HomeCarLayout* out) {
	memset(out, 0, sizeof(*out));
	HomeCarLayout* l = out;
	float strip_k = opts && opts->strip_k > 0 ? opts->strip_k : 1.0f;
	float text_k = opts && opts->text_k > 0 ? opts->text_k : 1.0f;
	float asc = opts && opts->strip_asc > 0 ? opts->strip_asc : 0.0f;
	float desc = opts && opts->strip_desc > 0 ? opts->strip_desc : 0.0f;
	float max_h = opts && opts->tile_max_h > 0 ? opts->tile_max_h : 0.0f;
	if (ngames < 0)
		ngames = 0;
	if (ntools < 0)
		ntools = 0;
	if (ngames > HOME_MAX_PINS)
		ngames = HOME_MAX_PINS;
	l->nitems = 1 + ngames;
	l->centre_x = W / 2;
	l->side_scale = HOMECAR_SIDE_SCALE;
	l->gap = opts && opts->row_gap > 0 ? opts->row_gap : 0.0f;

	// the strip: its lines Grid's step apart (home_layout.c: baselines 37 and 73 × strip_k × text_k), each centred across
	l->strip_lines = strip_lines < 0 ? 0 : strip_lines > 2 ? 2
														   : strip_lines;
	l->strip_left = HOME_EDGE + 3;
	l->strip_right = W - HOME_EDGE - 3;
	float step = 36 * strip_k * text_k;
	float strip_h = l->strip_lines > 0 ? asc + (l->strip_lines - 1) * step + desc : 0.0f;
	int nspaces = l->strip_lines > 0 ? 3 : 2;

	// the dock: squares of HOMECAR_DOCK_SQ, as many as fit the content's width, centred
	float cw = W - 2 * HOME_EDGE;
	float sq = HOMECAR_DOCK_SQ, sp = HOMECAR_DOCK_SPACING;
	int fit = (int)floorf((cw + sp) / (sq + sp));
	if (fit < 1)
		fit = 1;
	l->ntools = ntools < fit ? ntools : fit;
	if (l->ntools > HOME_MAX_PINS)
		l->ntools = HOME_MAX_PINS;
	float dock_h = l->ntools > 0 ? HOMECAR_DOCK_GAP + sq : 0.0f;

	// the tile: what the body leaves (the strip, the spaces at their least, the ring room above and under it, the
	// dock), capped, in the shape
	float tabs = opts && opts->tabs_bottom > 0 ? opts->tabs_bottom : bar;
	float body = H - bar - tabs;
	float h = body - strip_h - nspaces * HOMECAR_MARGIN - 2 * HOME_RING - dock_h;
	if (max_h > 0 && h > max_h)
		h = max_h;
	if (h < HOMECAR_MIN_H)
		h = HOMECAR_MIN_H;
	float w = h * HOMECAR_SHAPE_W / HOMECAR_SHAPE_H;
	if (w > cw) {
		w = cw;
		h = w * HOMECAR_SHAPE_H / HOMECAR_SHAPE_W;
	}
	l->tile_w = w;
	l->tile_h = h;

	// the spaces share what's left (none under the dock's squares: a lit square is filled, never ringed)
	float block = HOME_RING + h + HOME_RING + dock_h;
	l->space = (body - strip_h - block) / nspaces;
	float y = tabs + l->space;
	if (l->strip_lines > 0) {
		l->strip_top = y;
		l->strip_base[0] = y + asc;
		l->strip_base[1] = l->strip_base[0] + step;
		l->strip_bottom = y + strip_h;
		y = l->strip_bottom + l->space;
	}
	l->row_y = y + HOME_RING;
	l->row_cy = l->row_y + h / 2;
	if (l->ntools > 0) {
		l->dock_sq = sq;
		l->dock_glyph = roundf(sq * HOMECAR_DOCK_GLYPH);
		l->dock_y = l->row_y + h + HOME_RING + HOMECAR_DOCK_GAP;
		float x = (W - (l->ntools * sq + (l->ntools - 1) * sp)) / 2;
		for (int i = 0; i < l->ntools; i++)
			l->dock[i] = (HomeRect){x + i * (sq + sp), l->dock_y, sq, sq};
	}

	// Pick a game alone: Grid's card (home_layout.c HomeLayout_computeOpts: its top section's top and strip baselines
	// from the bar, its bottom HOME_BOTTOM over the hint bar), or down to the dock's gap and ring
	if (opts && opts->no_continue && ngames == 0) {
		float sk = strip_k * text_k;
		float top = bar + (l->strip_lines == 2 ? 103 * sk : l->strip_lines == 1 ? 71 * sk
																				: 27);
		float bottom = l->ntools > 0 ? l->dock_y - HOMECAR_DOCK_GAP - HOME_RING : H - bar - HOME_BOTTOM;
		l->tile_w = cw;
		l->tile_h = bottom - top;
		l->row_y = top;
		l->row_cy = top + l->tile_h / 2;
		if (l->strip_lines > 0) {
			l->strip_base[0] = bar + (l->strip_lines == 2 ? 37 : 40) * sk;
			l->strip_base[1] = bar + 73 * sk;
			l->strip_top = l->strip_base[0] - asc;
			l->strip_bottom = l->strip_base[l->strip_lines - 1] + desc;
		}
	}
}

HomeCarItem HomeCar_item(const HomeCarLayout* l, int i, float pos) {
	RowSizes s = {l->tile_w, l->tile_h, l->side_scale, l->gap, 1.0f};
	RowItem it = Row_item(&s, ROW_CAROUSEL, (float)i, pos);
	HomeCarItem out;
	float w = l->tile_w * it.scale, h = l->tile_h * it.scale;
	out.r = (HomeRect){l->centre_x + it.dx - w / 2, l->row_cy - h / 2, w, h};
	out.scale = it.scale;
	out.darken = it.darken;
	out.visible = it.visible && i >= 0 && i < l->nitems;
	return out;
}

HomeCarFocus HomeCar_clampFocus(const HomeCarLayout* l, HomeCarFocus f) {
	f.sel = (int)clampf((float)f.sel, 0, (float)(l->nitems > 0 ? l->nitems - 1 : 0));
	f.tool = (int)clampf((float)f.tool, 0, (float)(l->ntools > 0 ? l->ntools - 1 : 0));
	if (l->ntools <= 0)
		f.dock = false;
	return f;
}

HomeMoveResult HomeCar_move(const HomeCarLayout* l, HomeCarFocus* f, HomeDir dir) {
	*f = HomeCar_clampFocus(l, *f);
	int* at = f->dock ? &f->tool : &f->sel;
	int n = f->dock ? l->ntools : l->nitems;
	switch (dir) {
	case HOME_DIR_LEFT:
		if (*at > 0) {
			(*at)--;
			return HOME_MOVE_MOVED;
		}
		return HOME_MOVE_EDGE_PREV;
	case HOME_DIR_RIGHT:
		if (*at < n - 1) {
			(*at)++;
			return HOME_MOVE_MOVED;
		}
		return HOME_MOVE_EDGE_NEXT;
	case HOME_DIR_UP:
		if (f->dock) {
			f->dock = false;
			return HOME_MOVE_MOVED;
		}
		return HOME_MOVE_TABS;
	case HOME_DIR_DOWN:
		if (!f->dock && l->ntools > 0) {
			f->dock = true;
			return HOME_MOVE_MOVED;
		}
		return HOME_MOVE_STAY;
	}
	return HOME_MOVE_STAY;
}

void HomeCar_fromTabs(const HomeCarLayout* l, HomeCarFocus* f) {
	*f = HomeCar_clampFocus(l, *f);
	if (l->ntools > 0)
		f->dock = true;
}
