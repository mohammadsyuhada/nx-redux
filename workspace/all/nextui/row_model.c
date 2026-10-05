// row_model.c — Carousel/Backdrop geometry (dp) and curves. SDL-free.
#include "row_model.h"
#include <math.h>

#define ROW_HIDE_D 4.0f // items at d >= 4 are hidden; they fade over 3..4
#define ROW_EPS 1e-4f

static float minf(float a, float b) {
	return a < b ? a : b;
}
static float maxf(float a, float b) {
	return a > b ? a : b;
}
static float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

float Row_factor(float body_w, float body_h) {
	return minf(1.0f, minf(body_w / 960.0f, body_h / 358.0f));
}

RowSizes Row_sizes(RowKind k, float body_w, float body_h) {
	float f = Row_factor(body_w, body_h);
	RowSizes s = {0, 0, 0.62f, 0, f};
	switch (k) {
	case ROW_CAROUSEL:
		s.item_w = 340 * f, s.item_h = 240 * f, s.gap = 16 * f;
		break;
	case ROW_BACKDROP_BOX: {
		float h = 150 * f;
		if (body_h - 32 > h)
			h = maxf(150 * f, minf(body_h - 32, 150 * 1.3f));
		s.item_h = h, s.item_w = h * 170.0f / 150.0f, s.gap = 28 * f;
		break;
	}
	case ROW_BACKDROP_LOGO: { // the main-menu Consoles' logo slot: side logos shrink further, so the selected one stands
		// out (460 x 182, about 40% over the spec's 330 x 130 for the handhelds; sides at 0.40 of it, 48 apart). Sized by
		// the screen alone: no 1.0 cap on the factor, so in px it is the same at every UI scale (rowview.c skips its
		// big-UI shrink for it too). Held by the body's height at 500 too: a 16:9 screen's extra width (the Smart Pro
		// S) would otherwise make it bigger than on the Brick for about the same height; the Brick stays width-bound.
		float fl = minf(body_w / 960.0f, body_h / 500.0f);
		s.f = fl;
		s.item_w = 460 * fl, s.item_h = 182 * fl, s.scale = 0.40f, s.gap = 48 * fl;
		break;
	}
	case ROW_BACKDROP_TOOL: // 1.6x the spec's 150 wide (long names) and 1.3x its 160 tall, its icon and name 1.3x
		s.item_w = 240 * f, s.item_h = 208 * f, s.gap = 14 * f;
		break;
	case ROW_BACKDROP_COLL: // the tool slot twice as wide (long names), with side items at half size: the selected name
		// reads 2x its neighbours
		s.item_w = 300 * f, s.item_h = 160 * f, s.scale = 0.5f, s.gap = 14 * f;
		break;
	}
	return s;
}

void Row_fitBoxSlot(RowSizes* s, RowKind k, float body_h, float room, float caption_gap, float caption_h) {
	if (!s || k != ROW_BACKDROP_BOX)
		return;
	float avail = body_h - 2 * room - (caption_h > 0 ? caption_gap + caption_h : 0);
	if (s->item_h <= avail)
		return;
	float h = maxf(150 * s->f, avail);
	if (h >= s->item_h)
		return;
	s->item_h = h, s->item_w = h * 170.0f / 150.0f;
}

RowItem Row_item(const RowSizes* s, RowKind k, float index, float pos) {
	RowItem it = {0, 1, 0, 1, false};
	float diff = index - pos, d = fabsf(diff), t = minf(d, 1.0f);
	float first = s->item_w / 2 + s->gap + s->item_w * s->scale / 2;
	float off = t * first + maxf(0.0f, d - 1) * (s->item_w * s->scale + s->gap);
	it.dx = diff < 0 ? -off : off;
	it.scale = 1 + (s->scale - 1) * t;
	it.visible = d < ROW_HIDE_D;
	if (k == ROW_CAROUSEL) {
		it.darken = d <= 1 ? 0.60f * t : minf(0.85f, 0.45f + 0.15f * d);
		if (d > 3) // fade toward black over 3..4
			it.darken += (1 - it.darken) * clampf(d - 3, 0, 1);
	} else {
		it.alpha = Row_slotAlpha(d);
		if (k == ROW_BACKDROP_LOGO) // Consoles' side logos a step dimmer than the curve, eased in over the first step
			it.alpha *= 1 - (1 - ROW_LOGO_SIDE_ALPHA) * t;
	}
	return it;
}

float Row_slotAlpha(float d) {
	d = fabsf(d);
	float t = minf(d, 1.0f);
	float a = d <= 1 ? 1 - 0.5f * t : maxf(0.2f, 0.5f - 0.12f * (d - 1));
	if (d > 3)
		a *= clampf(ROW_HIDE_D - d, 0, 1);
	return a;
}

void Row_visibleRange(int n, float pos, int* first, int* last) {
	int f = (int)ceilf(pos - ROW_HIDE_D + ROW_EPS), l = (int)floorf(pos + ROW_HIDE_D - ROW_EPS);
	*first = f < 0 ? 0 : f;
	*last = l > n - 1 ? n - 1 : l;
}

float Row_top(float body_top, float body_h, float row_h, float room, float caption_gap, float caption_h) {
	float block = room + row_h + room + (caption_h > 0 ? caption_gap + caption_h : 0);
	return body_top + maxf(0.0f, (body_h - block) / 2) + room;
}

float Row_containH(float box_w, float box_h, float aspect) {
	if (!(aspect > 0))
		return box_h;
	return minf(box_h, box_w / aspect);
}

float Row_logoCountY(float row_cy, float drawn_h, float gap) {
	return row_cy + drawn_h / 2 + gap;
}

float Row_countSp(float k) {
	return maxf(ROW_COUNT_MIN_SP, ROW_COUNT_SP * k);
}

float Row_collNameFloor(float start_sp, float count_sp) {
	return maxf(ROW_COLL_NAME_FLOOR * start_sp, ROW_COLL_NAME_OVER_COUNT * count_sp);
}

float Row_collNameSp(float start_sp, float word_w, float avail, float count_sp) {
	float floor_sp = Row_collNameFloor(start_sp, count_sp);
	if (!(start_sp > 0) || !(avail > 0))
		return maxf(start_sp, floor_sp);
	// start_sp itself, then whole-sp steps (Tiles_fitWordsSp's sequence: no fractional font sizes below the start)
	for (float sp = start_sp; sp > floor_sp; sp = (sp == start_sp) ? ceilf(start_sp) - 1.0f : sp - 1.0f) {
		if (word_w * sp / start_sp <= avail)
			return sp;
	}
	return floor_sp;
}

int Row_lineStep(int font_px, float line) {
	return (int)floorf(font_px * line + 0.5f);
}

RowCollText Row_collText(int slot_h, int lines, int line_h, int gap, int count_h) {
	lines = lines < 1 ? 1 : (lines > ROW_COLL_LINES ? ROW_COLL_LINES : lines);
	RowCollText t;
	t.name_h = lines * line_h;
	t.block_h = t.name_h + gap + count_h;
	t.name_y = (slot_h - t.block_h) / 2;
	t.count_y = t.name_y + t.name_h + gap;
	return t;
}

int Row_collBlockH(int font_px, int lines, int gap, int count_h) {
	lines = lines < 1 ? 1 : (lines > ROW_COLL_LINES ? ROW_COLL_LINES : lines);
	return lines * Row_lineStep(font_px, ROW_COLL_LINE) + gap + count_h;
}

float Row_collFitSlotSp(float sp, float min_sp, float px_per_sp, int slot_h, int gap, int count_h,
						int (*lines_at)(float sp, void* ctx), void* ctx) {
	for (float s = sp; s >= min_sp; s = (s == sp) ? ceilf(sp) - 1.0f : s - 1.0f) {
		int lines = lines_at ? lines_at(s, ctx) : 1;
		if (Row_collBlockH((int)floorf(s * px_per_sp + 0.5f), lines, gap, count_h) <= slot_h)
			return s;
	}
	return min_sp;
}

// Fritsch–Carlson monotone cubic through the shade keys.
#define SHADE_N 6
static const float shade_x[SHADE_N] = {0, .12f, .32f, .55f, .82f, 1};
static const float shade_y[SHADE_N] = {.85f, .60f, .20f, .28f, .62f, .80f};

static void shade_tangents(float* m) {
	float delta[SHADE_N - 1];
	for (int k = 0; k < SHADE_N - 1; k++)
		delta[k] = (shade_y[k + 1] - shade_y[k]) / (shade_x[k + 1] - shade_x[k]);
	m[0] = delta[0];
	m[SHADE_N - 1] = delta[SHADE_N - 2];
	for (int k = 1; k < SHADE_N - 1; k++)
		m[k] = delta[k - 1] * delta[k] <= 0 ? 0 : (delta[k - 1] + delta[k]) / 2;
	for (int k = 0; k < SHADE_N - 1; k++) {
		if (delta[k] == 0) {
			m[k] = m[k + 1] = 0;
			continue;
		}
		float a = m[k] / delta[k], b = m[k + 1] / delta[k], s = a * a + b * b;
		if (s > 9) {
			float tau = 3 / sqrtf(s);
			m[k] = tau * a * delta[k];
			m[k + 1] = tau * b * delta[k];
		}
	}
}

float Row_shade(float y_frac) {
	float m[SHADE_N]; // recomputed per call (a few flops): no shared state across threads
	shade_tangents(m);
	float x = clampf(y_frac, 0, 1);
	int k = 0;
	while (k < SHADE_N - 2 && x > shade_x[k + 1])
		k++;
	float h = shade_x[k + 1] - shade_x[k], t = (x - shade_x[k]) / h, t2 = t * t, t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * shade_y[k] + (t3 - 2 * t2 + t) * h * m[k] + (-2 * t3 + 3 * t2) * shade_y[k + 1] +
		   (t3 - t2) * h * m[k + 1];
}

float Row_backdropGain(float y_frac) {
	return (1.0f - ROW_BACKDROP_DIM) * (1.0f - Row_shade(y_frac));
}

// One running-sum box pass along a line of `len` samples spaced `stride` apart; zero outside the edges.
static void box_line(const unsigned char* src, unsigned char* dst, int len, int stride, int r) {
	int win = 2 * r + 1, sum = 0;
	for (int i = 0; i <= r && i < len; i++)
		sum += src[i * stride];
	for (int i = 0; i < len; i++) {
		dst[i * stride] = (unsigned char)((sum + win / 2) / win);
		int add = i + r + 1, sub = i - r;
		if (add < len)
			sum += src[add * stride];
		if (sub >= 0)
			sum -= src[sub * stride];
	}
}

void Row_boxBlurAlpha(unsigned char* a, unsigned char* tmp, int w, int h, int r, int passes) {
	if (!a || !tmp || w <= 0 || h <= 0 || r <= 0)
		return;
	for (int p = 0; p < passes; p++) {
		for (int y = 0; y < h; y++) // horizontal: a → tmp
			box_line(a + y * w, tmp + y * w, w, 1, r);
		for (int x = 0; x < w; x++) // vertical: tmp → a
			box_line(tmp + x, a + x, h, w, r);
	}
}
