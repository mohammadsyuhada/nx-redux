// ui_ease.c — easing and fade curves (SDL-free, host-tested by tests/test_ui_ease.c)
#include "ui_ease.h"
#include <math.h>

static float clamp01(float v) {
	return v < 0 ? 0 : (v > 1 ? 1 : v);
}

// One axis of a cubic bezier with P0 = 0 and P3 = 1.
static float bez(float p1, float p2, float s) {
	float u = 1 - s;
	return 3 * u * u * s * p1 + 3 * u * s * s * p2 + s * s * s;
}
static float bezDeriv(float p1, float p2, float s) {
	float u = 1 - s;
	return 3 * u * u * p1 + 6 * u * s * (p2 - p1) + 3 * s * s * (1 - p2);
}

float UI_cubicBezier(float x1, float y1, float x2, float y2, float t) {
	t = clamp01(t);
	float s = t;
	for (int i = 0; i < 8; i++) { // Newton on x(s) = t
		float d = bezDeriv(x1, x2, s);
		if (fabsf(d) < 1e-6f)
			break;
		float next = s - (bez(x1, x2, s) - t) / d;
		if (next < 0 || next > 1)
			break;
		s = next;
	}
	if (fabsf(bez(x1, x2, s) - t) > 1e-4f) { // bisection fallback
		float lo = 0, hi = 1;
		for (int i = 0; i < 30; i++) {
			s = (lo + hi) / 2;
			if (bez(x1, x2, s) < t)
				lo = s;
			else
				hi = s;
		}
	}
	return bez(y1, y2, s);
}

float UI_easeStandard(float t) {
	return UI_cubicBezier(0.2f, 0.8f, 0.2f, 1.0f, t);
}

float UI_easedFadeAlpha(float edge, float t, float power) {
	float k = 1 - powf(clamp01(t), power);
	return edge * k * k * k;
}

float UI_linearHoldFadeAlpha(float edge, float y_from_bottom, float band_h, float hold) {
	if (y_from_bottom <= hold)
		return edge;
	if (band_h <= hold || y_from_bottom >= band_h)
		return 0;
	return edge * (1 - (y_from_bottom - hold) / (band_h - hold));
}
