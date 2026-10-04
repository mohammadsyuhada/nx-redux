// Home B2's geometry and D-pad (docs/home-b2.md): the worked numbers of the mockup on the Brick (1024 x 768) and the
// Smart Pro S (1920 x 1080 Brick px), then the moves.
#include "../../nextui/home_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define BAR 84.0f

static int near(float a, float b) {
	return fabsf(a - b) < 0.01f;
}

static int rectIs(HomeRect r, float x, float y, float w, float h) {
	return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
}

static void brick_strip_and_tools(void) {
	HomeLayout l;
	// two strip lines, 3 tools, 2 pins: top 187..465 (278), squares 79 with 20.5 gaps at x 894, Continue 813 wide
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &l);
	assert(!l.wide && l.k == 2 && near(l.strip_x, 54) && near(l.strip_base[0], 121) && near(l.strip_base[1], 157));
	assert(near(l.top_y, 187) && near(l.top_h, 278) && near(l.square, 79) && near(l.glyph, 46));
	assert(l.ntop == 4 && l.top[0].kind == HOME_TILE_CONTINUE && rectIs(l.top[0].r, 51, 187, 813, 278));
	assert(l.top[1].kind == HOME_TILE_TOOL && rectIs(l.top[1].r, 894, 187, 79, 79));
	assert(rectIs(l.top[2].r, 894, 286.5f, 79, 79) && rectIs(l.top[3].r, 894, 386, 79, 79));
	// the pins: two 446 wide at x 51 and 527, y 495, 155 tall
	assert(l.npins == 2 && rectIs(l.pins[0].r, 51, 495, 446, 155) && rectIs(l.pins[1].r, 527, 495, 446, 155));
	assert(near(l.page_h, 768));
	// one strip line: baseline 124, top 155..465 (310), squares 90 with 20 gaps at x 883, Continue 802
	HomeLayout_compute(1024, 768, BAR, 1, 2, 3, &l);
	assert(near(l.strip_base[0], 124) && near(l.top_h, 310) && near(l.square, 90));
	assert(rectIs(l.top[0].r, 51, 155, 802, 310) && rectIs(l.top[3].r, 883, 375, 90, 90));
	// no tools: Continue the full 922
	HomeLayout_compute(1024, 768, BAR, 2, 2, 0, &l);
	assert(l.ntop == 1 && rectIs(l.top[0].r, 51, 187, 922, 278) && l.square == 0);
	// 1 tool: stacked from the top; 5 tools: two and "+3"
	HomeLayout_compute(1024, 768, BAR, 2, 2, 1, &l);
	assert(l.ntop == 2 && rectIs(l.top[1].r, 894, 187, 79, 79));
	HomeLayout_compute(1024, 768, BAR, 2, 2, 5, &l);
	assert(l.ntop == 4 && l.top[2].kind == HOME_TILE_TOOL && l.top[3].kind == HOME_TILE_MORE && l.top[3].ref == 3);
	HomeLayout_compute(1024, 768, BAR, 2, 2, 4, &l);
	assert(l.top[3].kind == HOME_TILE_MORE && l.top[3].ref == 2);
	// no strip (fresh): the top from 111; no pins: the top runs down to 650
	HomeLayout_compute(1024, 768, BAR, 0, 0, 0, &l);
	assert(near(l.top_y, 111) && near(l.top_h, 650 - 111) && l.npins == 0);
	// more than one row: the rest go below, the page grows
	HomeLayout_compute(1024, 768, BAR, 2, 5, 0, &l);
	assert(l.npins == 5 && rectIs(l.pins[2].r, 51, 680, 446, 155) && rectIs(l.pins[4].r, 51, 865, 446, 155));
	assert(near(l.page_h, 865 + 155 + 34 + BAR));
}

static void sps_grid(void) {
	HomeLayout l;
	// 1920 x 1080: columns 201, the top capped at 372, two rows of four 432 pins; every pinned game in the rows
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 3, &l);
	assert(l.wide && l.k == 4 && near(l.top_y, 187) && near(l.top_h, 372) && near(l.strip_right, 1868));
	// up to 4 tools: the 2 x 2 block (171) from x 1497, Continue up to 30 before it (1416)
	assert(near(l.square, 171) && rectIs(l.top[0].r, 51, 187, 1416, 372));
	assert(rectIs(l.top[1].r, 1497, 187, 171, 171) && rectIs(l.top[2].r, 1698, 187, 171, 171));
	assert(rectIs(l.top[3].r, 1497, 388, 171, 171) && l.ntop == 4);
	// the rows from y0 + 402, games from the first
	assert(l.npins == 10 && l.pins[0].ref == 0 && rectIs(l.pins[0].r, 51, 589, 432, 155));
	assert(rectIs(l.pins[3].r, 1437, 589, 432, 155) && rectIs(l.pins[4].r, 51, 774, 432, 155));
	// one tool keeps the 2 x 2 block's place (Continue still 1416), at its top-left
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 1, &l);
	assert(l.ntop == 2 && rectIs(l.top[0].r, 51, 187, 1416, 372) && rectIs(l.top[1].r, 1497, 187, 171, 171));
	// 5 tools: a 3 x 2 block from x 1296, Continue 1215, five squares, no "+N"
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 5, &l);
	assert(l.ntop == 6 && rectIs(l.top[0].r, 51, 187, 1215, 372) && rectIs(l.top[1].r, 1296, 187, 171, 171));
	assert(rectIs(l.top[3].r, 1698, 187, 171, 171) && rectIs(l.top[5].r, 1497, 388, 171, 171));
	assert(l.top[5].kind == HOME_TILE_TOOL);
	// 6 tools fill the block; 8: five and "+3" in its last place
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 6, &l);
	assert(l.ntop == 7 && l.top[6].kind == HOME_TILE_TOOL && rectIs(l.top[6].r, 1698, 388, 171, 171));
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 8, &l);
	assert(l.ntop == 7 && l.top[6].kind == HOME_TILE_MORE && l.top[6].ref == 3 && rectIs(l.top[6].r, 1698, 388, 171, 171));
	// no tools: Continue the full width
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 0, &l);
	assert(l.ntop == 1 && rectIs(l.top[0].r, 51, 187, 1818, 372) && l.npins == 10);
}

static void top_moves(void) {
	HomeLayout l;
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &l); // Continue, 3 squares; 2 pins
	HomeFocus f = {HOME_SEC_TOP, 0, 0, -1};
	// RIGHT from Continue enters the column at its top
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.top == 2);
	// back LEFT to Continue, then RIGHT returns to the square left
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.top == 0);
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 2);
	// past the right edge: the next tab; UP from the top square: the tab row
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);
	f.top = 1;
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_TABS);
	f.top = 0;
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV);
	// DOWN: the first-row pin under the centre (Continue's centre 457.5 is nearer the left pin's 274 than 750? no: 750)
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.sec == HOME_SEC_PINS && f.pin == 0);
	f = (HomeFocus){HOME_SEC_TOP, 3, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.sec == HOME_SEC_PINS && f.pin == 1);
	// UP from the first row: the top tile last on
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && f.sec == HOME_SEC_TOP && f.top == 3);
	// no pins: DOWN stays
	HomeLayout_compute(1024, 768, BAR, 2, 0, 0, &l);
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY && f.sec == HOME_SEC_TOP);
}

static void pin_moves(void) {
	HomeLayout l;
	HomeLayout_compute(1920, 1080, BAR, 2, 6, 0, &l); // rows of 4 + 2
	assert(l.npins == 6);
	HomeFocus f = {HOME_SEC_PINS, 0, 3, -1};
	// RIGHT past a row's end: the next tab; LEFT along the row
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT && f.pin == 3);
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.pin == 2);
	// DOWN into the shorter last row: its last pin; DOWN on the last row stays
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.pin == 5);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY);
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && f.pin == 1);
	f.pin = 0;
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV);
	// DOWN from a square of the block (centre 1782.5) lands under it: pin 3 (centre 1653)
	HomeLayout_compute(1920, 1080, BAR, 2, 6, 2, &l);
	f = (HomeFocus){HOME_SEC_TOP, 2, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.pin == 3);
	// RIGHT from Continue enters the block's left column at its top; LEFT from it returns to Continue
	HomeLayout_compute(1920, 1080, BAR, 2, 6, 5, &l);
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.top == 0);
}

static void from_tabs_and_clamp(void) {
	HomeLayout l;
	HomeLayout_compute(1920, 1080, BAR, 2, 6, 1, &l); // rows 4 + 2, one tool
	HomeFocus f = {HOME_SEC_TOP, 0, 0, -1};
	HomeLayout_fromTabs(&l, &f); // from Continue: the last row's first pin
	assert(f.sec == HOME_SEC_PINS && f.pin == 4);
	f = (HomeFocus){HOME_SEC_TOP, 1, 0, -1};
	HomeLayout_fromTabs(&l, &f); // from another top tile: the last pin
	assert(f.pin == 5);
	HomeLayout_compute(1024, 768, BAR, 2, 0, 0, &l);
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	HomeLayout_fromTabs(&l, &f);
	assert(f.sec == HOME_SEC_TOP);
	// clamping after a rebuild with fewer tiles
	HomeLayout_compute(1024, 768, BAR, 2, 1, 1, &l);
	f = HomeLayout_clampFocus(&l, (HomeFocus){HOME_SEC_PINS, 5, 7, 9});
	assert(f.top == 1 && f.pin == 0 && f.prev == -1 && f.sec == HOME_SEC_PINS);
}

static void scroll(void) {
	HomeLayout l;
	HomeLayout_compute(1024, 768, BAR, 2, 5, 0, &l); // rows at 495, 680, 865
	HomeFocus f = {HOME_SEC_PINS, 0, 0, -1};
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, 0), 0));
	f.pin = 2; // its bottom 835 comes up to 650, where the rows rest
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, 0), 835 - 650));
	f.pin = 4;
	float s = HomeLayout_scrollFor(&l, f, 768, BAR, 0);
	assert(near(s, l.page_h - 768));
	f = (HomeFocus){HOME_SEC_TOP, 0, 4, -1};
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, s), 0));
}

// The strip at Large's size on a Small screen (strip_k 1.5, Home's 3 / 2): its baselines and the top section's start
// under it 1.5x their Brick px offsets from the bar, so they stay the Large px apart once scaled by 2/3; the rest as
// without; strip_k 1 is HomeLayout_compute.
static void strip_keeps_large_size(void) {
	HomeLayout a, b;
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &a);
	HomeLayout_computeStrip(1024, 768, BAR, 2, 1.0f, 2, 3, &b);
	assert(near(a.strip_base[0], b.strip_base[0]) && near(a.top_y, b.top_y) && near(a.top_h, b.top_h));
	HomeLayout_computeStrip(1024, 768, BAR, 2, 1.5f, 2, 3, &b);
	assert(near(b.strip_base[0], BAR + 37 * 1.5f) && near(b.strip_base[1], BAR + 73 * 1.5f));
	assert(near(b.top_y, BAR + 103 * 1.5f) && near(b.strip_x, a.strip_x));
	HomeLayout_computeStrip(1024, 768, BAR, 1, 1.5f, 2, 3, &b); // one line
	assert(near(b.strip_base[0], BAR + 40 * 1.5f) && near(b.top_y, BAR + 71 * 1.5f));
	HomeLayout_computeStrip(1024, 768, BAR, 0, 1.5f, 2, 3, &b); // no strip: the top section's start unchanged
	assert(near(b.top_y, BAR + 27));
}

// The Small UI scale with tools (strip_k 1.5): the glyph at the Large 46 px (69 Brick px), the squares sized from it
// (round(69 / 0.58) = 119), the top section just the squares tall, the pin rows taking the rest.
static void small_scale_tools_and_pins(void) {
	HomeLayout l;
	// the Brick at 2x: 1536 x 1152 Brick px, bar 126; strip top 280.5; a column of 3 squares 30 apart = 417 tall;
	// the pin row from 727.5 to the bottom (992): 264.5 tall (155 at Large)
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 2, 3, &l);
	assert(near(l.glyph, 69) && near(l.square, 119) && near(l.top_y, 280.5f) && near(l.top_h, 417));
	assert(rectIs(l.top[1].r, 1536 - 51 - 119, 280.5f, 119, 119) && near(l.top[2].r.y, 280.5f + 149));
	assert(near(l.pin_h, 264.5f) && near(l.pins[0].r.y, 727.5f) && near(l.pins[0].r.h, 264.5f));
	// the Smart Pro S at 2x: 1920 x 1080, bar 84; 3 tools one column of 3 (417 tall, 30 apart) flush right; one pin row
	// from 685.5 to the bottom (962): 276.5 tall
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 3, &l);
	assert(near(l.glyph, 69) && near(l.square, 119) && near(l.top_h, 417));
	assert(rectIs(l.top[1].r, 1920 - 51 - 119, 238.5f, 119, 119) && rectIs(l.top[3].r, 1920 - 51 - 119, 238.5f + 298, 119, 119));
	assert(near(l.pin_h, 276.5f) && near(l.pins[0].r.y, 685.5f) && near(l.pins[4].r.y, 685.5f + 276.5f + 30));
	// 4 tools: two columns, the first full (3), the second from the top
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 4, &l);
	float bx = 1920 - 51 - 2 * 119 - 30;
	assert(rectIs(l.top[1].r, bx, 238.5f, 119, 119) && rectIs(l.top[3].r, bx, 238.5f + 298, 119, 119));
	assert(rectIs(l.top[4].r, bx + 149, 238.5f, 119, 119) && l.ntop == 5);
	// 7 tools: five and "+N" in the 6 slots
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 7, &l);
	assert(l.ntop == 7 && l.top[6].kind == HOME_TILE_MORE && l.top[6].ref == 2 && rectIs(l.top[6].r, bx + 149, 238.5f + 298, 119, 119));
	// no tools: as before (the fixed pin rows)
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 0, &l);
	assert(near(l.pin_h, HOME_PIN_H));
}

int main(void) {
	brick_strip_and_tools();
	strip_keeps_large_size();
	small_scale_tools_and_pins();
	sps_grid();
	top_moves();
	pin_moves();
	from_tabs_and_clamp();
	scroll();
	printf("test_home_layout: all passed\n");
	return 0;
}
