#include "../../nextui/stack_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(float a, float b, float e) {
	return fabsf(a - b) <= e;
}

// The two screens: px per dp (FIXED_SCALE · 30/42), the 28-logical bars (tab row and hint bar) and the body between.
// Brick 1024×768 at scale 3: 84 px bars, a 600 px body = 280 dp, 477.9 dp wide.
// Smart Pro S 1280×720 at scale 2: 56 px bars, a 608 px body = 425.6 dp, 896 dp wide.
#define BRICK_PD (3 * 30.0f / 42.0f)
#define SPS_PD (2 * 30.0f / 42.0f)
#define BRICK_W (1024 / BRICK_PD)
#define BRICK_BODY (600 / BRICK_PD)
#define SPS_W (1280 / SPS_PD)
#define SPS_BODY (608 / SPS_PD)

static int px(float dp, float pd) {
	return Stack_round(dp * pd);
}

static void slot_tables(void) {
	// both screens are wide enough: every slot keeps its spec size
	const float ws[2] = {BRICK_W, SPS_W};
	for (int i = 0; i < 2; i++) {
		StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, ws[i]);
		assert(near(c.item_w, 330, 1e-4f) && near(c.item_h, 100, 1e-4f) && near(c.scale, 0.5f, 1e-6f));
		assert(near(c.gap, 16, 1e-6f) && near(c.cap, 26, 1e-6f));
		StackSizes l = Stack_mainSizes(STACK_MAIN_COLLECTIONS, ws[i]);
		assert(near(l.item_w, 400, 1e-4f) && near(l.item_h, 100, 1e-4f) && near(l.scale, 0.5f, 1e-6f));
		assert(near(l.gap, 14, 1e-6f) && near(l.cap, 0, 1e-6f));
		StackSizes t = Stack_mainSizes(STACK_MAIN_TOOLS, ws[i]);
		assert(near(t.item_w, 150, 1e-4f) && near(t.item_h, 140, 1e-4f) && near(t.scale, 0.6f, 1e-6f));
		assert(near(t.gap, 10, 1e-6f) && near(t.cap, 0, 1e-6f));
	}
	// in px, selected and neighbour (Brick, then SPS)
	StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, BRICK_W);
	assert(px(c.item_w, BRICK_PD) == 707 && px(c.item_h, BRICK_PD) == 214);
	assert(px(c.item_w * c.scale, BRICK_PD) == 354 && px(c.item_h * c.scale, BRICK_PD) == 107);
	assert(px(c.item_w, SPS_PD) == 471 && px(c.item_h, SPS_PD) == 143);
	assert(px(c.item_w * c.scale, SPS_PD) == 236 && px(c.item_h * c.scale, SPS_PD) == 71);
	StackSizes l = Stack_mainSizes(STACK_MAIN_COLLECTIONS, BRICK_W);
	assert(px(l.item_w, BRICK_PD) == 857 && px(l.item_w * l.scale, BRICK_PD) == 429);
	assert(px(l.item_w, SPS_PD) == 571 && px(l.item_w * l.scale, SPS_PD) == 286);
	StackSizes t = Stack_mainSizes(STACK_MAIN_TOOLS, BRICK_W);
	assert(px(t.item_w, BRICK_PD) == 321 && px(t.item_h, BRICK_PD) == 300);
	assert(px(t.item_w * t.scale, BRICK_PD) == 193 && px(t.item_h * t.scale, BRICK_PD) == 180);
	assert(px(t.item_w, SPS_PD) == 214 && px(t.item_h, SPS_PD) == 200);
	assert(px(t.item_w * t.scale, SPS_PD) == 129 && px(t.item_h * t.scale, SPS_PD) == 120);
	// a narrow window: the width is held to body − 2 · 24 dp (the height and the rest stay)
	StackSizes nc = Stack_mainSizes(STACK_MAIN_CONSOLES, 300);
	assert(near(nc.item_w, 252, 1e-4f) && near(nc.item_h, 100, 1e-4f));
	StackSizes nl = Stack_mainSizes(STACK_MAIN_COLLECTIONS, 400);
	assert(near(nl.item_w, 352, 1e-4f));
	StackSizes nt = Stack_mainSizes(STACK_MAIN_TOOLS, 190);
	assert(near(nt.item_w, 142, 1e-4f) && near(nt.item_h, 140, 1e-4f));
	assert(Stack_mainSizes(STACK_MAIN_TOOLS, 20).item_w == 0);
}

static void offsets_on_y(void) {
	StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, SPS_W); // h 100, s 0.5, g 16, cap 26
	float first = 50 + 16 + 25, step = 50 + 16;					// 91, then 66
	StackItem a = Stack_item(&c, 5, 5);
	assert(near(a.dy, 0, 1e-4f) && near(a.scale, 1, 1e-6f) && near(a.alpha, 1, 1e-6f) && a.visible);
	StackItem up1 = Stack_item(&c, 4, 5), up2 = Stack_item(&c, 3, 5);
	assert(near(up1.dy, -first, 1e-3f) && near(up2.dy, -(first + step), 1e-3f)); // above: no cap
	StackItem dn1 = Stack_item(&c, 6, 5), dn2 = Stack_item(&c, 7, 5);
	assert(near(dn1.dy, first + 26, 1e-3f) && near(dn2.dy, first + step + 26, 1e-3f)); // below: + cap
	assert(near(dn1.scale, 0.5f, 1e-6f) && near(dn2.scale, 0.5f, 1e-6f));
	// half a step: the size halfway to s, the offset and the cap's room half in (dd = 0.5)
	StackItem h = Stack_item(&c, 5.5f, 5);
	assert(near(h.scale, 0.75f, 1e-6f) && near(h.dy, first / 2 + 13, 1e-3f));
	StackItem hu = Stack_item(&c, 4.5f, 5);
	assert(near(hu.dy, -first / 2, 1e-3f) && near(hu.scale, 0.75f, 1e-6f));
	// Tools: h 140, s 0.6, g 10 → 122, then 94
	StackSizes t = Stack_mainSizes(STACK_MAIN_TOOLS, BRICK_W);
	assert(near(Stack_item(&t, 1, 0).dy, 122, 1e-3f) && near(Stack_item(&t, 2, 0).dy, 216, 1e-3f));
	assert(near(Stack_item(&t, 1, 0).scale, 0.6f, 1e-6f));
	// Collections: h 100, s 0.5, g 14, no cap → 89, then 64
	StackSizes l = Stack_mainSizes(STACK_MAIN_COLLECTIONS, BRICK_W);
	assert(near(Stack_item(&l, 3, 1).dy, 89 + 64, 1e-3f) && near(Stack_item(&l, 0, 1).dy, -89, 1e-3f));
}

// The Consoles stack with a pad behind the selection (docs/controller-art.md): its reach is the larger of the item's
// own and the pad's half ± its offset plus 10 dp; only the steps next to the selection grow, further steps don't.
static void pad_reach(void) {
	StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, SPS_W); // h 100, s 0.5, g 16, cap 26
	assert(c.extra_up == 0 && c.extra_down == 0);				// no pad: today's offsets (offsets_on_y)
	// a 2:1 pad in 2.6 h x 1.7 h (260 x 170) draws 260 x 130; centred 13 below the item (the logo and its count)
	float up, down;
	Stack_padExtra(&c, 130, 13, 10, &up, &down);
	assert(near(up, 65 - 13 + 10 - 50, 1e-4f) && near(down, 65 + 13 + 10 - 76, 1e-4f)); // 12 and 12
	// a pad inside the item's own reach adds nothing
	Stack_padExtra(&c, 60, 13, 10, &up, &down);
	assert(up == 0 && down == 0);
	Stack_padExtra(&c, 130, 13, 10, &c.extra_up, &c.extra_down);
	float first = 50 + 16 + 25, step = 50 + 16;
	assert(near(Stack_item(&c, 4, 5).dy, -(first + 12), 1e-3f) && near(Stack_item(&c, 3, 5).dy, -(first + 12 + step), 1e-3f));
	assert(near(Stack_item(&c, 6, 5).dy, first + 12 + 26, 1e-3f) && near(Stack_item(&c, 7, 5).dy, first + 12 + step + 26, 1e-3f));
	// half a step in: the room eases in with the offset, as the cap's
	assert(near(Stack_item(&c, 5.5f, 5).dy, (first + 12) / 2 + 13, 1e-3f));
}

static void fades(void) {
	StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, BRICK_W);
	assert(near(Stack_item(&c, 1, 0).alpha, 0.5f, 1e-5f));			 // neighbour 1 at 50%
	assert(near(Stack_item(&c, 2, 0).alpha, 0.38f, 1e-5f));			 // then 0.12 less per step
	assert(near(Stack_item(&c, 3, 0).alpha, 0.26f, 1e-5f));			 //
	assert(near(Stack_item(&c, 0.5f, 0).alpha, 0.75f, 1e-5f));		 // 1 → 0.5 over the first step
	assert(near(Stack_item(&c, 3.5f, 0).alpha, 0.2f * 0.5f, 1e-5f)); // fading out over 3..4 (floor 0.2, halfway)
	assert(near(Stack_item(&c, 3.25f, 0).alpha, 0.23f * 0.75f, 1e-5f));
	assert(!Stack_item(&c, 4, 0).visible && !Stack_item(&c, -4, 0).visible); // hidden from 4 on
	assert(Stack_item(&c, 3.99f, 0).visible);
	assert(near(Stack_item(&c, 0, 2).alpha, Stack_item(&c, 4, 2).alpha, 1e-6f)); // the same both ways
	StackSizes t = Stack_mainSizes(STACK_MAIN_TOOLS, SPS_W);
	assert(near(Stack_item(&t, 6, 0).alpha, 0, 1e-6f) && !Stack_item(&t, 6, 0).visible); // d 6: hidden, alpha 0
}

static void selection_centre(void) {
	// y = body/2 − cap/2 (dp from the body top), and its px on each screen (floor(v + 0.5))
	assert(near(Stack_selectionY(280, 26), 127, 1e-4f));
	assert(near(Stack_selectionY(280, 0), 140, 1e-4f));
	StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, BRICK_W);
	assert(84 + px(Stack_selectionY(BRICK_BODY, c.cap), BRICK_PD) == 356); // Brick: 84 + 272
	assert(56 + px(Stack_selectionY(SPS_BODY, c.cap), SPS_PD) == 341);	   // SPS: 56 + 285
	StackSizes t = Stack_mainSizes(STACK_MAIN_TOOLS, BRICK_W);
	assert(84 + px(Stack_selectionY(BRICK_BODY, t.cap), BRICK_PD) == 384); // the body's middle
	assert(56 + px(Stack_selectionY(SPS_BODY, t.cap), SPS_PD) == 360);
	// Consoles, Brick: the neighbour under the selection at 127 + 91 + 26 = 244 dp → 84 + 523 px
	assert(84 + px(Stack_selectionY(BRICK_BODY, c.cap) + Stack_item(&c, 1, 0).dy, BRICK_PD) == 84 + 523);
}

static void visible_range(void) {
	int f, l;
	StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, BRICK_W);
	Stack_visibleRange(&c, 0, 0, BRICK_BODY, &f, &l);
	assert(l < f);
	Stack_visibleRange(&c, 1, 0, BRICK_BODY, &f, &l);
	assert(f == 0 && l == 0);
	// Brick (280 dp), Consoles at 5: 4 (centre 36) and 6 (244) show; 3 (−30) and 7 (310) fall outside the body
	Stack_visibleRange(&c, 10, 5, BRICK_BODY, &f, &l);
	assert(f == 4 && l == 6);
	// SPS (425.6 dp, selection at 199.8): 2 (−23.2, its lower edge 1.8 inside) to 8 (448.8, its top 423.8 inside)
	StackSizes cs = Stack_mainSizes(STACK_MAIN_CONSOLES, SPS_W);
	Stack_visibleRange(&cs, 10, 5, SPS_BODY, &f, &l);
	assert(f == 2 && l == 8);
	// at the ends: clamped to the list
	Stack_visibleRange(&cs, 10, 0, SPS_BODY, &f, &l);
	assert(f == 0 && l == 3);
	Stack_visibleRange(&cs, 10, 9, SPS_BODY, &f, &l);
	assert(f == 6 && l == 9);
	// Tools, Brick: centre 140, neighbours at 18 and 262 (half 42: both reach in), 2 steps (−76, 356) don't
	StackSizes t = Stack_mainSizes(STACK_MAIN_TOOLS, BRICK_W);
	Stack_visibleRange(&t, 10, 5, BRICK_BODY, &f, &l);
	assert(f == 4 && l == 6);
	// mid-slide the range follows pos
	Stack_visibleRange(&t, 10, 5.5f, BRICK_BODY, &f, &l);
	assert(f == 4 && l == 7);
}

static void edge_fade(void) {
	assert(near(Stack_edgeAlpha(0, 280, 20), 0, 1e-6f));
	assert(near(Stack_edgeAlpha(10, 280, 20), 0.5f, 1e-6f));
	assert(near(Stack_edgeAlpha(20, 280, 20), 1, 1e-6f));
	assert(near(Stack_edgeAlpha(140, 280, 20), 1, 1e-6f));
	assert(near(Stack_edgeAlpha(275, 280, 20), 0.25f, 1e-6f));
	assert(near(Stack_edgeAlpha(-3, 280, 20), 0, 1e-6f) && near(Stack_edgeAlpha(290, 280, 20), 0, 1e-6f));
	assert(near(Stack_edgeAlpha(5, 280, 0), 1, 1e-6f)); // no fade
}

static void rounding(void) {
	assert(Stack_round(196.5f) == 197 && Stack_round(2.5f) == 3 && Stack_round(2.49f) == 2);
	assert(Stack_round(-0.5f) == 0 && Stack_round(-0.51f) == -1);
}

static void navigation(void) {
	StackNav v;
	// UP/DOWN step and stop at the ends, held or fresh (no wrap)
	v = Stack_navigate(5, 2, STACK_KEY_UP, false, true);
	assert(v.action == STACK_NAV_MOVE && v.sel == 1);
	v = Stack_navigate(5, 2, STACK_KEY_DOWN, false, false);
	assert(v.action == STACK_NAV_MOVE && v.sel == 3);
	v = Stack_navigate(5, 4, STACK_KEY_DOWN, true, true);
	assert(v.action == STACK_NAV_NONE && v.sel == 4);
	v = Stack_navigate(5, 4, STACK_KEY_DOWN, false, false);
	assert(v.action == STACK_NAV_NONE && v.sel == 4);
	// main menu: UP on the first item goes to the tab row on a fresh press; held, it stops
	v = Stack_navigate(5, 0, STACK_KEY_UP, true, true);
	assert(v.action == STACK_NAV_TAB_ROW && v.sel == 0);
	v = Stack_navigate(5, 0, STACK_KEY_UP, false, true);
	assert(v.action == STACK_NAV_NONE && v.sel == 0);
	// an empty tab: UP goes to the tab row; DOWN does nothing
	v = Stack_navigate(0, 0, STACK_KEY_UP, true, true);
	assert(v.action == STACK_NAV_TAB_ROW);
	v = Stack_navigate(0, 0, STACK_KEY_DOWN, true, true);
	assert(v.action == STACK_NAV_NONE);
	// game list: UP on the first item does nothing, fresh or held
	v = Stack_navigate(5, 0, STACK_KEY_UP, true, false);
	assert(v.action == STACK_NAV_NONE && v.sel == 0);
	v = Stack_navigate(0, 0, STACK_KEY_UP, true, false);
	assert(v.action == STACK_NAV_NONE);
	// main menu: LEFT/RIGHT switch tab on a fresh press only, from any item (empty tab too)
	v = Stack_navigate(5, 3, STACK_KEY_LEFT, true, true);
	assert(v.action == STACK_NAV_SWITCH_TAB && v.dir == -1 && v.sel == 3);
	v = Stack_navigate(5, 3, STACK_KEY_RIGHT, true, true);
	assert(v.action == STACK_NAV_SWITCH_TAB && v.dir == 1);
	v = Stack_navigate(0, 0, STACK_KEY_RIGHT, true, true);
	assert(v.action == STACK_NAV_SWITCH_TAB && v.dir == 1);
	v = Stack_navigate(5, 3, STACK_KEY_RIGHT, false, true); // held: nothing
	assert(v.action == STACK_NAV_NONE && v.sel == 3);
	// game list: LEFT/RIGHT do nothing
	v = Stack_navigate(5, 3, STACK_KEY_LEFT, true, false);
	assert(v.action == STACK_NAV_NONE && v.sel == 3);
	// an out-of-range selection is clamped first
	v = Stack_navigate(5, 9, STACK_KEY_UP, true, true);
	assert(v.action == STACK_NAV_MOVE && v.sel == 3);
	// UP on the tab row lands on the last item
	assert(Stack_fromTabRow(5) == 4 && Stack_fromTabRow(1) == 0 && Stack_fromTabRow(0) == 0);
}

// The game-list side arrangement (§8f.4, landscape family): the doc's worked tables, then both screens.
// Vertical alignment Right (px at 1 px a dp): the 960 dp Backdrop's stack (150 wide at x 123, its caption 230 + 682,
// 48 dp margins) mirrored: the stack's right edge 48 from the screen's, the caption from the left margin 48 to 32 short
// of the stack. A start (where Left's box is drawn) moves its left edge in, unless that leaves under 40% of its width.
static void mirror_side(void) {
	int cx = 123, item_w = 150, cap_x = Stack_capXPx(cx, item_w, 1.0f), cap_w = 960 - 48 - cap_x;
	assert(cap_x == 230 && cap_w == 682);
	Stack_mirrorSide(960, &cx, &cap_x, &cap_w, 0);
	assert(cx == 837 && cap_x == 48 && cap_w == 682);
	assert((cx - item_w / 2) - (cap_x + cap_w) == 32); // the caption-to-stack gap, as on the left
	assert(960 - (cx + item_w / 2) == 48);			   // the stack's margin, as the left one
	// a start at 52 (the 144-wide box in the 150 slot): the left edge moves in, the right end stays
	cx = 123, cap_x = 230, cap_w = 682;
	Stack_mirrorSide(960, &cx, &cap_x, &cap_w, 52);
	assert(cap_x == 52 && cap_x + cap_w == 730);
	// a start that would leave under 40%: the plain mirror
	cx = 123, cap_x = 230, cap_w = 682;
	Stack_mirrorSide(960, &cx, &cap_x, &cap_w, 500);
	assert(cap_x == 48 && cap_w == 682);
}

static void side_geometry(void) {
	StackSide d;
	// 960 × 432 (body 344), both with 48 dp side margins: Backdrop 150 × 200, x 123, caption 230 (682); Carousel
	// 268 × 189, x 182, caption 348 (564)
	StackSizes s = Stack_gameSizes(STACK_GAME_BACKDROP, 344, 960, &d);
	assert(d.item_w == 150 && d.item_h == 200 && d.x == 123 && d.cap_x == 230 && d.cap_w == 682 && d.margin == 48);
	assert(!d.guarded);
	assert(near(s.scale, 0.5f, 1e-6f) && s.gap == 16 && s.cap == 0 && s.item_w == 150 && s.item_h == 200);
	assert(near(344 / 2.0f - s.item_h / 2 - s.gap, 56, 1e-4f)); // neighbour 1's peek, of its 100
	s = Stack_gameSizes(STACK_GAME_CAROUSEL, 344, 960, &d);
	assert(d.item_w == 268 && d.item_h == 189 && d.x == 182 && d.cap_x == 348 && d.cap_w == 564 && d.margin == 48 && !d.guarded);
	assert(near(s.scale, 0.62f, 1e-6f) && s.gap == 16 && s.cap == 0);
	// 840 × 336 (body 248) and 933 × 704 (body 616: the 320 and 240 caps)
	Stack_gameSizes(STACK_GAME_BACKDROP, 248, 840, &d);
	assert(d.item_w == 108 && d.item_h == 144 && d.x == 102 && d.cap_x == 188 && d.cap_w == 604);
	Stack_gameSizes(STACK_GAME_CAROUSEL, 248, 840, &d);
	assert(d.item_w == 193 && d.item_h == 136 && near(d.cap_x, 273, 1e-4f) && near(d.cap_w, 519, 1e-4f));
	Stack_gameSizes(STACK_GAME_BACKDROP, 616, 933, &d);
	assert(d.item_w == 240 && d.item_h == 320 && d.x == 168 && d.cap_x == 320 && d.cap_w == 565);
	Stack_gameSizes(STACK_GAME_CAROUSEL, 616, 933, &d);
	assert(d.item_w == 340 && d.item_h == 240 && d.cap_x == 420 && d.cap_w == 465);
	// want: the clamps
	assert(near(Stack_gameWant(STACK_GAME_BACKDROP, 100), 110, 1e-4f));
	assert(near(Stack_gameWant(STACK_GAME_BACKDROP, 1000), 320, 1e-4f));
	assert(near(Stack_gameWant(STACK_GAME_CAROUSEL, 100), 100, 1e-4f));
	assert(near(Stack_gameWant(STACK_GAME_CAROUSEL, 1000), 240, 1e-4f));

	// Brick (body 280 dp, 477.9 dp wide). Backdrop: want 162.4 → 122 × 162, 48 dp side margins: x 109, caption 202
	// (227.9 dp, 47.7%)
	s = Stack_gameSizes(STACK_GAME_BACKDROP, BRICK_BODY, BRICK_W, &d);
	assert(near(Stack_gameWant(STACK_GAME_BACKDROP, BRICK_BODY), 162.4f, 1e-3f));
	assert(d.item_w == 122 && d.item_h == 162 && d.x == 109 && d.cap_x == 202 && d.margin == 48 && !d.guarded);
	assert(near(d.cap_w, 227.87f, 0.01f) && d.cap_w >= 0.4f * BRICK_W);
	// in px: 261 × 347 (neighbour 131 × 174), x 234, caption 433 … 921 (488 px)
	assert(px(d.item_w, BRICK_PD) == 261 && px(d.item_h, BRICK_PD) == 347);
	// the caption's left edge from the drawn stack (px): 234 + 261/2 + 32 dp (68.57 px) = 433.07 → 433
	assert(Stack_capXPx(234, 261, BRICK_PD) == 433);
	assert(px(d.item_w * s.scale, BRICK_PD) == 131 && px(d.item_h * s.scale, BRICK_PD) == 174);
	assert(px(d.x, BRICK_PD) == 234 && px(d.cap_x, BRICK_PD) == 433 && 1024 - px(d.margin, BRICK_PD) == 921);
	assert(px(d.x - d.item_w / 2, BRICK_PD) == px(d.margin, BRICK_PD)); // left margin == the caption's right margin
	// Carousel: want 154 → 218 × 154 is wider than R − gutter (206.7): the guard takes it to 206 × 145, its side
	// margins only 24.36 dp each (48 would take the caption under 40%): x 127.36, caption 262.36 (exactly 40%)
	s = Stack_gameSizes(STACK_GAME_CAROUSEL, BRICK_BODY, BRICK_W, &d);
	assert(near(Stack_gameWant(STACK_GAME_CAROUSEL, BRICK_BODY), 154, 1e-3f));
	assert(d.guarded && d.item_w == 206 && d.item_h == 145);
	assert(near(d.x, 127.36f, 0.01f) && near(d.cap_x, 262.36f, 0.01f) && near(d.margin, 24.36f, 0.01f));
	assert(near(d.cap_w, 0.4f * BRICK_W, 0.01f));
	assert(d.x - d.item_w / 2 >= STACK_GUTTER_DP); // the item never starts off-screen
	assert(px(d.item_w, BRICK_PD) == 441 && px(d.item_h, BRICK_PD) == 311);
	assert(px(d.item_w * s.scale, BRICK_PD) == 274 && px(d.item_h * s.scale, BRICK_PD) == 193);
	assert(px(d.x, BRICK_PD) == 273 && px(d.cap_x, BRICK_PD) == 562 && 973 - px(d.cap_x, BRICK_PD) == 411);
	assert(Stack_capXPx(274, 441, BRICK_PD) == 563); // 274 + 220.5 + 68.57

	// SPS (body 425.6 dp, 896 dp wide). Backdrop: want 246.85 → 185 × 247, 48 dp side margins: x 140.5, caption 265
	// (583 dp, 65%)
	s = Stack_gameSizes(STACK_GAME_BACKDROP, SPS_BODY, SPS_W, &d);
	assert(d.item_w == 185 && d.item_h == 247 && near(d.x, 140.5f, 1e-3f) && d.cap_x == 265 && d.margin == 48);
	assert(near(d.cap_w, 583, 0.01f) && !d.guarded);
	assert(px(d.item_w, SPS_PD) == 264 && px(d.item_h, SPS_PD) == 353);
	assert(px(d.x, SPS_PD) == 201 && px(d.cap_x, SPS_PD) == 379 && 1280 - px(d.margin, SPS_PD) == 1211);
	assert(px(d.x - d.item_w / 2, SPS_PD) == px(d.margin, SPS_PD)); // left margin == the caption's right margin
	assert(Stack_capXPx(201, 264, SPS_PD) == 379);					// 201 + 132 + 45.71 = 378.71
	// Carousel: want 234.08 → 234, w = 234 · 340/240 = 331.5 → 332 (rounded up, in double), 48 dp side margins
	// (x 214, was 287 at 0.32 of the width), caption 412 (436 dp, 49%)
	s = Stack_gameSizes(STACK_GAME_CAROUSEL, SPS_BODY, SPS_W, &d);
	assert(d.item_w == 332 && d.item_h == 234 && d.x == 214 && d.cap_x == 412 && d.margin == 48 && !d.guarded);
	assert(near(d.cap_w, 436, 0.01f) && d.cap_w >= 0.4f * SPS_W);
	assert(px(d.item_w, SPS_PD) == 474 && px(d.item_h, SPS_PD) == 334);
	assert(px(d.item_w * s.scale, SPS_PD) == 294 && px(d.item_h * s.scale, SPS_PD) == 207);
	assert(px(d.x, SPS_PD) == 306 && px(d.cap_x, SPS_PD) == 589 && 1280 - px(d.margin, SPS_PD) - px(d.cap_x, SPS_PD) == 622);
	assert(px(d.x - d.item_w / 2, SPS_PD) == px(d.margin, SPS_PD)); // left margin == the caption's right margin
	assert(Stack_capXPx(410, 474, SPS_PD) == 693);					// 410 + 237 + 45.71 = 692.71
	// an odd item width's half px and the dp gap round once, together: 100 + 30.5 + 68.57 = 199.07 → 199 (rounding
	// each part first would give 100 + 31 + 69 = 200)
	assert(Stack_capXPx(100, 61, BRICK_PD) == 199);

	// the guard on a near-square window: 500 wide → R = 244, w ≤ 220 and x ≤ R − w/2; the caption keeps 40%
	d = Stack_sideGeom(240, 340, 240, 500);
	assert(d.guarded && d.item_w == 220 && d.item_h == 155 && near(d.x, 134, 1e-4f));
	assert(near(d.cap_w, 200, 1e-3f));
	// a tiny window (200 wide): R = 64, w = 40, h = 53, and x clamped into [24 + 20, 64 − 20] = 44 (0.32 · 200 = 64)
	d = Stack_sideGeom(110, 3, 4, 200);
	assert(d.guarded && d.item_w == 40 && d.item_h == 53 && near(d.x, 44, 1e-4f) && near(d.cap_w, 80, 1e-3f));
	// the selection is centred in the body (cap 0)
	assert(near(Stack_selectionY(BRICK_BODY, 0), 140, 1e-4f) && near(Stack_selectionY(SPS_BODY, 0), 212.8f, 1e-3f));
}

// The game-list Carousel's darkening layer over a tile (as the horizontal row's): 0.6 · t to one step, then
// min(0.85, 0.45 + 0.15 · d), toward black over 3..4.
static void carousel_darken(void) {
	StackSizes s = Stack_gameSizes(STACK_GAME_CAROUSEL, BRICK_BODY, BRICK_W, NULL);
	assert(near(Stack_item(&s, 0, 0).darken, 0, 1e-6f));
	assert(near(Stack_item(&s, 0.5f, 0).darken, 0.3f, 1e-5f));
	assert(near(Stack_item(&s, 1, 0).darken, 0.6f, 1e-5f));
	assert(near(Stack_item(&s, 2, 0).darken, 0.75f, 1e-5f));
	assert(near(Stack_item(&s, -3, 0).darken, 0.85f, 1e-5f));
	assert(near(Stack_item(&s, 3.5f, 0).darken, 0.85f + 0.15f * 0.5f, 1e-5f));
	assert(!Stack_item(&s, 4, 0).visible);
	// neighbour 1's offset: h/2 + g + h·s/2 (Brick: 72.5 + 16 + 44.95)
	assert(near(Stack_item(&s, 1, 0).dy, 145 / 2.0f + 16 + 145 * 0.62f / 2, 1e-3f));
}

// Consoles' "N games" belongs to its item: its top is 8 dp under the item's drawn logo, both scaled about the item's
// live centre, fading 1 − d over the step. Settled it is exactly 8 dp under the selected logo; during a slide it never
// sits on a logo (the outgoing one's count against the rising logo, the incoming one's against the next logo below).
#define COUNT_LINE_DP 24.0f // the 14 sp count's line (Brick ~50 px / 2.143, SPS ~34 px / 1.429), rounded up
static void count_on_item(void) {
	StackCount k = Stack_countOn(127, 1, 60, 8, 0);
	assert(near(k.top, 127 + 30 + 8, 1e-4f) && near(k.alpha, 1, 1e-6f) && near(k.scale, 1, 1e-6f));
	// Brick px, settled: centre 356, a 214 px logo, 8 dp = 17 px → 480 (the fixed-slot rule's own value)
	k = Stack_countOn(356, 1, 214, 17, 0);
	assert(Stack_round(k.top) == 356 + 107 + 17);
	assert(near(Stack_countOn(0, 1, 60, 8, 1).alpha, 0, 1e-6f) && near(Stack_countOn(0, 1, 60, 8, 0.25f).alpha, 0.75f, 1e-6f));
	assert(near(Stack_countOn(0, 0.5f, 60, 8, 1.5f).alpha, 0, 1e-6f));

	const float screens[2][2] = {{BRICK_W, BRICK_BODY}, {SPS_W, SPS_BODY}};
	const float drawn[3] = {100, 60, 30}; // a logo filling the slot's height, a mid one, a wide one
	for (int sc = 0; sc < 2; sc++) {
		StackSizes c = Stack_mainSizes(STACK_MAIN_CONSOLES, screens[sc][0]);
		float sel_y = Stack_selectionY(screens[sc][1], c.cap);
		for (int a = 0; a < 3; a++) {
			for (int b = 0; b < 3; b++) {
				// DOWN from item 0 to item 1 (UP is the same positions in reverse): pos 0 → 1
				for (int step = 0; step <= 100; step++) {
					float pos = step / 100.0f;
					StackItem out = Stack_item(&c, 0, pos), in = Stack_item(&c, 1, pos), next = Stack_item(&c, 2, pos);
					float out_cy = sel_y + out.dy, in_cy = sel_y + in.dy, next_cy = sel_y + next.dy;
					StackCount co = Stack_countOn(out_cy, out.scale, drawn[a], 8, out.d);
					StackCount ci = Stack_countOn(in_cy, in.scale, drawn[b], 8, in.d);
					// the count follows its item's live position
					assert(near(co.top, out_cy + (drawn[a] / 2 + 8) * out.scale, 1e-3f));
					assert(near(ci.top, in_cy + (drawn[b] / 2 + 8) * in.scale, 1e-3f));
					float in_logo_top = in_cy - drawn[b] * in.scale / 2;
					float next_logo_top = next_cy - 100 * next.scale / 2; // the tallest logo below
					if (co.alpha > 0)
						assert(co.top + COUNT_LINE_DP * co.scale <= in_logo_top + 1e-3f);
					if (ci.alpha > 0)
						assert(ci.top + COUNT_LINE_DP * ci.scale <= next_logo_top + 1e-3f);
					// and never above its own logo's bottom
					assert(co.top >= out_cy + drawn[a] * out.scale / 2 && ci.top >= in_cy + drawn[b] * in.scale / 2);
				}
			}
		}
	}
}

int main(void) {
	slot_tables();
	offsets_on_y();
	pad_reach();
	fades();
	selection_centre();
	visible_range();
	edge_fade();
	rounding();
	navigation();
	count_on_item();
	side_geometry();
	mirror_side();
	carousel_darken();
	printf("test_stack_model: all passed\n");
	return 0;
}
