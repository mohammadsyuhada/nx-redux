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

int main(void) {
	box_slot_fits_caption();
	sizes();
	item_layout();
	visible_range_small();
	placement();
	shade_monotone();
	blur();
	printf("test_row_model: ok\n");
	return 0;
}
