#ifndef MENU_TRANSITION_H
#define MENU_TRANSITION_H

// Main-menu transition timing (SDL-free, host-tested in tests/test_menu_transition.c):
// - leaving a Backdrop game list with B: the outgoing screen (its last screenshot) fades to black over 0.35 s, then
//   the list closes. A key pressed during the fade finishes it: the list closes at once and that key is then handled
//   on the screen B returned to, so no key is lost or handled twice;
// - a glide whose first frame was slow (a tab of another layout builds its items on that frame) counts from one
//   frame's worth before its second frame, so it moves on screen instead of jumping to its end.

#include <stdbool.h>
#include <stdint.h>

#define MENU_TRANSITION_FADE_MS 350 // the Backdrop picture's fade in from / out to black (§8b.4)
#define MENU_TRANSITION_FRAME_MS 33 // the most a glide advances on the frame after a slow first frame (30 fps)

typedef struct {
	bool active;
	uint32_t start; // ms
} MenuTransitionExit;

// B on a Backdrop game list. Starts the fade when a picture is on screen and menu animations are on; returns false
// otherwise (no picture: the screen is already black; animations off): the caller closes the list now.
bool MenuTransition_exitBegin(MenuTransitionExit* x, uint32_t now, bool picture, bool animations);

// How dark the outgoing screen is: 0 while idle or at the start, eased (UI_easeStandard) up to 1 at 0.35 s.
float MenuTransition_exitDarkness(const MenuTransitionExit* x, uint32_t now);

// Once per frame, before input is handled. true when the list must close now (the fade's time is up, or a key was
// pressed this frame, which the caller then handles on the screen it returns to); the fade is then idle again.
// false while it runs (the caller handles no input this frame) or when it is idle.
bool MenuTransition_exitStep(MenuTransitionExit* x, uint32_t now, bool key_pressed);

void MenuTransition_exitCancel(MenuTransitionExit* x);

// A glide started at `start` (ms); this is its second frame, at `now`. Returns the start to count from: unchanged when
// the first frame took at most `cap` ms, else `now - cap`, so the glide shows `cap` ms of progress now.
uint32_t MenuTransition_rebaseStart(uint32_t start, uint32_t now, uint32_t cap);

#endif // MENU_TRANSITION_H
