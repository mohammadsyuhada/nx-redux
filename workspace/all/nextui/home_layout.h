#ifndef HOME_LAYOUT_H
#define HOME_LAYOUT_H

#include <stdbool.h>

// Home's landscape-split geometry (dp floats), pin placement, focus moves, scroll. Pure, host-tested.

#define HOME_MAX_PINS 16

typedef enum { HOME_PIN_GAME,
			   HOME_PIN_TOOL } HomePinKind;
typedef struct {
	float x, y, w, h;
} HomeRect; // page coordinates (y = 0 at the screen top, unscrolled)
typedef struct {
	int row, col, span;
	HomeRect r;
} HomePinSlot;
typedef enum { HOME_MODE_FULL,
			   HOME_MODE_NO_PINS,
			   HOME_MODE_FRESH } HomeMode;
typedef struct {
	HomeMode mode;
	float gutter, gap, tw, tile_h, top_h, page_h;
	int cols, rows;
	HomeRect cont, card; // Continue and the stats card (card.w == 0 in FRESH; cont is the Pick-a-game card)
	int npins;
	HomePinSlot pins[HOME_MAX_PINS];
} HomeLayout;

// kinds[] is already ordered games-first. has_continue false and npins 0 → FRESH.
void HomeLayout_compute(float screen_w, float screen_h, bool has_continue, const HomePinKind* kinds, int npins,
						HomeLayout* out);

typedef enum { HOME_FOCUS_CONTINUE,
			   HOME_FOCUS_CARD,
			   HOME_FOCUS_PIN } HomeFocusArea;
typedef struct {
	HomeFocusArea area;
	int pin;
} HomeFocus;
typedef struct {
	HomeFocusArea last_top;
	int last_pin;
} HomeFocusMemory;
typedef enum { HOME_DIR_UP,
			   HOME_DIR_DOWN,
			   HOME_DIR_LEFT,
			   HOME_DIR_RIGHT } HomeDir;
typedef enum { HOME_MOVE_STAY,
			   HOME_MOVE_MOVED,
			   HOME_MOVE_EDGE_PREV,
			   HOME_MOVE_EDGE_NEXT } HomeMoveResult;

// §9.7 split navigation; updates *f and *mem; EDGE_* = the caller may switch tab (on a fresh press only).
HomeMoveResult HomeLayout_move(const HomeLayout* l, HomeFocus* f, HomeFocusMemory* mem, HomeDir dir);
// UP on the tab row wraps to the bottom of Home: the last pin. No pins: `from` (clamped) stays. Records the move in
// *mem as a move would (the top item it came from, the pin).
HomeFocus HomeLayout_bottomFrom(const HomeLayout* l, HomeFocus from, HomeFocusMemory* mem);
// Clamp a focus to the current layout (after a rebuild): a pin index past the end → the last pin, no pins → Continue.
HomeFocus HomeLayout_clampFocus(const HomeLayout* l, HomeFocus f);
// The scroll offset (dp) that keeps the focused pin's rect ± (ring 3 + pad 8) inside [bar_dp, screen_h − bar_dp],
// moving as little as possible from `current`; 0 for the top section; clamped to [0, max(0, page_h − screen_h)].
float HomeLayout_scrollFor(const HomeLayout* l, HomeFocus f, float screen_h, float bar_dp, float current);

#endif
