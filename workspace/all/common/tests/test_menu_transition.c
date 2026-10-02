#include "../../nextui/menu_transition.h"
#include "../ui/ui_ease.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(float a, float b, float e) {
	return fabsf(a - b) <= e;
}

// No picture on screen, or animations off: nothing starts and the caller closes the list at once.
static void exit_begin(void) {
	MenuTransitionExit x = {0};
	assert(!MenuTransition_exitBegin(&x, 1000, false, true) && !x.active);
	assert(!MenuTransition_exitBegin(&x, 1000, true, false) && !x.active);
	assert(!MenuTransition_exitStep(&x, 1000, true)); // idle: never asks for a close
	assert(near(MenuTransition_exitDarkness(&x, 1000), 0, 1e-6f));
	assert(MenuTransition_exitBegin(&x, 1000, true, true) && x.active);
}

// The fade darkens with UI_easeStandard over 350 ms, then the list closes, once.
static void exit_runs_out(void) {
	MenuTransitionExit x = {0};
	MenuTransition_exitBegin(&x, 5000, true, true);
	assert(near(MenuTransition_exitDarkness(&x, 5000), 0, 1e-4f));
	assert(near(MenuTransition_exitDarkness(&x, 5175), UI_easeStandard(0.5f), 1e-4f));
	float prev = 0;
	for (uint32_t t = 5000; t <= 5350; t += 10) {
		float d = MenuTransition_exitDarkness(&x, t);
		assert(d >= prev - 1e-5f && d <= 1.0f);
		prev = d;
	}
	assert(near(MenuTransition_exitDarkness(&x, 5350), 1, 1e-6f));
	assert(!MenuTransition_exitStep(&x, 5000, false));
	assert(!MenuTransition_exitStep(&x, 5349, false)); // still running: the frame takes no input
	assert(x.active);
	assert(MenuTransition_exitStep(&x, 5350, false)); // time up: close now
	assert(!x.active);
	assert(!MenuTransition_exitStep(&x, 5400, false)); // only once
	assert(near(MenuTransition_exitDarkness(&x, 5400), 0, 1e-6f));
}

// A key pressed mid-fade finishes it at once (the caller closes the list, then handles the key): one close, no more.
static void exit_key_finishes(void) {
	MenuTransitionExit x = {0};
	MenuTransition_exitBegin(&x, 100, true, true);
	assert(!MenuTransition_exitStep(&x, 150, false));
	assert(MenuTransition_exitStep(&x, 160, true));
	assert(!x.active);
	assert(!MenuTransition_exitStep(&x, 170, true)); // the next key is the new screen's alone
}

// SDL_GetTicks wraps after ~49 days: the elapsed time still counts forward.
static void exit_tick_wrap(void) {
	MenuTransitionExit x = {0};
	MenuTransition_exitBegin(&x, 0xFFFFFF00u, true, true);
	assert(!MenuTransition_exitStep(&x, 0x00000010u, false)); // 272 ms in
	assert(MenuTransition_exitDarkness(&x, 0x00000010u) > 0.9f);
	assert(MenuTransition_exitStep(&x, 0x00000060u, false)); // 352 ms in
}

static void cancel(void) {
	MenuTransitionExit x = {0};
	MenuTransition_exitBegin(&x, 0, true, true);
	MenuTransition_exitCancel(&x);
	assert(!x.active && !MenuTransition_exitStep(&x, 1000, false));
}

// A glide whose first frame took 200 ms (a new layout built its items) shows one frame's progress next, not 200 ms.
static void rebase(void) {
	assert(MenuTransition_rebaseStart(1000, 1016, MENU_TRANSITION_FRAME_MS) == 1000); // a normal frame: unchanged
	assert(MenuTransition_rebaseStart(1000, 1033, MENU_TRANSITION_FRAME_MS) == 1000);
	assert(MenuTransition_rebaseStart(1000, 1200, MENU_TRANSITION_FRAME_MS) == 1200 - 33);
	assert(MenuTransition_rebaseStart(0xFFFFFFF0u, 0x00000100u, 33) == 0x00000100u - 33); // across a wrap
	assert(MenuTransition_rebaseStart(0xFFFFFFF0u, 0x00000005u, 33) == 0xFFFFFFF0u);
}

// Tab focus: the content dims 1 -> 0.4 over 180 ms with UI_easeStandard, and back the same way.
static void dim_in_and_out(void) {
	MenuTransitionDim d = {0};
	assert(near(MenuTransition_dimAlpha(&d, 0), 1.0f, 1e-6f) && !MenuTransition_dimStep(&d, 0)); // zeroed: lit, idle
	MenuTransition_dimAim(&d, true, 1000, true);
	assert(near(MenuTransition_dimAlpha(&d, 1000), 1.0f, 1e-4f));
	assert(near(MenuTransition_dimAlpha(&d, 1090), 1.0f - 0.6f * UI_easeStandard(0.5f), 1e-4f));
	assert(near(MenuTransition_dimAlpha(&d, 1180), 0.4f, 1e-6f));
	float prev = 1.0f;
	for (uint32_t t = 1000; t <= 1180; t += 5) { // monotonic down
		float a = MenuTransition_dimAlpha(&d, t);
		assert(a <= prev + 1e-6f && a >= 0.4f - 1e-6f);
		prev = a;
	}
	// the keep-alive: true while it runs, once more on the frame its time is up, then false
	assert(MenuTransition_dimStep(&d, 1100));
	assert(MenuTransition_dimStep(&d, 1180));
	assert(!MenuTransition_dimStep(&d, 1196));
	assert(near(MenuTransition_dimAlpha(&d, 5000), 0.4f, 1e-6f)); // settled: stays at 40%
	// the same aim again changes nothing
	MenuTransition_dimAim(&d, true, 6000, true);
	assert(!d.active && near(MenuTransition_dimAlpha(&d, 6000), 0.4f, 1e-6f));
	// back to the content: 0.4 -> 1 over 180 ms, the same curve
	MenuTransition_dimAim(&d, false, 7000, true);
	assert(near(MenuTransition_dimAlpha(&d, 7000), 0.4f, 1e-4f));
	assert(near(MenuTransition_dimAlpha(&d, 7090), 0.4f + 0.6f * UI_easeStandard(0.5f), 1e-4f));
	assert(near(MenuTransition_dimAlpha(&d, 7180), 1.0f, 1e-6f));
}

// A reversal mid-way starts from where the dim is (no jump); animations off snaps.
static void dim_reverse_and_snap(void) {
	MenuTransitionDim d = {0};
	MenuTransition_dimAim(&d, true, 0, true);
	float mid = MenuTransition_dimAlpha(&d, 60);
	MenuTransition_dimAim(&d, false, 60, true);
	assert(near(MenuTransition_dimAlpha(&d, 60), mid, 1e-4f));
	assert(MenuTransition_dimAlpha(&d, 100) > mid);
	assert(near(MenuTransition_dimAlpha(&d, 240), 1.0f, 1e-6f));
	MenuTransitionDim s = {0};
	MenuTransition_dimAim(&s, true, 0, false);
	assert(!s.active && near(MenuTransition_dimAlpha(&s, 0), 0.4f, 1e-6f) && !MenuTransition_dimStep(&s, 0));
	// snapping a running tween to the aim it already has
	MenuTransitionDim r = {0};
	MenuTransition_dimAim(&r, true, 0, true);
	MenuTransition_dimAim(&r, true, 50, false);
	assert(!r.active && near(MenuTransition_dimAlpha(&r, 50), 0.4f, 1e-6f));
	// across a tick wrap
	MenuTransitionDim w = {0};
	MenuTransition_dimAim(&w, true, 0xFFFFFFA0u, true);
	assert(MenuTransition_dimAlpha(&w, 0x00000014u) > 0.4f);			 // 116 ms in, across the wrap: still moving
	assert(near(MenuTransition_dimAlpha(&w, 0x00000054u), 0.4f, 1e-6f)); // 180 ms in
}

int main(void) {
	dim_in_and_out();
	dim_reverse_and_snap();
	exit_begin();
	exit_runs_out();
	exit_key_finishes();
	exit_tick_wrap();
	cancel();
	rebase();
	printf("test_menu_transition: ok\n");
	return 0;
}
