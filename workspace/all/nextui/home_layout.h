#ifndef HOME_LAYOUT_H
#define HOME_LAYOUT_H

#include <stdbool.h>

// Home B2 (docs/home-b2.md): the stats strip under the tabs, the top section (Continue and the tool squares) and the
// pinned games in rows below. Pure, host-tested.
//
// Units are Brick px (the mockup's): a screen at FIXED_SCALE s draws s / 3 px per Brick px, so the Brick (3x) is
// 1024 x 768 of them and the Smart Pro S (2x) 1920 x 1080. The page is laid out for its size: a screen under 1.6:1
// takes the Brick's arrangement, a wider one the Smart Pro S's 8-column grid.

#define HOME_MAX_PINS 16
#define HOME_MAX_TOP 8

#define HOME_EDGE 51.0f		// the content's left and right margin
#define HOME_GAP 30.0f		// between tiles
#define HOME_PIN_H 155.0f	// a pin row's height
#define HOME_BOTTOM 34.0f	// the last pin row's bottom to the hint bar
#define HOME_RADIUS 24.0f	// a tile's corners
#define HOME_RING 6.0f		// a lit game's ring, outside the tile
#define HOME_EDGE_W 2.0f	// a tile's edge, white 8%
#define HOME_TOP_MAX 372.0f // the wide layout's top section at most (1.2 x the Brick's 310)

typedef struct {
	float x, y, w, h;
} HomeRect; // page coordinates (y = 0 at the screen top, unscrolled)

typedef enum { HOME_TILE_CONTINUE,			  // Continue, or Pick a game
			   HOME_TILE_GAME,				  // a pinned game in the rows (ref: its index in the games)
			   HOME_TILE_TOOL,				  // a pinned tool's square (ref: its index in the tools)
			   HOME_TILE_MORE } HomeTileKind; // "+N" (ref: N), opens the Tools tab
typedef struct {
	HomeTileKind kind;
	int ref;
	HomeRect r;
} HomeTile;

typedef struct {
	bool wide;		 // the Smart Pro S arrangement
	int strip_lines; // 0 (fresh), 1 or 2
	float strip_x, strip_right, strip_base[2];
	float top_y, top_h;
	float square, glyph; // the tool squares' side and glyph (0: no tools)
	int ntop;
	HomeTile top[HOME_MAX_TOP]; // Continue first, then the squares in reading order
	int k;						// pins per row
	int npins;
	HomeTile pins[HOME_MAX_PINS]; // the rows' games, row by row
	float page_h;
} HomeLayout;

// W x H: the screen (Brick px); bar: the tab row's and the hint bar's height. ngames: the pinned games (Continue's own
// excluded), ntools: the pinned tools.
void HomeLayout_compute(float W, float H, float bar, int strip_lines, int ngames, int ntools, HomeLayout* out);

typedef enum { HOME_SEC_TOP,
			   HOME_SEC_PINS } HomeSection;
// The focus: its section and, per section, the tile it is (or was last) on; prev = the top tile last left (ties go back
// to it), -1 none.
typedef struct {
	HomeSection sec;
	int top, pin, prev;
} HomeFocus;
typedef enum { HOME_DIR_UP,
			   HOME_DIR_DOWN,
			   HOME_DIR_LEFT,
			   HOME_DIR_RIGHT } HomeDir;
typedef enum { HOME_MOVE_STAY,
			   HOME_MOVE_MOVED,
			   HOME_MOVE_EDGE_PREV, // past the left edge: the caller switches tab (a fresh press only)
			   HOME_MOVE_EDGE_NEXT,
			   HOME_MOVE_TABS } HomeMoveResult; // UP out of the top section: the tab row

// The D-pad on Home (docs/home-b2.md, D-pad on Home): in the top section a press goes to the nearest tile on that side
// that overlaps the current one across the move, ties to the tile last left, else the top-left one; DOWN goes to the
// first-row pin under the tile's centre. In the pins LEFT/RIGHT run along a row, UP/DOWN keep the column (a shorter
// last row takes its last pin), UP from the first row returns to the top tile last on.
HomeMoveResult HomeLayout_move(const HomeLayout* l, HomeFocus* f, HomeDir dir);
// UP on the tab row wraps to the last pin row: from Continue its first pin, from another top tile the last pin. No
// pins: the top section, unchanged.
void HomeLayout_fromTabs(const HomeLayout* l, HomeFocus* f);
// A focus kept in range after a rebuild (fewer tiles: the last one; no pins: the top section).
HomeFocus HomeLayout_clampFocus(const HomeLayout* l, HomeFocus f);
// The focused tile's rect.
HomeRect HomeLayout_focusRect(const HomeLayout* l, HomeFocus f);
// The page scroll (Brick px) keeping the focused pin's ring under the tab row and the pin HOME_BOTTOM above the hint
// bar, moving as little as possible from `current`; 0 in the top section; clamped to [0, page_h − H].
float HomeLayout_scrollFor(const HomeLayout* l, HomeFocus f, float H, float bar, float current);

#endif
