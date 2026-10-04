// stack_model.c — the Vertical orientation's stack geometry (dp) and d-pad rules (LIST-LAYOUT §8f.2–3). SDL-free.
#include "stack_model.h"
#include "row_model.h"
#include <math.h>

static float minf(float a, float b) {
	return a < b ? a : b;
}
static float maxf(float a, float b) {
	return a > b ? a : b;
}

StackSizes Stack_mainSizes(StackKind k, float body_w) {
	StackSizes s = {0, 0, 0.5f, 0, 0};
	switch (k) {
	case STACK_MAIN_CONSOLES:
		s.item_w = 330, s.item_h = 100, s.gap = 16, s.cap = 26;
		break;
	case STACK_MAIN_COLLECTIONS:
		s.item_w = 400, s.item_h = 100, s.gap = 14;
		break;
	default: // Tools
		s.item_w = 150, s.item_h = 140, s.scale = 0.6f, s.gap = 10;
		break;
	}
	s.item_w = maxf(0.0f, minf(s.item_w, body_w - 2 * STACK_GUTTER_DP));
	return s;
}

static int roundD(double v) {
	return (int)floor(v + 0.5);
}

StackSide Stack_sideGeom(float want, int ar_w, int ar_h, float width) {
	StackSide sd = {0, 0, 0, 0, 0, false};
	if (ar_w <= 0 || ar_h <= 0)
		return sd;
	double ar = (double)ar_w / ar_h;
	int h = roundD(want), w = roundD(h * ar);
	double R = (double)width - STACK_SIDE_CAP_MARGIN_DP - STACK_SIDE_CAP_GAP_DP - STACK_SIDE_CAP_MIN_SHARE * width;
	if (w > R - STACK_GUTTER_DP) {
		w = (int)floor(R - STACK_GUTTER_DP);
		if (w < 0)
			w = 0;
		h = roundD(w / ar);
		sd.guarded = true;
	}
	double x = roundD(STACK_SIDE_X_SHARE * width);
	double lo = STACK_GUTTER_DP + w / 2.0, hi = R - w / 2.0;
	if (x > hi)
		x = hi;
	if (x < lo)
		x = lo;
	sd.item_w = (float)w;
	sd.item_h = (float)h;
	sd.x = (float)x;
	sd.cap_x = (float)(x + w / 2.0 + STACK_SIDE_CAP_GAP_DP);
	sd.cap_w = (float)(width - STACK_SIDE_CAP_MARGIN_DP - (x + w / 2.0 + STACK_SIDE_CAP_GAP_DP));
	sd.margin = STACK_SIDE_CAP_MARGIN_DP;
	return sd;
}

int Stack_capXPx(int cx_px, int item_w_px, float pd) {
	return Stack_round((float)cx_px + item_w_px / 2.0f + STACK_SIDE_CAP_GAP_DP * pd);
}

void Stack_mirrorSide(int screen_w, int* cx_px, int* cap_x_px, int* cap_w_px, int start_px) {
	*cx_px = screen_w - *cx_px;
	int right = screen_w - *cap_x_px; // the column's right end, 32 dp short of the stack
	*cap_x_px = right - *cap_w_px;
	if (start_px > *cap_x_px && right - start_px >= 0.4f * *cap_w_px)
		*cap_x_px = start_px;
	*cap_w_px = right - *cap_x_px;
}

static float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

void Stack_equalMargins(StackSide* sd, float width) {
	// the stack's left = the caption's right, 48 dp where the caption keeps 40% of the width with it, less (never
	// under the 24 dp gutter) where it wouldn't
	double room = (double)width - sd->item_w - STACK_SIDE_CAP_GAP_DP - STACK_SIDE_CAP_MIN_SHARE * width;
	double m = room / 2 < STACK_CAROUSEL_MARGIN_DP ? room / 2 : STACK_CAROUSEL_MARGIN_DP;
	if (m < STACK_GUTTER_DP)
		m = STACK_GUTTER_DP;
	double x = m + sd->item_w / 2.0;
	sd->x = (float)x;
	sd->cap_x = (float)(x + sd->item_w / 2.0 + STACK_SIDE_CAP_GAP_DP);
	sd->cap_w = (float)(width - m - sd->cap_x);
	sd->margin = (float)m;
}

float Stack_gameWant(StackKind k, float body_h) {
	if (k == STACK_GAME_CAROUSEL)
		return clampf(0.55f * body_h, 100, 240);
	return clampf(0.58f * body_h, 110, 320);
}

StackSizes Stack_gameSizes(StackKind k, float body_h, float width, StackSide* side) {
	bool carousel = k == STACK_GAME_CAROUSEL;
	StackSide sd = Stack_sideGeom(Stack_gameWant(k, body_h), carousel ? 340 : 3, carousel ? 240 : 4, width);
	Stack_equalMargins(&sd, width);
	if (side)
		*side = sd;
	StackSizes s = {sd.item_w, sd.item_h, carousel ? 0.62f : 0.5f, 16, 0};
	return s;
}

float Stack_selectionY(float body_h, float cap) {
	return body_h / 2 - cap / 2;
}

void Stack_padExtra(const StackSizes* s, float pad_h, float off, float clear, float* up, float* down) {
	*up = maxf(0.0f, pad_h / 2 - off + clear - s->item_h / 2);
	*down = maxf(0.0f, pad_h / 2 + off + clear - (s->item_h / 2 + s->cap));
}

StackItem Stack_item(const StackSizes* s, float index, float pos) {
	StackItem it;
	float diff = index - pos, d = fabsf(diff), t = minf(d, 1.0f);
	float first = s->item_h / 2 + (diff < 0 ? s->extra_up : s->extra_down) + s->gap + s->item_h * s->scale / 2;
	float off = t * first + maxf(0.0f, d - 1) * (s->item_h * s->scale + s->gap);
	it.dy = diff < 0 ? -off : off + s->cap * t; // below the selection: the cap's room too, eased in over a step
	it.scale = 1 + (s->scale - 1) * t;
	it.alpha = Row_slotAlpha(d);
	it.darken = d <= 1 ? 0.60f * t : minf(0.85f, 0.45f + 0.15f * d);
	if (d > 3) // the tiles fade toward the black ground over 3..4
		it.darken += (1 - it.darken) * clampf(d - 3, 0, 1);
	it.d = d;
	it.visible = d < STACK_HIDE_D;
	return it;
}

static bool inBody(const StackSizes* s, float index, float pos, float body_h) {
	StackItem it = Stack_item(s, index, pos);
	if (!it.visible)
		return false;
	float cy = Stack_selectionY(body_h, s->cap) + it.dy, half = s->item_h * it.scale / 2;
	return cy + half > 0 && cy - half < body_h;
}

void Stack_visibleRange(const StackSizes* s, int n, float pos, float body_h, int* first, int* last) {
	*first = 0;
	*last = -1;
	if (n <= 0)
		return;
	// the candidates (d < 4), then trimmed from both ends to what reaches the body (offsets grow with the distance)
	int f, l;
	Row_visibleRange(n, pos, &f, &l);
	while (f <= l && !inBody(s, (float)f, pos, body_h))
		f++;
	while (l >= f && !inBody(s, (float)l, pos, body_h))
		l--;
	if (f > l)
		return;
	*first = f;
	*last = l;
}

StackCount Stack_countOn(float item_cy, float item_scale, float drawn_h, float gap, float d) {
	StackCount c;
	c.scale = item_scale;
	c.top = item_cy + (drawn_h / 2 + gap) * item_scale;
	d = fabsf(d);
	c.alpha = d < 1 ? 1 - d : 0.0f;
	return c;
}

float Stack_edgeAlpha(float y, float body_h, float fade) {
	if (!(fade > 0))
		return 1.0f;
	float a = minf(y, body_h - y) / fade;
	return a < 0 ? 0.0f : (a > 1 ? 1.0f : a);
}

int Stack_round(float v) {
	return (int)floorf(v + 0.5f);
}

StackNav Stack_navigate(int n, int sel, StackKey key, bool fresh, bool main_menu) {
	StackNav nav = {STACK_NAV_NONE, sel, 0};
	if (n <= 0) {
		nav.sel = 0;
		if (main_menu && fresh && key == STACK_KEY_UP)
			nav.action = STACK_NAV_TAB_ROW;
		else if (main_menu && fresh && (key == STACK_KEY_LEFT || key == STACK_KEY_RIGHT))
			nav.action = STACK_NAV_SWITCH_TAB, nav.dir = key == STACK_KEY_LEFT ? -1 : 1;
		return nav;
	}
	if (sel < 0)
		sel = 0;
	if (sel > n - 1)
		sel = n - 1;
	nav.sel = sel;
	switch (key) {
	case STACK_KEY_UP:
		if (sel > 0)
			nav.action = STACK_NAV_MOVE, nav.sel = sel - 1;
		else if (main_menu && fresh)
			nav.action = STACK_NAV_TAB_ROW;
		break;
	case STACK_KEY_DOWN:
		if (sel < n - 1)
			nav.action = STACK_NAV_MOVE, nav.sel = sel + 1;
		break;
	case STACK_KEY_LEFT:
	case STACK_KEY_RIGHT:
		if (main_menu && fresh)
			nav.action = STACK_NAV_SWITCH_TAB, nav.dir = key == STACK_KEY_LEFT ? -1 : 1;
		break;
	}
	return nav;
}

int Stack_fromTabRow(int n) {
	return n > 0 ? n - 1 : 0;
}
