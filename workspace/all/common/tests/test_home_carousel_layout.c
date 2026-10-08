// Home's Carousel geometry and D-pad (nextui/home_carousel_layout.h): the Brick (1536 x 1152 Home units, bars 126) and
// the Smart Pro S (2560 x 1440), then the moves.
#include "../../nextui/home_carousel_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define BAR 126.0f
#define UNDERLINE (BAR - 9) // the tab underline's bottom: SCALE1(2) px above the bar's (9 units on the Brick)

static int near(float a, float b) {
	return fabsf(a - b) < 0.01f;
}

// The Brick: the strip 1.5 x (the UI scale over Home's) with 33 px text over its 27, its ink 36 above a baseline and 10
// under, the game list Carousel's centre 537 tall (358 px), its gap 25.6 (17 px).
static const HomeCarOpts BRICK = {1.5f, 33.0f / 27.0f, 36.0f, 10.0f, 537.0f, 25.6f, UNDERLINE};
// The Smart Pro S: 26 px over 20, its ink 40 and 12, the centre 640 tall (320 px), the gap 42.
static const HomeCarOpts SPS = {1.5f, 1.3f, 40.0f, 12.0f, 640.0f, 42.0f, UNDERLINE};

// The block's bottom: the dock's squares, else the ring under the tile.
static float blockBottom(const HomeCarLayout* l) {
	return l->ntools > 0 ? l->dock_y + l->dock_sq : l->row_y + l->tile_h + HOME_RING;
}

// The strip midway between the tab row's underline and the selection's ring, the dock (or the ring) as far above the hint bar.
static void assertSpaces(const HomeCarLayout* l, float H, const HomeCarOpts* o) {
	float above_strip = l->strip_top - o->tabs_bottom, under_strip = (l->row_y - HOME_RING) - l->strip_bottom;
	float under_block = (H - BAR) - blockBottom(l);
	assert(near(above_strip, l->space) && near(under_strip, l->space) && near(under_block, l->space));
	assert(l->space >= HOMECAR_MARGIN - 0.01f);
	// the strip's ink between its first baseline's tall letters and its last's descenders, Grid's step apart
	float step = 36 * o->strip_k * o->text_k;
	assert(near(l->strip_base[0] - o->strip_asc, l->strip_top));
	assert(near(l->strip_base[l->strip_lines - 1] + o->strip_desc, l->strip_bottom));
	if (l->strip_lines == 2)
		assert(near(l->strip_base[1] - l->strip_base[0], step));
}

static void brick_two_lines_and_tools(void) {
	HomeCarLayout l;
	HomeCar_compute(1536, 1152, BAR, 2, &BRICK, 4, 3, &l);
	assert(l.strip_lines == 2);
	assertSpaces(&l, 1152, &BRICK);
	// the body (909: the underline to the hint bar) less the strip (36 + 66 + 10), three margins, the rings and the dock (150) leaves 599: the cap wins
	assert(near(l.tile_h, 537) && near(l.tile_w, 537 * 340.0f / 240.0f));
	// the spaces share the rest: (909 - 112 - (12 + 537 + 150)) / 3
	float strip_h = 36 + 36 * 1.5f * 33.0f / 27.0f + 10;
	assert(near(l.space, (909 - strip_h - (12 + 537 + 150)) / 3));
	assert(near(l.dock_y, l.row_y + 537 + HOME_RING + HOMECAR_DOCK_GAP));
	// the row: Continue + 4 games around the centre, the dock's three squares centred
	assert(l.nitems == 5 && l.ntools == 3 && near(l.centre_x, 768) && near(l.row_cy, l.row_y + 537 / 2.0f));
	float dock_w = 3 * HOMECAR_DOCK_SQ + 2 * HOMECAR_DOCK_SPACING;
	assert(near(l.dock[0].x, (1536 - dock_w) / 2) && near(l.dock[2].x + l.dock[2].w, (1536 + dock_w) / 2));
	assert(near(l.dock[1].y, l.dock_y) && near(l.dock_glyph, roundf(117 * 0.58f)));
}

static void brick_one_line_no_tools_and_fresh(void) {
	HomeCarLayout l;
	// one strip line (nothing played this month), with and without tools
	HomeCar_compute(1536, 1152, BAR, 1, &BRICK, 2, 3, &l);
	assert(l.strip_lines == 1 && near(l.strip_bottom - l.strip_top, 46));
	assertSpaces(&l, 1152, &BRICK);
	HomeCar_compute(1536, 1152, BAR, 1, &BRICK, 2, 0, &l);
	assert(l.ntools == 0 && near(l.tile_h, 537));
	assertSpaces(&l, 1152, &BRICK);
	HomeCar_compute(1536, 1152, BAR, 2, &BRICK, 2, 0, &l);
	assertSpaces(&l, 1152, &BRICK);
	// fresh: no strip, Pick a game alone, two equal spaces above its ring and under it
	HomeCar_compute(1536, 1152, BAR, 0, &BRICK, 0, 0, &l);
	assert(l.strip_lines == 0 && l.nitems == 1);
	assert(near(l.row_y - HOME_RING - UNDERLINE, l.space) && near(1152 - BAR - blockBottom(&l), l.space));
	// no cap: the body's whole room (no strip, no dock: 909 - 2 x 12 - 12), the spaces at their least
	HomeCarOpts nocap = BRICK;
	nocap.tile_max_h = 0;
	HomeCar_compute(1536, 1152, BAR, 0, &nocap, 0, 0, &l);
	assert(near(l.tile_h, 909 - 24 - 12) && near(l.row_y, UNDERLINE + 12 + HOME_RING) && near(l.space, 12));
	// no underline given: from the bar
	nocap.tabs_bottom = 0;
	HomeCar_compute(1536, 1152, BAR, 0, &nocap, 0, 0, &l);
	assert(near(l.tile_h, 900 - 24 - 12) && near(l.row_y, BAR + 12 + HOME_RING));
}

static void sps_capped(void) {
	HomeCarLayout l;
	// the body leaves far more than 640: the cap, 906.67 wide; the spaces grow, still equal (2 and 1 lines, tools or not)
	HomeCar_compute(2560, 1440, BAR, 2, &SPS, 6, 5, &l);
	assert(near(l.tile_h, 640) && near(l.tile_w, 640 * 340.0f / 240.0f) && l.ntools == 5 && l.space > 50);
	assertSpaces(&l, 1440, &SPS);
	HomeCar_compute(2560, 1440, BAR, 1, &SPS, 6, 5, &l);
	assertSpaces(&l, 1440, &SPS);
	HomeCar_compute(2560, 1440, BAR, 2, &SPS, 6, 0, &l);
	assertSpaces(&l, 1440, &SPS);
	HomeCar_compute(2560, 1440, BAR, 1, &SPS, 0, 0, &l);
	assertSpaces(&l, 1440, &SPS);
}

static void width_bound_and_dock_fit(void) {
	HomeCarLayout l;
	// a narrow screen: the tile no wider than the content (HOME_EDGE each side), its height following the shape
	HomeCarOpts o = {1, 1, 0, 0, 0, 20, 0};
	HomeCar_compute(700, 1152, BAR, 0, &o, 1, 0, &l);
	assert(near(l.tile_w, 700 - 2 * HOME_EDGE) && near(l.tile_h, (700 - 2 * HOME_EDGE) * 240.0f / 340.0f));
	// as many squares as fit: (598 + 32) / (117 + 32) = 4 of 9
	HomeCar_compute(700, 1152, BAR, 0, &o, 1, 9, &l);
	assert(l.ntools == 4 && l.dock[3].x + l.dock[3].w <= 700 - HOME_EDGE + 0.01f);
}

static void items_slide(void) {
	HomeCarLayout l;
	HomeCar_compute(1536, 1152, BAR, 2, &BRICK, 4, 0, &l);
	// at rest on 0: Continue full size centred, lit; item 1 at 0.62, gap 25.6 from Continue's edge, darkened 0.6
	HomeCarItem c = HomeCar_item(&l, 0, 0), n = HomeCar_item(&l, 1, 0);
	assert(c.visible && near(c.scale, 1) && near(c.darken, 0) && near(c.r.x, 768 - l.tile_w / 2) && near(c.r.y, l.row_y));
	assert(near(n.scale, 0.62f) && near(n.darken, 0.6f) && near(n.r.x, c.r.x + l.tile_w + 25.6f));
	assert(near(n.r.y + n.r.h / 2, l.row_cy));
	// none before the first item; 3 steps out still in, 4 out gone; past the list's end nothing
	assert(!HomeCar_item(&l, -1, 0).visible && HomeCar_item(&l, 3, 0).visible && !HomeCar_item(&l, 4, 0).visible);
	assert(HomeCar_item(&l, 4, 1).visible && !HomeCar_item(&l, 5, 1).visible);
	// halfway to 1: both items between their sizes, symmetric about the centre
	HomeCarItem a = HomeCar_item(&l, 0, 0.5f), b = HomeCar_item(&l, 1, 0.5f);
	assert(near(a.scale, b.scale) && near(a.r.x + a.r.w, 768 - (b.r.x - 768)));
}

static void moves(void) {
	HomeCarLayout l;
	HomeCar_compute(1536, 1152, BAR, 2, &BRICK, 2, 2, &l); // Continue + 2 games, 2 tools
	HomeCarFocus f = {false, 0, 0};
	assert(HomeCar_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV && f.sel == 0);
	assert(HomeCar_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.sel == 1);
	assert(HomeCar_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.sel == 2);
	assert(HomeCar_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT && f.sel == 2);
	assert(HomeCar_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_TABS && !f.dock);
	// DOWN into the dock on its first square; along it; past its ends the tabs; UP back to the same row item
	assert(HomeCar_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.dock && f.tool == 0);
	assert(HomeCar_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY && f.dock);
	assert(HomeCar_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV && f.tool == 0);
	assert(HomeCar_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.tool == 1);
	assert(HomeCar_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT && f.tool == 1);
	assert(HomeCar_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && !f.dock && f.sel == 2);
	// back down: the square last on
	assert(HomeCar_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.dock && f.tool == 1);
	// from the tab row: the dock (its square kept), or the row without tools
	f = (HomeCarFocus){false, 1, 1};
	HomeCar_fromTabs(&l, &f);
	assert(f.dock && f.tool == 1 && f.sel == 1);
	HomeCar_compute(1536, 1152, BAR, 2, &BRICK, 2, 0, &l);
	f = (HomeCarFocus){false, 1, 0};
	HomeCar_fromTabs(&l, &f);
	assert(!f.dock && f.sel == 1);
	assert(HomeCar_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY && !f.dock);
	// clamped after a rebuild: fewer games, no tools
	f = (HomeCarFocus){true, 5, 4};
	f = HomeCar_clampFocus(&l, f);
	assert(!f.dock && f.sel == 2 && f.tool == 0);
}

// Pick a game alone (no Continue, no pinned games): Grid's card, the content's width from Grid's top-section top
// (bar + 27 with no strip) down to HOME_BOTTOM over the hint bar; with tools down to the dock, which stays put.
static void pick_alone(float W, float H, const HomeCarOpts* base) {
	HomeCarOpts o = *base;
	o.no_continue = true;
	HomeCarLayout l, row;
	HomeCar_compute(W, H, BAR, 0, &o, 0, 0, &l);
	assert(l.nitems == 1 && l.ntools == 0 && l.strip_lines == 0);
	assert(near(l.tile_w, W - 2 * HOME_EDGE) && near(l.row_y, BAR + 27) && near(l.tile_h, H - BAR - HOME_BOTTOM - (BAR + 27)));
	HomeCarItem it = HomeCar_item(&l, 0, 0);
	assert(it.visible && near(it.scale, 1) && near(it.r.x, HOME_EDGE) && near(it.r.y, BAR + 27));
	assert(near(it.r.w, W - 2 * HOME_EDGE) && near(it.r.y + it.r.h, H - BAR - HOME_BOTTOM));
	assert(!HomeCar_item(&l, 1, 0).visible); // no neighbours
	// with tools: the dock where the row (Continue or Pick at the shape) puts it, the card down to its gap and ring
	HomeCar_compute(W, H, BAR, 0, base, 0, 3, &row);
	HomeCar_compute(W, H, BAR, 0, &o, 0, 3, &l);
	assert(l.ntools == 3 && near(l.dock_y, row.dock_y) && near(l.dock[0].x, row.dock[0].x));
	assert(near(l.tile_w, W - 2 * HOME_EDGE) && near(l.row_y, BAR + 27));
	assert(near(l.row_y + l.tile_h + HOME_RING + HOMECAR_DOCK_GAP, l.dock_y));
	// a strip (tools pinned, so not fresh): Grid's baselines and top section under them
	float sk = o.strip_k * o.text_k;
	HomeCar_compute(W, H, BAR, 1, &o, 0, 3, &l);
	assert(near(l.strip_base[0], BAR + 40 * sk) && near(l.row_y, BAR + 71 * sk) && near(l.strip_top, l.strip_base[0] - o.strip_asc));
	HomeCar_compute(W, H, BAR, 2, &o, 0, 3, &l);
	assert(near(l.strip_base[0], BAR + 37 * sk) && near(l.strip_base[1], BAR + 73 * sk) && near(l.row_y, BAR + 103 * sk));
	assert(near(l.strip_bottom, l.strip_base[1] + o.strip_desc));
	// a Continue or a pinned game keeps the row's shape
	HomeCar_compute(W, H, BAR, 0, &o, 1, 0, &l);
	HomeCar_compute(W, H, BAR, 0, base, 1, 0, &row);
	assert(near(l.tile_w, row.tile_w) && near(l.tile_h, row.tile_h) && near(l.row_y, row.row_y));
}

int main(void) {
	brick_two_lines_and_tools();
	brick_one_line_no_tools_and_fresh();
	sps_capped();
	width_bound_and_dock_fit();
	items_slide();
	moves();
	pick_alone(1536, 1152, &BRICK);
	pick_alone(2560, 1440, &SPS);
	printf("test_home_carousel_layout: all passed\n");
	return 0;
}
