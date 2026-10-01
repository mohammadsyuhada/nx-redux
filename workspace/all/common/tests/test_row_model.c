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
	assert(near(l.scale, 0.45f, 1e-4f) && near(l.item_w, 330 * l.f, 0.01f));
	RowSizes t = Row_sizes(ROW_BACKDROP_TOOL, 896, 504 - 2 * BAR);
	assert(near(t.gap, 14 * t.f, 0.01f) && near(t.item_h, 160 * t.f, 0.01f));
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

// Main-menu Carousel (sub-project 8): Consoles' logo slot 330 x 130 (sides 0.45), Collections' tool slot with sides at
// 0.5, Tools' tool slot unchanged.
static void main_menu_sizes(void) {
	float bh = 504 - 2 * BAR;
	RowSizes l = Row_sizes(ROW_BACKDROP_LOGO, 896, bh);
	assert(near(l.f, 896.0f / 960, 1e-4f));
	assert(near(l.item_w, 330 * l.f, 0.01f) && near(l.item_h, 130 * l.f, 0.01f) && near(l.scale, 0.45f, 1e-4f));
	assert(near(l.gap, 28 * l.f, 0.01f));
	RowSizes c = Row_sizes(ROW_BACKDROP_COLL, 896, bh);
	assert(near(c.item_w, 150 * c.f, 0.01f) && near(c.item_h, 160 * c.f, 0.01f) && near(c.gap, 14 * c.f, 0.01f));
	assert(near(c.scale, 0.5f, 1e-4f));
	RowSizes t = Row_sizes(ROW_BACKDROP_TOOL, 896, bh);
	assert(near(t.item_w, c.item_w, 1e-4f) && near(t.item_h, c.item_h, 1e-4f) && near(t.scale, 0.62f, 1e-4f));
	// the selected collection reads 2x its neighbours: half size one step out, the frameless alpha fades
	RowItem side = Row_item(&c, ROW_BACKDROP_COLL, 4, 3);
	assert(near(side.scale, 0.5f, 1e-4f) && near(side.alpha, 0.5f, 1e-4f) && near(side.darken, 0, 1e-6f));
	assert(near(side.dx, c.item_w / 2 + c.gap + c.item_w * 0.5f / 2, 1e-3f));
	RowItem far = Row_item(&c, ROW_BACKDROP_COLL, 6.5f, 3); // d = 3.5: the 3-4 step fade
	assert(far.visible && near(far.alpha, fmaxf(0.2f, 0.5f - 0.12f * 2.5f) * 0.5f, 1e-4f));
	assert(!Row_item(&c, ROW_BACKDROP_COLL, 7, 3).visible);
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
	// the logo slot exactly: Brick f = 477.87 / 960, 64.71 dp tall, its top at 146.84 dp; SPS 121.33 dp at 191.33 dp
	RowSizes l = Row_sizes(ROW_BACKDROP_LOGO, 1024 / pdOf(&SCREENS[0]), 280);
	assert(near(l.item_h, 64.71f, 0.01f) && near(Row_top(BAR, 280, l.item_h, 0, 0, 0), 146.84f, 0.01f));
	RowSizes ls = Row_sizes(ROW_BACKDROP_LOGO, 896, 504 - 2 * BAR);
	assert(near(ls.item_h, 121.33f, 0.01f) && near(Row_top(BAR, 504 - 2 * BAR, ls.item_h, 0, 0, 0), 191.33f, 0.01f));
}

// Consoles: "N games" 8 dp under the logo as drawn, its height from the logo's aspect in the slot.
static void consoles_count_y(void) {
	assert(near(Row_containH(330, 130, 4.2f), 330 / 4.2f, 1e-3f)); // a wide logo: the width binds
	assert(near(Row_containH(330, 130, 1.0f), 130, 1e-4f));		   // a square one: the slot's height
	assert(near(Row_containH(330, 130, 0), 130, 1e-4f));		   // no aspect: the slot
	assert(near(Row_logoCountY(100, 40, 8), 128, 1e-4f));
	const float centre[2] = {179.2f, 252.0f};
	// Mega Drive (4.20: the width binds), PrBoom (1.93, the squarest bundled logo: the slot's 130 f height binds), a
	// square logo (the height binds too)
	const float aspects[3] = {4.20f, 1.93f, 1.0f};
	const float brick_y[3] = {206.76f, 219.56f, 219.56f}, sps_y[3] = {296.67f, 320.67f, 320.67f};
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
	const float brick_w[4] = {57, 178, 179, 129}, sps_w[4] = {72, 224, 226, 162}; // My, Favourite, Handheld, Games
	const float brick_space = 11, sps_space = 14;
	for (int s = 0; s < 2; s++) {
		const Screen* sc = &SCREENS[s];
		float sw = sc->w / pdOf(sc), bh = sc->h / pdOf(sc) - 2 * BAR;
		RowSizes c = Row_sizes(ROW_BACKDROP_COLL, sw, bh);
		float k = fminf(1.0f, c.item_w / 150.0f), start = ROW_COLL_NAME_SP * k;
		int avail = dpPx(sc, c.item_w) - 2 * dpPx(sc, 8);
		const float* w = s == 0 ? brick_w : sps_w;
		float longest = 0;
		for (int i = 0; i < 4; i++)
			longest = fmaxf(longest, w[i]);
		float count_sp = Row_countSp(k);
		float sp = Row_collNameSp(start, longest, (float)avail, count_sp);
		float r = sp / start; // the widths at the chosen size
		if (s == 0) {
			// Brick: avail 126 px, count 10 sp; "Handheld" is 179 px at 14.9 sp and still 150 px at the 12.5 sp floor
			// (1.25 x the count, above 0.75 x the start): line 2 ends mid-word with "…" (accepted; Task 7 checks it)
			assert(avail == 126 && near(start, 14.93f, 0.01f) && near(count_sp, 10, 1e-4f) && near(sp, 12.5f, 1e-4f));
		} else {
			// SPS: avail 178 px; 28 sp -> 22 sp, where "Handheld" scales to 177.6 px (rowview then measures the rounded
			// 38 px font, 179 px, and takes one more step: 21 sp, 170 px)
			assert(avail == 178 && near(start, 28, 1e-3f) && near(count_sp, 13.07f, 0.01f) && near(sp, 22, 1e-4f));
		}
		assert(sp >= ROW_COLL_NAME_FLOOR * start - 1e-4f && sp <= start);
		// the name never reads smaller than its count: name_sp >= 1.25 x count_sp, for this long name and for a short
		// one that fits at the start, and in px after NX_SP's rounding
		assert(sp >= 1.25f * count_sp - 1e-4f);
		float short_sp = Row_collNameSp(start, 40, (float)avail, count_sp);
		assert(near(short_sp, start, 1e-4f) && short_sp >= 1.25f * count_sp);
		assert(spPx(sc, sp) > spPx(sc, count_sp));
		// the first line, filled greedily with whole words: "My" fits whole, "My Favourite" doesn't, so line 1 is the
		// whole word "My" (it never opens mid-word); the rest goes to line 2
		float space = (s == 0 ? brick_space : sps_space) * r, x = 0;
		int n = 0;
		for (int i = 0; i < 4; i++) {
			float add = (n ? space : 0) + w[i] * r;
			if (x + add > avail)
				break;
			x += add, n++;
		}
		assert(n == 1 && x <= avail);
	}
}

static void collections_text(void) {
	assert(Row_lineStep(32, ROW_COLL_LINE) == 37 && Row_lineStep(38, ROW_COLL_LINE) == 44);
	// slot px, the name sp (collections_long_name's), its font px, line step, gap px, count font px, then the 2-line
	// and 1-line name/count tops
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
		int count_px = spPx(sc, Row_countSp(fminf(1.0f, c.item_w / 150.0f))), count_h = (int)(count_px * 1.2f + 0.5f); // ~ the font height
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

int main(void) {
	box_slot_fits_caption();
	sizes();
	item_layout();
	visible_range_small();
	placement();
	shade_monotone();
	blur();
	main_menu_sizes();
	main_menu_centring();
	consoles_count_y();
	collections_name_size();
	collections_long_name();
	collections_text();
	printf("test_row_model: ok\n");
	return 0;
}
