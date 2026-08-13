#ifndef UI_LISTVIEW_H
#define UI_LISTVIEW_H

#include <stdbool.h>
#include "sdl.h"
#include "ui_list.h"

// One row of a ListView, filled by the caller's provider. Lazy: requested
// only for rows the widget needs this frame (visible rows, plus the
// selected row for pill sizing and any row probed for is_header during
// input/scroll walks). May be called more than once per row per frame;
// string pointers must stay valid until the next get_row call or frame
// end (a static scratch buffer per provider is fine).
typedef struct {
	const char* label;		// required; NULL treated as ""
	SDL_Surface* icon;		// optional; provider picks variant via `selected`
	const char* annotation; // optional screen-right secondary text, outside the pill
	const char* badge;		// optional in-pill right-aligned text; reserves pill width
	bool is_header;			// section header row: half-height gray label, not selectable
} ListViewRow;

typedef enum {
	LISTVIEW_NONE,
	LISTVIEW_ACTIVATED,
	LISTVIEW_BACK,
	LISTVIEW_MENU,
	LISTVIEW_BUTTON
} ListViewActionType;

typedef struct {
	ListViewActionType type;
	int index; // current selection (-1 when the list is empty)
	int btn;   // BTN_X or BTN_Y for LISTVIEW_BUTTON
} ListViewAction;

typedef struct {
	// -- config (caller fills; stable per screen/context) --
	const char* title; // menu bar title; NULL = caller draws its own chrome
	TTF_Font* font;	   // row font; NULL = font.large
	int count;		   // total rows, headers included
	void (*get_row)(void* ctx, int i, bool selected, ListViewRow* out);
	void* ctx;					// provider context (no globals needed)
	const void* list_id;		// content identity - change => glide snap + marquee
								// clear (see UI_listViewReset for the full reset)
	const char* empty_title;	// empty-state copy for count == 0; NULL = widget
								// draws no empty-state visual (caller may)
	const char* empty_subtitle; // optional second line
	const char* empty_y_label;	// optional Y-button label on the empty state
	char** empty_btn_pairs;		// optional centered button pairs for the empty
								// state (e.g. {"A", "NEW", NULL}); overrides the
								// default B/BACK (+ empty_y_label) set. Same
								// lifetime rule as hint_pairs.
	char** hint_pairs;			// UI_renderButtonHintBar pairs; NULL = caller draws.
								// The array must outlive the UI_listViewRender call
								// (function-scope or static; an if/else-branch-scoped
								// compound literal is dead by render time — UB).
	bool no_wrap;				// zero-init = wrapping ON
	int list_y_override;		// 0 = layout default; else band origin
	int max_width_override;		// 0 = layout default; else thumb-adjusted row width
	// -- state (widget-owned; zero-init valid) --
	int selected;
	int scroll; // first visible row index
	ListGlide glide;
	ScrollTextState marquee;
	// -- internal (widget-private; zero-init valid) --
	bool input_pending;	  // selection changed since last render
	const void* drawn_id; // identity last rendered (defensive guard)
	int drawn_count;
	int prev_selected; // selection last rendered (header pre-target)
	int last_visible;  // rows drawn last frame (LEFT/RIGHT page size)
} ListView;

// Input: UP/DOWN PAD_justRepeated, wraps unless no_wrap, skips headers.
// LEFT/RIGHT page by the visible window, clamped. A => ACTIVATED{index},
// B => BACK, MENU tap => MENU{index}, X/Y => BUTTON{btn,index}. Everything
// else untouched, NONE returned. When the list is empty, B/MENU/X/Y still
// report (index -1) so apps keep their empty-state actions, and A reports
// BUTTON{BTN_A, -1} (empty states may advertise an A action, e.g. "NEW").
ListViewAction UI_listViewHandleInput(ListView* v);

// Jump the selection to the start of the adjacent first-letter group, for fast
// navigation of long alphabetically-sorted lists (wired to L1/R1 by callers).
// dir > 0 -> first row of the next letter; dir < 0 -> first row of the previous
// letter. Returns true if the cursor moved; no-op at the ends or on an
// empty/single-row list. Only meaningful for label-sorted lists.
bool UI_listViewJumpInitial(ListView* v, int dir);

// Render one frame. Does NOT clear the screen - callers GFX_clear first.
void UI_listViewRender(ListView* v, SDL_Surface* screen);

// The app's single dirty term: pending input, travelling pill, or a marquee
// that needs a main-surface render. Steady GPU marquee scrolling is NOT
// busy - drive it with UI_listViewTickIdle from the idle branch.
bool UI_listViewBusy(ListView* v);

// Marquee mid-scroll or pre-scroll: for hosts (nextui) whose idle loop
// separates "needs GPU tick" from "needs full redraw".
bool UI_listViewMarqueeBusy(ListView* v);

// Idle marquee tick: activate-after-delay + animate. Call in the idle
// (non-dirty) branch of the main loop.
void UI_listViewTickIdle(ListView* v);

// Explicit content change: snap glide, selected=0, scroll=0, marquee clear.
// Callers that relocate the cursor after a content change call this first,
// then assign v->selected.
void UI_listViewReset(ListView* v, int count, const void* list_id);

#endif // UI_LISTVIEW_H
