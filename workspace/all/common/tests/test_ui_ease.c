#include "../ui/ui_ease.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(float a, float b, float eps) {
	return fabsf(a - b) <= eps;
}

static void bezier_endpoints_and_monotone(void) {
	assert(near(UI_easeStandard(0.0f), 0.0f, 1e-4f));
	assert(near(UI_easeStandard(1.0f), 1.0f, 1e-4f));
	assert(near(UI_easeStandard(-1.0f), 0.0f, 1e-4f)); // clamped
	assert(near(UI_easeStandard(2.0f), 1.0f, 1e-4f));
	float prev = 0;
	for (int i = 1; i <= 100; i++) {
		float v = UI_easeStandard(i / 100.0f);
		assert(v >= prev - 1e-5f);
		prev = v;
	}
}

static void bezier_known_points(void) {
	// cubic-bezier(.2,.8,.2,1) is a strong ease-out: well past half way at a quarter of the time
	assert(UI_easeStandard(0.25f) > 0.6f);
	assert(UI_easeStandard(0.5f) > 0.85f);
	// linear control points give the identity
	for (int i = 0; i <= 10; i++)
		assert(near(UI_cubicBezier(0.25f, 0.25f, 0.75f, 0.75f, i / 10.0f), i / 10.0f, 1e-3f));
}

static void eased_fade(void) {
	assert(near(UI_easedFadeAlpha(0.9f, 0.0f, 3.5f), 0.9f, 1e-5f));
	assert(near(UI_easedFadeAlpha(0.9f, 1.0f, 3.5f), 0.0f, 1e-5f));
	// zero slope at the far end: the last step is tiny
	assert(UI_easedFadeAlpha(0.9f, 0.95f, 3.5f) < 0.01f);
	// higher power stays darker for longer
	assert(UI_easedFadeAlpha(0.9f, 0.5f, 3.5f) > UI_easedFadeAlpha(0.9f, 0.5f, 2.0f));
}

static void linear_hold_fade(void) {
	assert(near(UI_linearHoldFadeAlpha(0.8f, 0, 20, 2), 0.8f, 1e-5f));
	assert(near(UI_linearHoldFadeAlpha(0.8f, 2, 20, 2), 0.8f, 1e-5f));
	assert(near(UI_linearHoldFadeAlpha(0.8f, 11, 20, 2), 0.4f, 1e-5f));
	assert(near(UI_linearHoldFadeAlpha(0.8f, 20, 20, 2), 0.0f, 1e-5f));
	assert(near(UI_linearHoldFadeAlpha(0.8f, 25, 20, 2), 0.0f, 1e-5f));
}

int main(void) {
	bezier_endpoints_and_monotone();
	bezier_known_points();
	eased_fade();
	linear_hold_fade();
	printf("test_ui_ease: ok\n");
	return 0;
}
