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

int main(void) {
	exit_begin();
	exit_runs_out();
	exit_key_finishes();
	exit_tick_wrap();
	cancel();
	rebase();
	printf("test_menu_transition: ok\n");
	return 0;
}
