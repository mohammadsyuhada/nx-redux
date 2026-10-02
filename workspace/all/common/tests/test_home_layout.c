#include "../../nextui/home_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(float a, float b) {
	return fabsf(a - b) < 0.05f;
}
static const HomePinKind G = HOME_PIN_GAME, T = HOME_PIN_TOOL;

static void brick_3x_floor_case(void) { // 478 x 358 dp
	HomePinKind k[] = {G, G, T};
	HomeLayout l;
	HomeLayout_compute(478, 358, true, k, 3, &l);
	assert(l.mode == HOME_MODE_FULL && l.cols == 4 && near(l.tw, 97));
	assert(near(l.top_h, 165) && near(l.tile_h, 72)); // floor holds the top; tiles give up height
	assert(near(l.card.w, 2 * 97 + 14) && near(l.cont.w, 430 - (2 * 97 + 14) - 14));
	assert(near(l.cont.x, 24) && near(l.cont.y, 52) && near(l.card.x + l.card.w, 478 - 24));
	// row 0: game cols 0-1, game cols 2-3; row 1: tool col 0
	assert(l.pins[0].row == 0 && l.pins[0].col == 0 && l.pins[0].span == 2);
	assert(l.pins[1].row == 0 && l.pins[1].col == 2);
	assert(l.pins[2].row == 1 && l.pins[2].col == 0 && l.pins[2].span == 1);
	assert(near(l.pins[0].r.y, 52 + 165 + 14) && near(l.pins[0].r.w, 2 * 97 + 14));
	assert(l.rows == 2 && near(l.page_h, 52 + 165 + 14 + 2 * 72 + 14 + 3 + 52));
}

static void sps_2x_six_columns(void) { // 896 x 504 dp
	HomePinKind k[] = {G};
	HomeLayout l;
	HomeLayout_compute(896, 504, true, k, 1, &l);
	assert(l.cols == 6 && near(l.tw, (848 - 70) / 6.0f));
	assert(near(l.tile_h, l.tw)); // 0.4 * room (153) capped at square
	assert(near(l.top_h, 383 - l.tw));
}

static void placement_reference_example(void) {
	HomePinKind k[] = {G, G, G, G, G, G, T, T, T, T, T};
	HomeLayout l;
	HomeLayout_compute(896, 504, true, k, 11, &l); // 6 columns: 3 games | 3 games | 5 tools
	assert(l.pins[2].row == 0 && l.pins[3].row == 1 && l.pins[6].row == 2 && l.pins[10].row == 2 && l.rows == 3);
	HomeLayout_compute(478, 358, true, k, 11, &l); // 4 columns: 2 games x3 rows, 4 tools, 1 tool
	assert(l.pins[5].row == 2 && l.pins[6].row == 3 && l.pins[9].row == 3 && l.pins[10].row == 4 && l.rows == 5);
}

static void game_gap_stays_empty(void) {
	// 597 x 336 (SPS 3x) has 5 columns: g g | (gap) ; g t t
	HomePinKind k[] = {G, G, G, T, T};
	HomeLayout l;
	HomeLayout_compute(597, 336, true, k, 5, &l);
	assert(l.cols == 5);
	assert(l.pins[1].row == 0 && l.pins[1].col == 2);
	assert(l.pins[2].row == 1 && l.pins[2].col == 0); // third game doesn't fit col 4
	assert(l.pins[3].row == 1 && l.pins[3].col == 2 && l.pins[4].col == 3);
}

static void fresh_and_no_pins_layouts(void) {
	HomeLayout l;
	HomeLayout_compute(478, 358, false, NULL, 0, &l);
	assert(l.mode == HOME_MODE_FRESH && near(l.cont.w, 430) && near(l.cont.h, 358 - 104) && l.card.w == 0);
	HomeLayout_compute(478, 358, true, NULL, 0, &l);
	assert(l.mode == HOME_MODE_NO_PINS && near(l.top_h, 358 - 104) && l.rows == 0);
	HomePinKind k[] = {T};
	HomeLayout_compute(478, 358, false, k, 1, &l); // pins but nothing played
	assert(l.mode == HOME_MODE_FULL);			   // the Continue slot shows the "Pick a game" card
}

static void moves_and_edges(void) {
	HomePinKind k[] = {G, G, T};
	HomeLayout l;
	HomeLayout_compute(478, 358, true, k, 3, &l);
	HomeFocus f = {HOME_FOCUS_CONTINUE, 0};
	HomeFocusMemory m = {HOME_FOCUS_CONTINUE, 0};
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_UP) == HOME_MOVE_STAY);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.area == HOME_FOCUS_CARD);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.area == HOME_FOCUS_PIN && f.pin == 0);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.pin == 1);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);			 // end of row 0
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.pin == 2); // shorter row: its last
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_DOWN) == HOME_MOVE_STAY);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_UP) == HOME_MOVE_MOVED && f.pin == 0);				  // col 0 → game at cols 0-1
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_UP) == HOME_MOVE_MOVED && f.area == HOME_FOCUS_CARD); // last top
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.pin == 0);			  // remembered pin
}

static void moves_without_pins(void) {
	HomeLayout l;
	HomeLayout_compute(478, 358, true, NULL, 0, &l);
	HomeFocus f = {HOME_FOCUS_CONTINUE, 0};
	HomeFocusMemory m = {HOME_FOCUS_CONTINUE, 0};
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_DOWN) == HOME_MOVE_STAY && f.area == HOME_FOCUS_CONTINUE);
	HomeLayout_compute(478, 358, false, NULL, 0, &l); // fresh: one item
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_DOWN) == HOME_MOVE_STAY);
}

static void focus_clamps_after_rebuild(void) {
	HomePinKind k[] = {G};
	HomeLayout l;
	HomeLayout_compute(478, 358, true, k, 1, &l);
	HomeFocus f = HomeLayout_clampFocus(&l, (HomeFocus){HOME_FOCUS_PIN, 4});
	assert(f.area == HOME_FOCUS_PIN && f.pin == 0);
	HomeLayout_compute(478, 358, true, NULL, 0, &l);
	f = HomeLayout_clampFocus(&l, (HomeFocus){HOME_FOCUS_PIN, 0});
	assert(f.area == HOME_FOCUS_CONTINUE);
	HomeLayout_compute(478, 358, false, NULL, 0, &l);
	f = HomeLayout_clampFocus(&l, (HomeFocus){HOME_FOCUS_CARD, 0});
	assert(f.area == HOME_FOCUS_CONTINUE); // fresh has no card
}

// UP on the tab row wraps to the last pin, from Continue, Pick a game or the stats card; no pins: the top item stays.
static void bottom_from_tab_row(void) {
	HomePinKind k[] = {G, G, T, T, T}; // Brick 4 cols: g g | t t t (row 1: pins 2-4)
	HomeLayout l;
	HomeLayout_compute(478, 358, true, k, 5, &l);
	assert(l.rows == 2 && l.pins[2].row == 1 && l.pins[4].row == 1);
	HomeFocusMemory m = {HOME_FOCUS_CONTINUE, 0};
	HomeFocus f = HomeLayout_bottomFrom(&l, (HomeFocus){HOME_FOCUS_CONTINUE, 0}, &m);
	assert(f.area == HOME_FOCUS_PIN && f.pin == 4 && m.last_pin == 4 && m.last_top == HOME_FOCUS_CONTINUE);
	f = HomeLayout_bottomFrom(&l, (HomeFocus){HOME_FOCUS_CARD, 0}, &m);
	assert(f.area == HOME_FOCUS_PIN && f.pin == 4 && m.last_top == HOME_FOCUS_CARD);
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_UP) == HOME_MOVE_MOVED && f.pin == 1);				  // a normal move from there
	assert(HomeLayout_move(&l, &f, &m, HOME_DIR_UP) == HOME_MOVE_MOVED && f.area == HOME_FOCUS_CARD); // where it began
	HomePinKind one[] = {G};																		  // a single pin
	HomeLayout_compute(478, 358, true, one, 1, &l);
	f = HomeLayout_bottomFrom(&l, (HomeFocus){HOME_FOCUS_CARD, 0}, &m);
	assert(f.area == HOME_FOCUS_PIN && f.pin == 0);
	HomeLayout_compute(478, 358, false, k, 5, &l); // nothing played: from "Pick a game"
	f = HomeLayout_bottomFrom(&l, (HomeFocus){HOME_FOCUS_CONTINUE, 0}, &m);
	assert(f.area == HOME_FOCUS_PIN && f.pin == 4);
	HomeLayout_compute(478, 358, true, NULL, 0, &l); // no pins: the top item stays
	f = HomeLayout_bottomFrom(&l, (HomeFocus){HOME_FOCUS_CARD, 0}, &m);
	assert(f.area == HOME_FOCUS_CARD);
	HomeLayout_compute(478, 358, false, NULL, 0, &l); // fresh: Pick a game stays
	f = HomeLayout_bottomFrom(&l, (HomeFocus){HOME_FOCUS_CONTINUE, 0}, &m);
	assert(f.area == HOME_FOCUS_CONTINUE);
}

static void scroll_keeps_ring_in_body(void) {
	HomePinKind k[] = {G, G, G, G, G, G};
	HomeLayout l;
	HomeLayout_compute(478, 358, true, k, 6, &l); // 3 rows of 72
	float bar = 28 * 42 / 30.0f;				  // the 28-logical bars in dp
	assert(HomeLayout_scrollFor(&l, (HomeFocus){HOME_FOCUS_CONTINUE, 0}, 358, bar, 50) == 0);
	float off = HomeLayout_scrollFor(&l, (HomeFocus){HOME_FOCUS_PIN, 5}, 358, bar, 0);
	HomeRect r = l.pins[5].r;
	assert(r.y + r.h + 3 + 8 - off <= 358 - bar + 0.01f);
	assert(off <= l.page_h - 358 + 0.01f);
	float back = HomeLayout_scrollFor(&l, (HomeFocus){HOME_FOCUS_PIN, 0}, 358, bar, off);
	assert(l.pins[0].r.y - 3 - 8 - back >= bar - 0.01f);
}

int main(void) {
	brick_3x_floor_case();
	sps_2x_six_columns();
	placement_reference_example();
	game_gap_stays_empty();
	fresh_and_no_pins_layouts();
	moves_and_edges();
	moves_without_pins();
	focus_clamps_after_rebuild();
	scroll_keeps_ring_in_body();
	bottom_from_tab_row();
	printf("test_home_layout: ok\n");
	return 0;
}
