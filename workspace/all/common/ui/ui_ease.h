#ifndef UI_EASE_H
#define UI_EASE_H

// Easing and fade curves. SDL-free so tests/test_ui_ease.c can run them on the host.

// CSS-style cubic-bezier(x1, y1, x2, y2): maps time t (clamped to [0,1]) to progress.
float UI_cubicBezier(float x1, float y1, float x2, float y2, float t);

// The standard menu ease, cubic-bezier(.2, .8, .2, 1).
float UI_easeStandard(float t);

// edge * (1 - t^power)^3, t = 0 at the dark edge (clamped to [0,1]).
float UI_easedFadeAlpha(float edge, float t, float power);

// edge while y_from_bottom <= hold, then linear down to 0 at band_h.
float UI_linearHoldFadeAlpha(float edge, float y_from_bottom, float band_h, float hold);

#endif // UI_EASE_H
