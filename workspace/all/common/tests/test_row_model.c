#include "../../nextui/row_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int near(float a, float b, float e) {
	return fabsf(a - b) <= e;
}
#define BAR (28.0f * 42.0f / 30.0f)

static void sizes(void) {
	float bh = 358 - 2 * BAR;
	assert(near(Row_factor(478, bh), 478.0f / 960, 1e-4f));
	RowSizes c = Row_sizes(ROW_CAROUSEL, 478, bh);
	assert(near(c.item_w, 340 * c.f, 0.01f) && near(c.item_h, 240 * c.f, 0.01f) && near(c.scale, 0.62f, 1e-4f));
	RowSizes b = Row_sizes(ROW_BACKDROP_BOX, 896, 504 - 2 * BAR);
	assert(b.item_h <= 150 * 1.3f + 0.01f && b.item_h >= 150 * b.f - 0.01f);
	assert(near(b.item_w / b.item_h, 170.0f / 150, 1e-3f));
	assert(b.item_h <= (504 - 2 * BAR) - 32 + 0.01f);
	RowSizes l = Row_sizes(ROW_BACKDROP_LOGO, 896, 504 - 2 * BAR);
	assert(near(l.scale, 0.40f, 1e-4f) && near(l.item_w, 460 * l.f, 0.01f));
	RowSizes t = Row_sizes(ROW_BACKDROP_TOOL, 896, 504 - 2 * BAR);
	assert(near(t.gap, 14 * t.f, 0.01f) && near(t.item_h, 208 * t.f, 0.01f) && near(t.item_w, 240 * t.f, 0.01f));
}

static void item_layout(void) {
	RowSizes s = {100, 80, 0.6f, 10, 1};
	RowItem a = Row_item(&s, ROW_CAROUSEL, 5, 5);
	assert(near(a.dx, 0, 1e-4f) && near(a.scale, 1, 1e-4f) && near(a.darken, 0, 1e-4f) && a.visible);
	float first = 50 + 10 + 30;
	RowItem b = Row_item(&s, ROW_CAROUSEL, 6, 5);
	assert(near(b.dx, first, 1e-3f) && near(b.scale, 0.6f, 1e-4f) && near(b.darken, 0.60f, 1e-4f));
	RowItem h = Row_item(&s, ROW_CAROUSEL, 5.5f, 5); // half a step
	assert(near(h.dx, first / 2, 1e-3f) && near(h.scale, 0.8f, 1e-4f) && near(h.darken, 0.30f, 1e-4f));
	RowItem c = Row_item(&s, ROW_CAROUSEL, 7, 5);
	assert(near(c.dx, first + 60 + 10, 1e-3f) && near(c.darken, 0.75f, 1e-4f));
	RowItem l = Row_item(&s, ROW_CAROUSEL, 1, 5); // left side: negative offset
	assert(l.dx < 0 && !l.visible);
	RowItem bd = Row_item(&s, ROW_BACKDROP_BOX, 7, 5);
	assert(near(bd.alpha, 0.5f - 0.12f, 1e-4f));
	RowItem far = Row_item(&s, ROW_BACKDROP_BOX, 8.5f, 5); // d = 3.5: half faded
	assert(far.visible && near(far.alpha, fmaxf(0.2f, 0.5f - 0.12f * 2.5f) * 0.5f, 1e-4f));
}

static void visible_range_small(void) {
	int f, l;
	Row_visibleRange(0, 0, &f, &l);
	assert(l < f);
	Row_visibleRange(1, 0, &f, &l);
	assert(f == 0 && l == 0);
	Row_visibleRange(100, 50.4f, &f, &l);
	assert(f == 47 && l == 54);
}

static void placement(void) {
	float top = Row_top(40, 280, 120, 4, 16, 50); // block = 4 + 120 + 4 + 16 + 50 = 194 → (280 − 194)/2 = 43
	assert(near(top, 40 + 43 + 4, 1e-3f));
	assert(near(Row_top(40, 100, 120, 4, 16, 50), 40 + 4, 1e-3f)); // short body: clamps under the header
	assert(near(Row_top(40, 280, 160, 0, 0, 0), 40 + 60, 1e-3f));  // no caption: the row alone
}

static void shade_monotone(void) {
	float keys[6][2] = {{0, .85f}, {.12f, .60f}, {.32f, .20f}, {.55f, .28f}, {.82f, .62f}, {1, .80f}};
	for (int k = 0; k < 6; k++)
		assert(near(Row_shade(keys[k][0]), keys[k][1], 1e-4f));
	for (int k = 0; k < 5; k++) { // no overshoot between keys
		float lo = fminf(keys[k][1], keys[k + 1][1]), hi = fmaxf(keys[k][1], keys[k + 1][1]);
		for (int i = 1; i < 50; i++) {
			float y = keys[k][0] + (keys[k + 1][0] - keys[k][0]) * i / 50.0f;
			float v = Row_shade(y);
			assert(v >= lo - 1e-4f && v <= hi + 1e-4f);
		}
	}
}

static void blur(void) {
	unsigned char a[9 * 9], t[9 * 9];
	memset(a, 0, sizeof(a));
	a[4 * 9 + 4] = 255;
	Row_boxBlurAlpha(a, t, 9, 9, 1, 1);
	assert(a[4 * 9 + 4] > 0 && a[4 * 9 + 4] < 255 && a[3 * 9 + 3] > 0 && a[0] == 0);
	int sum = 0;
	for (int i = 0; i < 81; i++)
		sum += a[i];
	assert(abs(sum - 255) <= 9); // mass roughly preserved (rounding)
}

// The Brick at 3x (device): the box slot grown to its 1.3x cap plus the three caption rows (62 + 2 x 49 px = 74.7 dp)
// overflowed the body by ~7 dp, so the third row ran under the hint bar. The slot now gives way to the caption.
static void box_slot_fits_caption(void) {
	float bh = 768 / (3 * 30.0f / 42.0f) - 2 * BAR; // 280 dp
	RowSizes b = Row_sizes(ROW_BACKDROP_BOX, 1024 / (3 * 30.0f / 42.0f), bh);
	assert(near(b.item_h, 195, 0.01f));
	float gap = 18 * b.f, cap = 160 / (3 * 30.0f / 42.0f);
	assert(4 + b.item_h + 4 + gap + cap > bh); // the overflow
	Row_fitBoxSlot(&b, ROW_BACKDROP_BOX, bh, 4, gap, cap);
	assert(near(4 + b.item_h + 4 + gap + cap, bh, 0.01f)); // the block fits exactly
	assert(near(b.item_w / b.item_h, 170.0f / 150, 1e-3f));
	float top = Row_top(BAR, bh, b.item_h, 4, gap, cap);
	assert(top + b.item_h + 4 + gap + cap <= BAR + bh + 0.01f); // nothing under the bar
	// room to spare: untouched
	RowSizes w = Row_sizes(ROW_BACKDROP_BOX, 896, 504 - 2 * BAR);
	RowSizes w0 = w;
	Row_fitBoxSlot(&w, ROW_BACKDROP_BOX, 504 - 2 * BAR, 4, 18 * w.f, 60);
	assert(w.item_h == w0.item_h && w.item_w == w0.item_w);
	// never below the spec-scaled size: a body too short clamps under the header instead
	RowSizes t = Row_sizes(ROW_BACKDROP_BOX, 478, 120);
	Row_fitBoxSlot(&t, ROW_BACKDROP_BOX, 120, 4, 18 * t.f, 200);
	assert(near(t.item_h, 150 * t.f, 0.01f));
	// other kinds are fixed sizes
	RowSizes c = Row_sizes(ROW_CAROUSEL, 478, bh), c0 = c;
	Row_fitBoxSlot(&c, ROW_CAROUSEL, bh, 4, 8, 500);
	assert(c.item_h == c0.item_h);
}


// The two screens (landscape): the Brick 1024x768 at FIXED_SCALE 3 and the Smart Pro S 1280x720 at FIXED_SCALE 2.
typedef struct {
	const char* name;
	int w, h, scale;
} Screen;
static const Screen SCREENS[2] = {{"brick", 1024, 768, 3}, {"sps", 1280, 720, 2}};
static float pdOf(const Screen* s) { // px per dp
	return s->scale * 30.0f / 42.0f;
}
static int dpPx(const Screen* s, float dp) { // NX_DPF
	return (int)(dp * s->scale * 30.0f / 42.0f + 0.5f);
}
static int spPx(const Screen* s, float sp) { // NX_SP
	return (int)(sp * s->scale * 12.0f / 14.0f + 0.5f);
}

// Main-menu Carousel (sub-project 8): Consoles' logo slot 460 x 182 (sides 0.40, 48 apart), Collections' tool slot twice as
// wide with sides at 0.5, Tools' 240 x 208.
static void main_menu_sizes(void) {
	float bh = 504 - 2 * BAR;
	RowSizes l = Row_sizes(ROW_BACKDROP_LOGO, 896, bh);
	assert(near(l.f, bh / 500, 1e-4f)); // the SPS body's height binds (425.6 / 500 under 896 / 960)
	assert(near(l.item_w, 460 * l.f, 0.01f) && near(l.item_h, 182 * l.f, 0.01f) && near(l.scale, 0.40f, 1e-4f));
	assert(near(l.gap, 48 * l.f, 0.01f));
	// sized by the screen alone: no 1.0 cap, so a roomy body (a small UI scale on a big panel) still scales it
	RowSizes big = Row_sizes(ROW_BACKDROP_LOGO, 1920, 1000);
	assert(near(big.f, 2.0f, 1e-4f) && near(big.item_w, 920, 0.01f));
	// the Brick's body is tall enough: the width binds there
	RowSizes br = Row_sizes(ROW_BACKDROP_LOGO, 1024 / (3 * 30.0f / 42.0f), 280);
	assert(near(br.f, 1024 / (3 * 30.0f / 42.0f) / 960, 1e-4f));
	RowSizes c = Row_sizes(ROW_BACKDROP_COLL, 896, bh);
	assert(near(c.item_w, 300 * c.f, 0.01f) && near(c.item_h, 160 * c.f, 0.01f) && near(c.gap, 14 * c.f, 0.01f));
	assert(near(c.scale, 0.5f, 1e-4f));
	RowSizes t = Row_sizes(ROW_BACKDROP_TOOL, 896, bh);
	assert(near(t.item_w, 240 * t.f, 0.01f) && near(t.item_h, 208 * t.f, 0.01f) && near(t.scale, 0.62f, 1e-4f));
	// the selected collection reads 2x its neighbours: half size one step out, the frameless alpha fades
	RowItem side = Row_item(&c, ROW_BACKDROP_COLL, 4, 3);
	assert(near(side.scale, 0.5f, 1e-4f) && near(side.alpha, 0.5f, 1e-4f) && near(side.darken, 0, 1e-6f));
	assert(near(side.dx, c.item_w / 2 + c.gap + c.item_w * 0.5f / 2, 1e-3f));
	RowItem far = Row_item(&c, ROW_BACKDROP_COLL, 6.5f, 3); // d = 3.5: the 3-4 step fade
	assert(far.visible && near(far.alpha, fmaxf(0.2f, 0.5f - 0.12f * 2.5f) * 0.5f, 1e-4f));
	assert(!Row_item(&c, ROW_BACKDROP_COLL, 7, 3).visible);
	// Consoles' side logos: 0.65 x the curve (0.325 one step out), the selected one unchanged, eased in over the step
	assert(near(Row_item(&l, ROW_BACKDROP_LOGO, 3, 3).alpha, 1.0f, 1e-6f));
	assert(near(Row_item(&l, ROW_BACKDROP_LOGO, 4, 3).alpha, 0.325f, 1e-4f));
	assert(near(Row_item(&l, ROW_BACKDROP_LOGO, 3.5f, 3).alpha, 0.75f * 0.825f, 1e-4f));
	assert(near(Row_item(&l, ROW_BACKDROP_LOGO, 5, 3).alpha, 0.38f * 0.65f, 1e-4f));
}

// Each main-menu Carousel row is centred alone between the tab row and the hint bar: no caption block, Consoles' logo
// slot included, on both screens.
static void main_menu_centring(void) {
	const RowKind kinds[3] = {ROW_BACKDROP_LOGO, ROW_BACKDROP_COLL, ROW_BACKDROP_TOOL};
	// the body's centre (dp): Brick 39.2 + 280 / 2, SPS 39.2 + 425.6 / 2
	const float centre[2] = {179.2f, 252.0f};
	for (int s = 0; s < 2; s++) {
		const Screen* sc = &SCREENS[s];
		float sw = sc->w / pdOf(sc), bh = sc->h / pdOf(sc) - 2 * BAR;
		assert(near(BAR + bh / 2, centre[s], 0.01f));
		for (int k = 0; k < 3; k++) {
			RowSizes z = Row_sizes(kinds[k], sw, bh);
			float top = Row_top(BAR, bh, z.item_h, 0, 0, 0);
			assert(near(top + z.item_h / 2, centre[s], 0.01f));
			assert(top >= BAR && top + z.item_h <= BAR + bh);
			// a caption gap with no caption reserves nothing
			assert(near(Row_top(BAR, bh, z.item_h, 0, 18 * z.f, 0), top, 1e-4f));
		}
	}
	// the logo slot exactly: Brick f = 477.87 / 960, 90.60 dp tall, its top at 133.90 dp; SPS f = 425.6 / 500, 154.92 dp
	// at 174.54 dp
	RowSizes l = Row_sizes(ROW_BACKDROP_LOGO, 1024 / pdOf(&SCREENS[0]), 280);
	assert(near(l.item_h, 90.60f, 0.01f) && near(Row_top(BAR, 280, l.item_h, 0, 0, 0), 133.90f, 0.01f));
	RowSizes ls = Row_sizes(ROW_BACKDROP_LOGO, 896, 504 - 2 * BAR);
	assert(near(ls.item_h, 154.92f, 0.01f) && near(Row_top(BAR, 504 - 2 * BAR, ls.item_h, 0, 0, 0), 174.54f, 0.01f));
}

// Consoles: "N games" 8 dp under the logo as drawn, its height from the logo's aspect in the slot.
static void consoles_count_y(void) {
	assert(near(Row_containH(330, 130, 4.2f), 330 / 4.2f, 1e-3f)); // a wide logo: the width binds
	assert(near(Row_containH(330, 130, 1.0f), 130, 1e-4f));		   // a square one: the slot's height
	assert(near(Row_containH(330, 130, 0), 130, 1e-4f));		   // no aspect: the slot
	assert(near(Row_logoCountY(100, 40, 8), 128, 1e-4f));
	const float centre[2] = {179.2f, 252.0f};
	// Mega Drive (4.20: the width binds), PrBoom (1.93, the squarest bundled logo: the slot's 182 f height binds), a
	// square logo (the height binds too)
	const float aspects[3] = {4.20f, 1.93f, 1.0f};
	const float brick_y[3] = {214.46f, 232.50f, 232.50f}, sps_y[3] = {306.61f, 337.46f, 337.46f};
	for (int s = 0; s < 2; s++) {
		const Screen* sc = &SCREENS[s];
		float sw = sc->w / pdOf(sc), bh = sc->h / pdOf(sc) - 2 * BAR;
		RowSizes l = Row_sizes(ROW_BACKDROP_LOGO, sw, bh);
		float prev = 0;
		for (int a = 0; a < 3; a++) {
			float drawn = Row_containH(l.item_w, l.item_h, aspects[a]);
			assert(drawn <= l.item_h + 1e-4f);
			float y = Row_logoCountY(centre[s], drawn, ROW_LOGO_COUNT_GAP_DP);
			assert(near(y, s == 0 ? brick_y[a] : sps_y[a], 0.02f));
			assert(y >= prev); // a taller logo pushes the count down (never past the slot's bottom + 8 dp)
			prev = y;
			// the count line (14 sp = 16.8 dp) stays inside the body, above the hint bar
			assert(y + Row_countSp(l.f) * 1.2f <= BAR + bh);
		}
	}
}

// Collections: the name from 30 sp x the slot scale, shrunk in whole sp until its longest word fits the slot less 8 dp
// each side (never below max(0.75 x the start, 1.25 x the count)), line height 1.15, at most 2 lines; the count line
// (max(10 sp, 14 sp x k)) reserved 6 dp (unscaled) under it on every item.
static void collections_name_size(void) {
	// the count: max(10 sp, 14 sp x k)
	assert(near(Row_countSp(1), 14, 1e-4f) && near(Row_countSp(0.9333f), 13.07f, 0.01f));
	assert(near(Row_countSp(0.4978f), 10, 1e-4f) && near(Row_countSp(0), 10, 1e-4f));
	// the floor: max(0.75 x the start, 1.25 x the count)
	assert(near(Row_collNameFloor(28, 13.07f), 21, 1e-3f) && near(Row_collNameFloor(14.93f, 10), 12.5f, 1e-3f));
	assert(near(Row_collNameSp(20, 100, 120, 10), 20, 1e-4f));		 // fits at the start: the start itself
	assert(near(Row_collNameSp(14.5f, 150, 125, 8), 12, 1e-4f));	 // 14.5 -> 14 -> 13 -> 12 (150 x 12 / 14.5 = 124.1)
	assert(near(Row_collNameSp(20, 400, 100, 10), 15, 1e-4f));		 // never below 0.75 x the start
	assert(near(Row_collNameSp(16, 200, 160, 8), 12, 1e-4f));		 // 16 -> 15 -> 14 -> 13 (162.5 > 160) -> the 12 floor
	assert(near(Row_collNameSp(14.5f, 150, 125, 10), 12.5f, 1e-4f)); // 13 overflows; 12 < 1.25 x 10: the count's floor
	assert(near(Row_collNameSp(10, 400, 100, 10), 12.5f, 1e-4f));	 // a floor above the start still wins
}

// "My Favourite Handheld Games" at 3x (the Brick) and 2x (SPS). The word widths are the system font's (font1.ttf,
// measured with PIL) at the start size, 38 px on the Brick and 48 px on the SPS, and scale with the size.
static void collections_long_name(void) {
	// My, Favourite, Handheld, Games: px measured at the 30 sp start (Brick 14.93 sp, SPS 28 sp), scaled to the start
	const float at30 = ROW_COLL_NAME_SP / 30.0f;
	const float brick_w[4] = {57 * at30, 178 * at30, 179 * at30, 129 * at30};
	const float sps_w[4] = {72 * at30, 224 * at30, 226 * at30, 162 * at30};
	const float brick_space = 11 * at30, sps_space = 14 * at30;
	for (int s = 0; s < 2; s++) {
		const Screen* sc = &SCREENS[s];
		float sw = sc->w / pdOf(sc), bh = sc->h / pdOf(sc) - 2 * BAR;
		RowSizes c = Row_sizes(ROW_BACKDROP_COLL, sw, bh);
		float k = fminf(1.0f, c.item_w / 300.0f), start = ROW_COLL_NAME_SP * k;
		int avail = dpPx(sc, c.item_w) - 2 * dpPx(sc, 8);
		const float* w = s == 0 ? brick_w : sps_w;
		float longest = 0;
		for (int i = 0; i < 4; i++)
			longest = fmaxf(longest, w[i]);
		float count_sp = Row_countSp(k);
		float sp = Row_collNameSp(start, longest, (float)avail, count_sp);
		float r = sp / start; // the widths at the chosen size
		if (s == 0) {
			// Brick: avail 286 px (the slot twice the tool slot's width), count 10 sp; "Handheld" (215 px) fits at the
			// 17.9 sp start
			assert(avail == 286 && near(start, 17.92f, 0.01f) && near(count_sp, 10, 1e-4f) && near(sp, start, 1e-4f));
		} else {
			// SPS: avail 378 px; "Handheld" (271 px) fits at the 33.6 sp start
			assert(avail == 378 && near(start, 33.6f, 1e-3f) && near(count_sp, 13.07f, 0.01f) && near(sp, start, 1e-4f));
		}
		assert(sp >= ROW_COLL_NAME_FLOOR * start - 1e-4f && sp <= start);
		// the name never reads smaller than its count: name_sp >= 1.25 x count_sp, for this long name and for a short
		// one that fits at the start, and in px after NX_SP's rounding
		assert(sp >= 1.25f * count_sp - 1e-4f);
		float short_sp = Row_collNameSp(start, 40, (float)avail, count_sp);
		assert(near(short_sp, start, 1e-4f) && short_sp >= 1.25f * count_sp);
		assert(spPx(sc, sp) > spPx(sc, count_sp));
		// the first line, filled greedily with whole words (it never opens mid-word): the Brick fits "My" only (with
		// "Favourite", 295 px > 286), the SPS "My Favourite" (372 px of 378); the rest goes to the next lines
		float space = (s == 0 ? brick_space : sps_space) * r, x = 0;
		int n = 0;
		for (int i = 0; i < 4; i++) {
			float add = (n ? space : 0) + w[i] * r;
			if (x + add > avail)
				break;
			x += add, n++;
		}
		assert(n == (s == 0 ? 1 : 2) && x <= avail);
	}
}

static void collections_text(void) {
	assert(Row_lineStep(32, ROW_COLL_LINE) == 37 && Row_lineStep(38, ROW_COLL_LINE) == 44);
	// slot px, a shrunk name's sp (the old narrow slot's long name), its font px, line step, gap px, count font px,
	// then the 2-line and 1-line name/count tops
	const struct {
		int slot_h;
		float sp;
		int name_px, step, gap, count_px, y2, c2, y1, c1;
	} want[2] = {{171, 12.5f, 32, 37, 13, 26, 26, 113, 45, 95}, {213, 22, 38, 44, 9, 22, 45, 142, 67, 120}};
	for (int s = 0; s < 2; s++) {
		const Screen* sc = &SCREENS[s];
		float sw = sc->w / pdOf(sc), bh = sc->h / pdOf(sc) - 2 * BAR;
		RowSizes c = Row_sizes(ROW_BACKDROP_COLL, sw, bh);
		int slot_h = dpPx(sc, c.item_h), name_px = spPx(sc, want[s].sp);
		int step = Row_lineStep(name_px, ROW_COLL_LINE), gap = dpPx(sc, ROW_COLL_COUNT_GAP_DP);
		int count_px = spPx(sc, Row_countSp(fminf(1.0f, c.item_w / 300.0f))), count_h = (int)(count_px * 1.2f + 0.5f); // ~ the font height
		assert(slot_h == want[s].slot_h && name_px == want[s].name_px && step == want[s].step);
		assert(gap == want[s].gap && count_px == want[s].count_px);
		RowCollText two = Row_collText(slot_h, 2, step, gap, count_h);
		assert(two.name_y == want[s].y2 && two.count_y == want[s].c2 && two.name_h == 2 * step);
		assert(two.count_y == two.name_y + two.name_h + gap);		   // 6 dp under the name
		assert(two.name_y >= 0 && two.name_y + two.block_h <= slot_h); // the group fits the slot
		RowCollText one = Row_collText(slot_h, 1, step, gap, count_h);
		assert(one.name_y == want[s].y1 && one.count_y == want[s].c1);
		RowCollText three = Row_collText(slot_h, 3, step, gap, count_h); // clamped to 2 lines
		assert(three.name_h == two.name_h && three.name_y == two.name_y);
		// centred as one group: the space above the name equals the space under the reserved count (± 1 px)
		int under = slot_h - (two.count_y + count_h);
		assert(abs(under - two.name_y) <= 1);
	}
}

// The Vertical stack's Collections slot (§8f.3, 100 dp): the name shrinks in whole sp until its block and the reserved
// count line fit. Brick (scale 3): slot 214 px, 6 dp gap 13 px, 2.571 px/sp, the 14 sp count's line ~50 px (36 px
// font). SPS (scale 2): slot 143 px, gap 9 px, 1.714 px/sp, count line ~34 px (24 px font).
static int two_lines(float sp, void* ctx) {
	(void)sp, (void)ctx;
	return 2;
}
static int one_line(float sp, void* ctx) {
	(void)sp, (void)ctx;
	return 1;
}
static int wraps_above(float sp, void* ctx) { // a name on two lines above *ctx sp, one line at or below it
	return sp > *(float*)ctx ? 2 : 1;
}
#define BRICK_PX_SP (3 * 12.0f / 14.0f)
#define SPS_PX_SP (2 * 12.0f / 14.0f)

static void collections_fit_slot(void) {
	// the block: lines × round(font × 1.15), the gap, the count line
	assert(Row_collBlockH(77, 2, 13, 50) == 2 * 89 + 13 + 50); // 30 sp on the Brick: 241 > 214, it spills
	assert(Row_collBlockH(77, 1, 13, 50) == 89 + 13 + 50);
	assert(Row_collBlockH(77, 5, 13, 50) == Row_collBlockH(77, 2, 13, 50)); // at most two lines
	// a long two-line name on the Brick: 30 → 25 sp (64 px, step 74: 148 + 13 + 50 = 211 ≤ 214; 26 sp is 217)
	float sp = Row_collFitSlotSp(30, 10, BRICK_PX_SP, 214, 13, 50, two_lines, NULL);
	assert(near(sp, 25, 1e-4f));
	assert(Row_collBlockH((int)floorf(sp * BRICK_PX_SP + 0.5f), 2, 13, 50) <= 214);
	assert(Row_collBlockH((int)floorf(26 * BRICK_PX_SP + 0.5f), 2, 13, 50) > 214);
	// with a 48 px count line (the other system font): still within 214
	sp = Row_collFitSlotSp(30, 10, BRICK_PX_SP, 214, 13, 48, two_lines, NULL);
	assert(Row_collBlockH((int)floorf(sp * BRICK_PX_SP + 0.5f), 2, 13, 48) <= 214 && sp >= 25);
	// a one-line name keeps its 30 sp (89 + 13 + 50 = 152)
	assert(near(Row_collFitSlotSp(30, 10, BRICK_PX_SP, 214, 13, 50, one_line, NULL), 30, 1e-4f));
	// a name that drops to one line as it shrinks stops there: two lines at 28+ (166 + 63 > 214), one at 27
	float at = 27.5f;
	assert(near(Row_collFitSlotSp(30, 10, BRICK_PX_SP, 214, 13, 50, wraps_above, &at), 27, 1e-4f));
	// SPS: a two-line name 30 → 25 sp (43 px, step 49: 98 + 9 + 34 = 141 ≤ 143; 26 sp is 147)
	sp = Row_collFitSlotSp(30, 10, SPS_PX_SP, 143, 9, 34, two_lines, NULL);
	assert(near(sp, 25, 1e-4f) && Row_collBlockH((int)floorf(sp * SPS_PX_SP + 0.5f), 2, 9, 34) <= 143);
	assert(near(Row_collFitSlotSp(30, 10, SPS_PX_SP, 143, 9, 34, one_line, NULL), 30, 1e-4f)); // 59 + 43 = 102
	// a fractional start steps to whole sp; nothing fits → the minimum
	assert(near(Row_collFitSlotSp(25.5f, 10, BRICK_PX_SP, 214, 13, 50, two_lines, NULL), 25, 1e-4f));
	assert(near(Row_collFitSlotSp(30, 10, BRICK_PX_SP, 60, 13, 50, two_lines, NULL), 10, 1e-4f));
}

// The Backdrop picture's dim is 65% (2026-10-02, was 50%), under the shade: at least 72% dark everywhere.
static void backdrop_dim(void) {
	assert(near(ROW_BACKDROP_DIM, 0.65f, 1e-6f));
	assert(near(Row_backdropGain(0.32f), 0.35f * 0.8f, 1e-4f)); // the shade's lightest key (0.20)
	assert(near(Row_backdropGain(0.0f), 0.35f * 0.15f, 1e-4f));
	assert(near(Row_backdropGain(1.0f), 0.35f * 0.2f, 1e-4f));
	for (int i = 0; i <= 100; i++)
		assert(Row_backdropGain(i / 100.0f) <= 0.28f + 1e-4f);
}

// Content-sized slots (Horizontal Tools and Collections): each item as wide as its content, one gap between neighbours'
// edges. Equal widths give Row_item's offsets exactly; unequal ones keep the edge gap at rest and slide linearly.
static float widthOf(int i, void* ctx) {
	return ((const float*)ctx)[i];
}
static void test_content_sized_dx(void) {
	RowSizes s = Row_sizes(ROW_BACKDROP_TOOL, 960, 358);
	float eq[9];
	for (int i = 0; i < 9; i++)
		eq[i] = s.item_w;
	float poss[] = {4, 4.25f, 4.5f, 4.9f, 5};
	for (int p = 0; p < 5; p++)
		for (int i = 0; i < 9; i++) {
			float dx = Row_itemDxVar(&s, (float)i, poss[p], 9, widthOf, eq);
			assert(fabsf(dx - Row_item(&s, ROW_BACKDROP_TOOL, (float)i, poss[p]).dx) < 0.01f);
		}
	float w[5] = {100, 60, 200, 80, 120}; // at rest on 2: the edge gap between every pair of neighbours
	for (int i = 1; i < 5; i++) {
		float a = Row_itemDxVar(&s, (float)(i - 1), 2, 5, widthOf, w), b = Row_itemDxVar(&s, (float)i, 2, 5, widthOf, w);
		float wa = w[i - 1] * (i - 1 == 2 ? 1 : s.scale), wb = w[i] * (i == 2 ? 1 : s.scale);
		assert(fabsf((b - wb / 2) - (a + wa / 2) - s.gap) < 0.01f);
	}
	assert(fabsf(Row_itemDxVar(&s, 2, 2, 5, widthOf, w)) < 0.01f); // the selection centred
	// halfway: halfway between the two rests
	float m = Row_itemDxVar(&s, 3, 2.5f, 5, widthOf, w);
	float r2 = Row_itemDxVar(&s, 3, 2, 5, widthOf, w), r3 = Row_itemDxVar(&s, 3, 3, 5, widthOf, w);
	assert(fabsf(m - (r2 + r3) / 2) < 0.01f);
}

// Collection names: a word longer than 5 characters starts a new line (never the first word; 2 lines at most, the rest
// staying on the second).
static void test_coll_break(void) {
	char l[2][256];
	assert(Row_collBreak("To Be Completed", l) == 2 && !strcmp(l[0], "To Be") && !strcmp(l[1], "Completed"));
	assert(Row_collBreak("Play Later", l) == 1 && !strcmp(l[0], "Play Later")); // "Later" is 5
	assert(Row_collBreak("Favorites", l) == 1 && !strcmp(l[0], "Favorites"));
	assert(Row_collBreak("Pokemon Games", l) == 1 && !strcmp(l[0], "Pokemon Games")); // the first word stays
	assert(Row_collBreak("Super Mario Collection", l) == 2 && !strcmp(l[0], "Super Mario") &&
		   !strcmp(l[1], "Collection"));
	assert(Row_collBreak("My Retro Achievements Picks", l) == 2 && !strcmp(l[0], "My Retro") &&
		   !strcmp(l[1], "Achievements Picks"));									  // 2 lines at most
	assert(Row_collBreak("  Spaced   Out  ", l) == 1 && !strcmp(l[0], "Spaced Out")); // runs of spaces collapse
	assert(Row_collBreak("", l) == 0);
}

// The width rule too: a line that would overflow max_w breaks before the word (short words wrap as well).
static int charW(const char* t, void* ctx) {
	(void)ctx;
	return (int)strlen(t) * 10; // 10 px a character
}
static void test_coll_break_fit(void) {
	char l[2][256];
	assert(Row_collBreakFit("Super Mario Bros Games Ever", l, charW, NULL, 120) == 2 &&
		   !strcmp(l[0], "Super Mario") && !strcmp(l[1], "Bros Games Ever"));						  // 12 chars max a line
	assert(Row_collBreakFit("To Be Completed", l, charW, NULL, 1000) == 2 && !strcmp(l[0], "To Be")); // the >5 rule
	assert(Row_collBreakFit("Play Later", l, charW, NULL, 1000) == 1);
	assert(Row_collBreakFit("Play Later", l, NULL, NULL, 0) == 1); // no measure: the >5 rule alone
}

int main(void) {
	test_coll_break_fit();
	test_coll_break();
	test_content_sized_dx();
	box_slot_fits_caption();
	sizes();
	item_layout();
	visible_range_small();
	placement();
	shade_monotone();
	backdrop_dim();
	blur();
	main_menu_sizes();
	main_menu_centring();
	consoles_count_y();
	collections_name_size();
	collections_long_name();
	collections_text();
	collections_fit_slot();
	printf("test_row_model: ok\n");
	return 0;
}
