#ifndef HOME_CAROUSEL_LAYOUT_H
#define HOME_CAROUSEL_LAYOUT_H

#include <stdbool.h>

#include "home_layout.h"

// Home's Carousel layout (Layouts > Home layout: Carousel; the mockup's homeCarGeom, nx-showcase.html): the stats
// strip on centred lines under the tab row, one row that opens on Continue with the pinned games after it (the game
// list Carousel's tiles, sized to the body) and the pinned tools in a dock of small squares under it. Pure, host-tested
// (common/tests/test_home_carousel_layout.c).
//
// Units are Home's (home_layout.h's Brick px at Home's scale): the Brick is 1536 x 1152 of them, the Smart Pro S
// 2560 x 1440, the tab row and the hint bar 126 each on both.

#define HOMECAR_SHAPE_W 340.0f // the game-list Carousel's tile shape (row_model.c ROW_CAROUSEL)
#define HOMECAR_SHAPE_H 240.0f
#define HOMECAR_SIDE_SCALE 0.62f   // the neighbours, as ROW_CAROUSEL's
#define HOMECAR_MARGIN 12.0f	   // the least of each equal space (see HomeCar_compute; the mockup's 4 dp)
#define HOMECAR_DOCK_GAP 33.0f	   // the row's ring room to the dock (the mockup's 12 dp)
#define HOMECAR_DOCK_SQ 117.0f	   // a tool square's side (the mockup's 44 dp on its 432 dp screen)
#define HOMECAR_DOCK_SPACING 32.0f // between the squares (the mockup's 12 dp)
#define HOMECAR_DOCK_GLYPH 0.58f   // the glyph, of a square's side (as Grid's squares)
#define HOMECAR_MIN_H 160.0f	   // the tile's height at the least, whatever the body leaves

// What HomeCar_compute takes beyond the screen and the counts.
typedef struct {
	float strip_k, text_k; // as HomeLayoutOpts': the strip's line step (Grid's 36) × strip_k × text_k
	float strip_asc;	   // the strip font's ascent: its first line's top above its baseline
	float strip_desc;	   // the strip font's descent: its last line's bottom under its baseline
	float tile_max_h;	   // the centre tile's height at most (the game-list Carousel's centre); 0: no cap
	float row_gap;		   // between neighbouring tiles (the game-list Carousel's gap)
	float tabs_bottom;	   // the tab row's underline's bottom, where the first space starts; 0: bar
	bool no_continue;	   // no Continue: item 0 is Pick a game (alone, with no pinned games: Grid's card, see below)
} HomeCarOpts;

typedef struct {
	int strip_lines;			   // 0 (fresh), 1 or 2
	float strip_base[2];		   // the lines' baselines; each line is centred across the screen
	float strip_left, strip_right; // the most a line may span
	float strip_top, strip_bottom; // the strip's text: its first line's top, its last line's bottom
	float space;				   // the equal space: underline to strip, strip to ring, dock to hint bar
	float centre_x;
	float tile_w, tile_h; // the centre tile
	float row_y, row_cy;  // the centre tile's top and centre (its ring HOME_RING outside it)
	float gap, side_scale;
	int nitems; // Continue (or Pick a game) and the pinned games
	int ntools; // the tools in the dock: as many as fit across, the rest left off
	float dock_y, dock_sq, dock_glyph;
	HomeRect dock[HOME_MAX_PINS];
} HomeCarLayout;

// W x H: the screen; bar: the tab row's and the hint bar's height. ngames: the pinned games (Continue's own excluded),
// ntools: the pinned tools. Top to bottom, from the tab row's underline (opts->tabs_bottom), the body holds a space, the strip's text, a space, the row's block (the ring,
// the tile, the ring, then the dock gap and the squares) and a space down to the hint bar, the three spaces equal: so
// the strip sits midway between the tab row and the selection's ring, and the dock as far above the hint bar. The
// centre tile takes what the body leaves once the strip, the dock and three HOMECAR_MARGINs are in, up to
// opts->tile_max_h, in the 340:240 shape and never wider than the content (HOME_EDGE in from each side); the spaces
// share the rest. No strip (fresh): two spaces, above the ring and under the block.
// Pick a game alone (opts->no_continue, no pinned games): the card Grid draws (home_layout.c's Continue tile), the
// content's width (HOME_EDGE in from each side) from Grid's top-section top (its strip's baselines too) down to
// HOME_BOTTOM over the hint bar; with tools down to the dock, the dock where the row puts it (its gap and the ring
// as usual).
void HomeCar_compute(float W, float H, float bar, int strip_lines, const HomeCarOpts* opts, int ngames, int ntools,
					 HomeCarLayout* out);

// Item i of the row with the row's position at pos (0 = Continue centred; fractional while it slides): its rect, scale
// and darkening (row_model.c Row_item, ROW_CAROUSEL); visible false past 4 steps out.
typedef struct {
	HomeRect r;
	float scale, darken;
	bool visible;
} HomeCarItem;
HomeCarItem HomeCar_item(const HomeCarLayout* l, int i, float pos);

// The focus: the row's selection, or the dock's square (the row's selection is kept while in the dock), each
// remembered.
typedef struct {
	bool dock;
	int sel, tool;
} HomeCarFocus;

// The D-pad: along the row or the dock with LEFT/RIGHT (past an end: HOME_MOVE_EDGE_PREV / _NEXT, the caller switches
// tab on a fresh press only); UP from the row to the tab row (HOME_MOVE_TABS), from the dock back to the row; DOWN from
// the row to the dock on its remembered square when there are tools.
HomeMoveResult HomeCar_move(const HomeCarLayout* l, HomeCarFocus* f, HomeDir dir);
// UP on the tab row: into the dock when it has tools, else the row as it was.
void HomeCar_fromTabs(const HomeCarLayout* l, HomeCarFocus* f);
// A focus kept in range after a rebuild (fewer items: the last one; no tools: the row).
HomeCarFocus HomeCar_clampFocus(const HomeCarLayout* l, HomeCarFocus f);

#endif
