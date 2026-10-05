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
	// two strip lines, 3 tools, 2 pins: the top section down to the bottom whatever the pins (187..650, 463), its
	// squares four rows of round(373 / 4) = 93 (30.3 apart) at x 880, Continue 799 wide
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &l);
	assert(!l.wide && l.k == 2 && near(l.strip_x, 54) && near(l.strip_base[0], 121) && near(l.strip_base[1], 157));
	assert(near(l.top_y, 187) && near(l.top_h, 463) && near(l.square, 93) && near(l.glyph, 54));
	assert(l.ntop == 4 && l.top[0].kind == HOME_TILE_CONTINUE && rectIs(l.top[0].r, 51, 187, 799, 463));
	assert(l.top[1].kind == HOME_TILE_TOOL && rectIs(l.top[1].r, 880, 187, 93, 93));
	assert(near(l.top[2].r.y, 187 + 93 + 91.0f / 3) && near(l.top[3].r.y, 187 + 2 * (93 + 91.0f / 3)));
	// the pins below it, past the screen's end: two 446 wide at x 51 and 527, y 684 (the hint bar's top, 34 under 650), 155 tall
	assert(l.npins == 2 && rectIs(l.pins[0].r, 51, 684, 446, 155) && rectIs(l.pins[1].r, 527, 684, 446, 155));
	assert(near(l.page_h, 684 + 155 + 34 + BAR));
	// one strip line: baseline 124, top 155..650 (495), squares 101 at x 872, Continue 791
	HomeLayout_compute(1024, 768, BAR, 1, 2, 3, &l);
	assert(near(l.strip_base[0], 124) && near(l.top_h, 495) && near(l.square, 101));
	assert(rectIs(l.top[0].r, 51, 155, 791, 495) && rectIs(l.top[1].r, 872, 155, 101, 101));
	// no tools: Continue the full 922
	HomeLayout_compute(1024, 768, BAR, 2, 2, 0, &l);
	assert(l.ntop == 1 && rectIs(l.top[0].r, 51, 187, 922, 463) && l.square == 0);
	// 1 tool: from the top; 4 tools: one column of 4; 5 tools: a second column, the first full, Continue 676
	HomeLayout_compute(1024, 768, BAR, 2, 2, 1, &l);
	assert(l.ntop == 2 && rectIs(l.top[1].r, 880, 187, 93, 93));
	HomeLayout_compute(1024, 768, BAR, 2, 2, 4, &l);
	assert(l.ntop == 5 && rectIs(l.top[4].r, 880, 557, 93, 93));
	HomeLayout_compute(1024, 768, BAR, 2, 2, 5, &l);
	assert(l.ntop == 6 && rectIs(l.top[0].r, 51, 187, 676, 463) && rectIs(l.top[1].r, 757, 187, 93, 93));
	assert(l.top[5].kind == HOME_TILE_TOOL && l.top[5].ref == 4 && rectIs(l.top[5].r, 880, 187, 93, 93));
	// no strip (fresh): the top from 111; no pins: the top runs down to 650
	HomeLayout_compute(1024, 768, BAR, 0, 0, 0, &l);
	assert(near(l.top_y, 111) && near(l.top_h, 650 - 111) && l.npins == 0);
	// more pins: rows of two from 684, the page grows
	HomeLayout_compute(1024, 768, BAR, 2, 5, 0, &l);
	assert(l.npins == 5 && rectIs(l.pins[2].r, 51, 869, 446, 155) && rectIs(l.pins[4].r, 51, 1054, 446, 155));
	assert(near(l.page_h, 1054 + 155 + 34 + BAR));
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
	// 6 tools fill the block; 8: the six that fit (Pin Tool keeps within the cap)
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 6, &l);
	assert(l.ntop == 7 && l.top[6].kind == HOME_TILE_TOOL && rectIs(l.top[6].r, 1698, 388, 171, 171));
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 8, &l);
	assert(l.ntop == 7 && l.top[6].kind == HOME_TILE_TOOL && l.top[6].ref == 5 && rectIs(l.top[6].r, 1698, 388, 171, 171));
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
	HomeLayout_compute(1024, 768, BAR, 2, 5, 0, &l); // rows at 684, 869, 1054, all below the screen's end
	HomeFocus f = {HOME_SEC_PINS, 0, 0, -1};
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, 0), 839 - 650)); // its bottom 839 comes up to 650
	f.pin = 2;
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, 0), 1024 - 650));
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
	// the Brick at 2x: 1536 x 1152 Brick px, bar 126; strip top 280.5; the top down to the bottom (992, 711.5) with
	// four rows of 155 squares whatever the pins; the pin rows from 1026 (the hint bar's top), 264.5 tall: what three Large
	// squares (119, 20 Large px apart) would leave of the screen, as when a row shared it
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 2, 3, &l);
	assert(near(l.square, 155) && near(l.glyph, 90) && near(l.top_y, 280.5f) && near(l.top_h, 711.5f));
	assert(rectIs(l.top[1].r, 1536 - 51 - 155, 280.5f, 155, 155) && near(l.top[2].r.y, 466));
	assert(near(l.pin_h, 264.5f) && rectIs(l.pins[0].r, 51, 1026, 702, 264.5f) && near(l.page_h, 1450.5f));
	// no tools: the same pin rows
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 4, 0, &l);
	assert(near(l.pin_h, 264.5f) && rectIs(l.pins[2].r, 51, 1026 + 264.5f + 30, 702, 264.5f));
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
	// 7 tools: a third column from 1452, Continue 1371; 10 tools: the nine that fit
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 7, &l);
	assert(l.ntop == 8 && l.top[7].kind == HOME_TILE_TOOL && rectIs(l.top[7].r, 1750, 238.5f, 119, 119));
	assert(rectIs(l.top[0].r, 51, 238.5f, 1371, 417) && rectIs(l.top[1].r, 1452, 238.5f, 119, 119));
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 10, &l);
	assert(l.ntop == 10 && l.top[9].kind == HOME_TILE_TOOL && l.top[9].ref == 8 && rectIs(l.top[9].r, 1750, 238.5f + 298, 119, 119));
	// no tools: as before (the fixed pin rows)
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 8, 0, &l);
	assert(near(l.pin_h, HOME_PIN_H));
}

// The wide layout with at most 2 pinned games: no pin rows, the top section down to the bottom (962). The tools four
// rows of squares filling it, a column at a time, flush right; the games a column three squares wide left of them,
// stacked; Continue the rest.
static void wide_few_games(void) {
	HomeLayout l;
	float d = 171 + 91.0f / 3; // Large: top 187..962 (775); squares round(685 / 4) = 171, the rows 30.33 apart
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 0, &l);
	assert(l.wide && l.npins == 0 && near(l.top_h, 775) && l.ntop == 1 && rectIs(l.top[0].r, 51, 187, 1818, 775));
	assert(near(l.page_h, 1080));
	// 3 tools: one column flush right from the top, Continue up to 30 before it; glyph 58%
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 3, &l);
	assert(near(l.square, 171) && near(l.glyph, 99) && l.ntop == 4);
	assert(rectIs(l.top[0].r, 51, 187, 1617, 775) && rectIs(l.top[1].r, 1698, 187, 171, 171));
	assert(rectIs(l.top[3].r, 1698, 187 + 2 * d, 171, 171));
	// 4 tools fill the column; the 4th at the bottom (962)
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 4, &l);
	assert(l.ntop == 5 && rectIs(l.top[4].r, 1698, 791, 171, 171) && rectIs(l.top[0].r, 51, 187, 1617, 775));
	// 1 game, 6 tools: two columns from 1497 (4 + 2), the game 573 wide (3 squares) full height from 894, Continue 813
	HomeLayout_compute(1920, 1080, BAR, 2, 1, 6, &l);
	assert(l.ntop == 8 && l.npins == 0);
	assert(rectIs(l.top[0].r, 51, 187, 813, 775));
	assert(l.top[1].kind == HOME_TILE_GAME && l.top[1].ref == 0 && rectIs(l.top[1].r, 894, 187, 573, 775));
	assert(l.top[2].kind == HOME_TILE_TOOL && l.top[2].ref == 0 && rectIs(l.top[2].r, 1497, 187, 171, 171));
	assert(rectIs(l.top[5].r, 1497, 791, 171, 171) && rectIs(l.top[6].r, 1698, 187, 171, 171));
	assert(l.top[7].kind == HOME_TILE_TOOL && l.top[7].ref == 5 && rectIs(l.top[7].r, 1698, 187 + d, 171, 171));
	// 2 games, no tools: stacked at the right, each (775 − 30) / 2 tall
	HomeLayout_compute(1920, 1080, BAR, 2, 2, 0, &l);
	assert(l.ntop == 3 && rectIs(l.top[0].r, 51, 187, 1215, 775));
	assert(rectIs(l.top[1].r, 1296, 187, 573, 372.5f) && rectIs(l.top[2].r, 1296, 589.5f, 573, 372.5f));
	assert(l.top[2].kind == HOME_TILE_GAME && l.top[2].ref == 1);
	// 2 games, 10 tools: three columns from 1296, the game from 693, Continue 612; Continue + 2 + 10 = 13 top tiles
	HomeLayout_compute(1920, 1080, BAR, 2, 2, 10, &l);
	assert(l.ntop == 13 && rectIs(l.top[0].r, 51, 187, 612, 775) && rectIs(l.top[1].r, 693, 187, 573, 372.5f));
	assert(rectIs(l.top[3].r, 1296, 187, 171, 171) && l.top[12].kind == HOME_TILE_TOOL && rectIs(l.top[12].r, 1698, 187 + d, 171, 171));
	// 14 tools: the twelve that fit
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 14, &l);
	assert(l.ntop == 13 && l.top[12].kind == HOME_TILE_TOOL && l.top[12].ref == 11 && rectIs(l.top[12].r, 1698, 791, 171, 171));
	// Small, 9 tools, no games: three columns of 158 from 1335, Continue 1254
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 0, 9, &l);
	assert(l.ntop == 10 && rectIs(l.top[0].r, 51, 238.5f, 1254, 723.5f) && rectIs(l.top[9].r, 1711, 238.5f, 158, 158));
	// Small (strip_k 1.5): top 238.5..962 (723.5); squares round(633.5 / 4) = 158, 30.5 apart down; the game 534 wide
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 1, 3, &l);
	assert(near(l.top_h, 723.5f) && near(l.square, 158) && near(l.glyph, 92));
	assert(rectIs(l.top[2].r, 1711, 238.5f, 158, 158) && rectIs(l.top[4].r, 1711, 238.5f + 2 * 188.5f, 158, 158));
	assert(rectIs(l.top[1].r, 1147, 238.5f, 534, 723.5f) && rectIs(l.top[0].r, 51, 238.5f, 1066, 723.5f));
	// 3 games: the pin rows as before
	HomeLayout_compute(1920, 1080, BAR, 2, 3, 3, &l);
	assert(l.npins == 3 && near(l.top_h, 372) && l.ntop == 4);
}

// D-pad across the wide few-games top: Continue → the games → the tools, UP/DOWN within the game column, DOWN at the
// bottom stays (no rows), UP from the tab row lands in the top section.
static void wide_few_games_moves(void) {
	HomeLayout l;
	HomeLayout_compute(1920, 1080, BAR, 2, 2, 6, &l); // Continue, games 1-2, tools 3-8
	HomeFocus f = {HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.top == 2);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY && f.sec == HOME_SEC_TOP);
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && l.top[f.top].kind == HOME_TILE_TOOL);
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.top == 2);
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_TABS);
	HomeLayout_fromTabs(&l, &f);
	assert(f.sec == HOME_SEC_TOP);
}

// The Brick at Home's Small scale (1536 x 1152 Brick px, bar 126): no games, the top down to the bottom (992) with the
// tools four rows of 155 squares, two columns for 8; with games three rows of 119, up to three columns over the pin row.
static void brick_small_tools(void) {
	HomeLayout l;
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 0, 8, &l);
	assert(near(l.top_h, 711.5f) && near(l.square, 155) && near(l.glyph, 90) && l.npins == 0 && l.ntop == 9);
	assert(rectIs(l.top[0].r, 51, 280.5f, 1064, 711.5f) && rectIs(l.top[1].r, 1145, 280.5f, 155, 155));
	assert(rectIs(l.top[8].r, 1330, 280.5f + 3 * 185.5f, 155, 155));
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 0, 0, &l); // nothing pinned: Continue fills it
	assert(l.ntop == 1 && rectIs(l.top[0].r, 51, 280.5f, 1434, 711.5f));
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 2, 9, &l); // pinned games too: the same top section
	assert(near(l.top_h, 711.5f) && l.ntop == 10 && rectIs(l.top[0].r, 51, 280.5f, 879, 711.5f));
	assert(rectIs(l.top[1].r, 960, 280.5f, 155, 155) && rectIs(l.top[9].r, 1330, 280.5f, 155, 155));
	assert(l.npins == 2 && near(l.pins[0].r.y, 1026));
}

int main(void) {
	brick_small_tools();
	wide_few_games();
	wide_few_games_moves();
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
