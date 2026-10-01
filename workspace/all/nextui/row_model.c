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
	case ROW_BACKDROP_LOGO:
		s.item_w = 330 * f, s.item_h = 130 * f, s.scale = 0.45f, s.gap = 28 * f;
		break;
	case ROW_BACKDROP_TOOL:
		s.item_w = 150 * f, s.item_h = 160 * f, s.gap = 14 * f;
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
		it.alpha = d <= 1 ? 1 - 0.5f * t : maxf(0.2f, 0.5f - 0.12f * (d - 1));
		if (d > 3)
			it.alpha *= clampf(ROW_HIDE_D - d, 0, 1);
	}
	return it;
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
