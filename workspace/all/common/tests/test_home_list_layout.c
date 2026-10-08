// Home's List rows and geometry (nextui/home_list_layout.h): the row order, the selection clamp, the strip and the
// list's top, the Continue tag.
#include "../../nextui/home_list_layout.h"
#include <assert.h>
#include <stdio.h>

// Continue, then the games in pin order, then the tools.
static void row_order(void) {
	assert(HomeList_count(0, 0) == 1);
	assert(HomeList_count(3, 2) == 6);
	HomeListItem it = HomeList_item(0, 3, 2);
	assert(it.kind == HOMELIST_CONTINUE && it.ref == 0);
	it = HomeList_item(1, 3, 2);
	assert(it.kind == HOMELIST_GAME && it.ref == 0);
	it = HomeList_item(3, 3, 2);
	assert(it.kind == HOMELIST_GAME && it.ref == 2);
	it = HomeList_item(4, 3, 2);
	assert(it.kind == HOMELIST_TOOL && it.ref == 0);
	it = HomeList_item(5, 3, 2);
	assert(it.kind == HOMELIST_TOOL && it.ref == 1);
	// tools only
	it = HomeList_item(1, 0, 2);
	assert(it.kind == HOMELIST_TOOL && it.ref == 0);
	// out of range reads as the nearest row
	it = HomeList_item(9, 1, 1);
	assert(it.kind == HOMELIST_TOOL && it.ref == 0);
	it = HomeList_item(-1, 1, 1);
	assert(it.kind == HOMELIST_CONTINUE);
}

static void clamp(void) {
	assert(HomeList_clampSel(4, 2, 0) == 2);
	assert(HomeList_clampSel(-3, 2, 0) == 0);
	assert(HomeList_clampSel(1, 2, 0) == 1);
	assert(HomeList_clampSel(5, 0, 0) == 0);
}

// The Brick: bar 66, 8 dp = 11, 12 dp = 17, a 33 px strip font (ascent 31, descent 8), lines 36 apart.
static void geometry(void) {
	HomeListOpts o = {66, 11, 17, 66 + 17, 31, 8, 36};
	HomeListGeom g;
	HomeList_compute(0, &o, &g);
	assert(g.strip_lines == 0 && g.list_top == 83);
	HomeList_compute(1, &o, &g);
	assert(g.strip_lines == 1 && g.strip_base[0] == 66 + 11 + 31);
	assert(g.list_top == g.strip_base[0] + 8 + 17);
	HomeList_compute(2, &o, &g);
	assert(g.strip_lines == 2 && g.strip_base[1] == g.strip_base[0] + 36);
	assert(g.list_top == g.strip_base[1] + 8 + 17);
	HomeList_compute(5, &o, &g); // clamped to 2
	assert(g.strip_lines == 2);
}

// The mockup's tag on its 16 px line: 2 and 8 px padding, 10 px before the name.
static void tag(void) {
	HomeListTag t = HomeList_tag(16, 50);
	assert(t.pad_y == 2 && t.pad_x == 8 && t.gap == 10);
	assert(t.w == 66 && t.h == 20);
	t = HomeList_tag(24, 70); // 1.5 x
	assert(t.pad_y == 3 && t.pad_x == 12 && t.gap == 15 && t.w == 94 && t.h == 30);
}

int main(void) {
	row_order();
	clamp();
	geometry();
	tag();
	printf("test_home_list_layout: all passed\n");
	return 0;
}
