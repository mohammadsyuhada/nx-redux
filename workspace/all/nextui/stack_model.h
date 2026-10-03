#ifndef STACK_MODEL_H
#define STACK_MODEL_H

#include <stdbool.h>

// stack_model.h — the Vertical orientation's stack (LIST-LAYOUT §8f.2): the horizontal row (row_model.h) stood on end,
// the same continuous position with offsets on Y. Geometry in dp, the d-pad's rules, and nothing else. SDL-free.
//
// The main-menu Carousel-Vertical (§8f.3) centres its slots; the game lists' stacks (§8f.4–6) use the side
// arrangement: the stack left of centre, the caption to its right (landscape family only: our two screens).

typedef enum { STACK_MAIN_CONSOLES, // the logo slot
			   STACK_MAIN_COLLECTIONS,
			   STACK_MAIN_TOOLS,				// also a Tools listing's slots in a game-list Backdrop
			   STACK_GAME_BACKDROP,				// box art over the picture (§8f.5)
			   STACK_GAME_CAROUSEL } StackKind; // the game tiles on black (§8f.6)

#define STACK_GUTTER_DP 24.0f	 // a slot's width is held to body width − 2 · this
#define STACK_EDGE_FADE_DP 20.0f // to transparent at the body's top and bottom (plain-black stacks)
#define STACK_SLIDE_MS 300		 // the position's tween (UI_easeStandard), as the row's
#define STACK_HIDE_D 4.0f		 // items fade out over 3..4 steps and are hidden from 4 on

// One stack's sizes (dp): the selected slot w × h, the neighbour scale, the gap between slots, and cap, the room kept
// under the selection (the selection and what hangs under it centred as one block). extra_up and extra_down: the
// selection's further reach above and below (a Consoles pad, Stack_padExtra; 0 for every other stack), which only the
// steps next to the selection make room for.
typedef struct {
	float item_w, item_h, scale, gap, cap;
	float extra_up, extra_down;
} StackSizes;

// The main-menu Carousel-Vertical's slots (§8f.3), unscaled (no small-screen factor), the width held to
// body_w − 2 · 24 dp: Consoles 330 × 100, s 0.5, gap 16, cap 26; Collections min(400, ·) × 100, s 0.5, gap 14;
// Tools 150 × 140, s 0.6, gap 10.
StackSizes Stack_mainSizes(StackKind k, float body_w);

// The game-list stacks' side arrangement (§8f.4, landscape family): h = round(want), w = round(h · ar), the stack's
// centre x = round(0.32 · width), the caption's left edge cap_x = x + w/2 + 32, its right margin 24. The guard keeps
// the caption at least 40% of the width: with R = width − 24 − 32 − 0.4 · width, a w > R − gutter becomes
// floor(R − gutter) and h = round(w / ar); x is clamped to [gutter + w/2, R − w/2]. All dp; ar = ar_w : ar_h (worked
// in double, as the mockup's JavaScript: 234 · 340/240 = 331.5 rounds up).
// Stack_gameSizes then gives both stacks equal side margins M = STACK_CAROUSEL_MARGIN_DP (48), held to what keeps the
// caption at 40% of the width and never under the 24 dp gutter: the stack's left edge at M (x = M + w/2), the
// caption's right margin M.
#define STACK_SIDE_X_SHARE 0.32f
#define STACK_SIDE_CAP_GAP_DP 32.0f
#define STACK_SIDE_CAP_MARGIN_DP 24.0f
#define STACK_SIDE_CAP_MIN_SHARE 0.4f
#define STACK_CAROUSEL_MARGIN_DP 48.0f // a game-list stack's left margin = its caption's right margin, at most
#define STACK_CAPTION_LINE_GAP_DP 3.0f // between the side caption's rows
typedef struct {
	float item_w, item_h; // the selected item
	float x;			  // the stack's centre, from the screen's left
	float cap_x, cap_w;	  // the caption's left edge and width (to the right margin)
	bool guarded;		  // the 40% guard took the width
	float margin;		  // the caption's right margin (dp): 24, a game list's its stack's left margin
} StackSide;
StackSide Stack_sideGeom(float want, int ar_w, int ar_h, float width);
// The caption's left edge on screen (px), from the drawn stack: the stack's centre cx_px + its selected item's half
// width item_w_px / 2 + 32 dp (at pd px a dp), rounded once (Stack_round).
int Stack_capXPx(int cx_px, int item_w_px, float pd);

// Equal side margins M (the game lists' Carousel and Backdrop): the stack's left edge at M (x = M + w/2), the
// caption's right margin M; M = STACK_CAROUSEL_MARGIN_DP, held to what keeps the caption at 40% of the width and
// never under the 24 dp gutter.
void Stack_equalMargins(StackSide* sd, float width);

// A game-list stack's wanted height (dp): Backdrop clamp(0.58 · body, 110, 320), Carousel clamp(0.55 · body, 100, 240).
float Stack_gameWant(StackKind k, float body_h);
// A game-list stack's sizes (cap 0: the selection centred in the body) and its side geometry: Backdrop ar 3:4,
// neighbours 0.5, gap 16; Carousel ar 340:240, neighbours 0.62, gap 16.
StackSizes Stack_gameSizes(StackKind k, float body_h, float width, StackSide* side);

// The selected slot's centre, from the body's top: body_h/2 − cap/2.
float Stack_selectionY(float body_h, float cap);

// The selection's extra reach (dp) for a pad of drawn height pad_h centred `off` below its item's centre (on the logo
// and its count line): the larger of its own reach (item_h/2 above, item_h/2 + cap below) and the pad's half ± off plus
// clear, minus its own; 0 when the pad stays inside it.
void Stack_padExtra(const StackSizes* s, float pad_h, float off, float clear, float* up, float* down);

// An item at `index` with the stack at `pos`: its centre's offset from the selection's centre (dy, down positive), its
// size (1 → scale over the first step), its alpha (Row_slotAlpha: the frameless fade) and whether it shows (d < 4).
// The step to neighbour 1 is h/2 + g + h·s/2 (plus extra_up above, extra_down below), then h·s + g per further step;
// items below the selection sit a further cap · min(1, index − pos) lower.
// darken: the game-list Carousel's black layer over a tile (Row_item's: 0.6·t up to one step, then min(0.85, 0.45 +
// 0.15·d), toward 1 over 3..4 steps); the frameless stacks use alpha instead.
typedef struct {
	float dy, scale, alpha, darken, d;
	bool visible;
} StackItem;
StackItem Stack_item(const StackSizes* s, float index, float pos);

// The items to draw: d < 4 and some of the item (at its size) inside the body [0, body_h], with the selection at
// Stack_selectionY. last < first when none (n == 0).
void Stack_visibleRange(const StackSizes* s, int n, float pos, float body_h, int* first, int* last);

// Consoles' "N games" on an item (§8f.3; device fix: the count belongs to its item, so it never sits over a logo
// sliding through the selection). Its top is `gap` under the item's logo as drawn (drawn_h at the selected size), both
// scaled with the item about its live centre item_cy; it shows on the items within a step of the position, fading
// with the distance d (1 − d), so the outgoing item's count fades out as the incoming one's fades in over the step.
// Settled (d 0, scale 1): exactly item_cy + drawn_h/2 + gap, at full opacity. Any unit.
typedef struct {
	float top, scale, alpha;
} StackCount;
StackCount Stack_countOn(float item_cy, float item_scale, float drawn_h, float gap, float d);

// The edge fade's opacity at y (from the body's top): 0 at the top and bottom edges, rising linearly to 1 over `fade`.
float Stack_edgeAlpha(float y, float body_h, float fade);

// The mockup's rounding (JavaScript Math.round): floor(v + 0.5), so 196.5 → 197.
int Stack_round(float v);

// The d-pad (§8f.2). UP/DOWN step one item and stop at the ends (a held key too: no wrap). On the main menu, UP on
// the first item (or on an empty tab) goes to the tab row on a fresh press only (held, it stops), and LEFT/RIGHT
// switch tab on a fresh press (held: nothing; L1/R1 stay the root's own). In a game list LEFT/RIGHT do nothing and UP
// on the first item does nothing.
typedef enum { STACK_KEY_UP,
			   STACK_KEY_DOWN,
			   STACK_KEY_LEFT,
			   STACK_KEY_RIGHT } StackKey;
typedef enum { STACK_NAV_NONE,		// nothing moves (the key is still the stack's)
			   STACK_NAV_MOVE,		// the selection moves to .sel
			   STACK_NAV_TAB_ROW,	// focus goes to the tab row
			   STACK_NAV_SWITCH_TAB // the tab steps by .dir (−1 / +1)
} StackNavAction;
typedef struct {
	StackNavAction action;
	int sel; // the selection after the key (unchanged unless MOVE)
	int dir; // SWITCH_TAB: −1 or +1
} StackNav;
StackNav Stack_navigate(int n, int sel, StackKey key, bool fresh, bool main_menu);

// UP on the tab row wraps to the stack's last item (§5, as the List): n − 1, or 0 for an empty tab.
int Stack_fromTabRow(int n);

#endif
