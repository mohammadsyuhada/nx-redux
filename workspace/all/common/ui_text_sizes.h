// Per-device text sizes, tuned by eye page by page (the log and the reasons: .dev/TEXT_SIZES.md). Each entry is the
// text's size in screen px on the Brick (3.2" 1024x768), the Brick Pro (3.95" 1024x768) and the Smart Pro family
// (4.96" 1280x720), set on each device on its own; once the devices' values follow a pattern they can fold back into
// the UI scale. Header-only and SDL-free: host-tested by tests/test_ui_scale.c.
#ifndef UI_TEXT_SIZES_H
#define UI_TEXT_SIZES_H

#include "ui_scale.h"

typedef struct {
	float brick, brickpro, smartpro;
} TextPx;

static inline float TextPx_for(TextPx t, UIDevice d) {
	return d == UI_DEVICE_BRICK ? t.brick : d == UI_DEVICE_BRICKPRO ? t.brickpro
																	: t.smartpro;
}

// Home's stats strip (the "This month" and "Most played" lines). Was 27 on the Brick (2026-10-07: 30, then 33); the
// Brick Pro and Smart Pro keep their sizes from the UI scale (STRIP_PX 27 at Home's scale) until tuned.
#define TEXT_HOME_STATS ((TextPx){33, 28, 26})
// Home's Continue card: the game's title and its info line (the time). Were 29 and 21 on the Brick (2026-10-07: both
// the stats' 33, the caption's spacing growing with them); the Brick Pro and Smart Pro keep the UI scale's
// (CONT_TITLE_PX 44 / CONT_SUB_PX 31 at Home's scale).
#define TEXT_HOME_CONT_TITLE ((TextPx){33, 28, 26})
#define TEXT_HOME_CONT_INFO ((TextPx){33, 28, 26})
// Home's pinned games: the name and its info line (the time, shown on the selected pin). Were 23 and 19 on the Brick
// (2026-10-07: both 33, as the Continue card; their line boxes grow with them); the Brick Pro and Smart Pro keep the UI
// scale's (PIN_NAME_PX 33.8 / PIN_INFO_PX 28.6 at Home's scale).
#define TEXT_HOME_PIN_NAME ((TextPx){33, 28, 26})
#define TEXT_HOME_PIN_INFO ((TextPx){33, 28, 26})
// Not a text size: the pinned games' height, times what the layout gives them (2026-10-07: double on the Brick).
#define HOME_PIN_K ((TextPx){2, 1, 0.8f}) // the Smart Pro S: 0.8 (its top section taller)
// Not a text size: Home's tool squares per column on a 4:3 screen (2026-10-07: 5 on the Brick Pro; the rest 4).
#define HOME_TOOL_ROWS ((TextPx){4, 5, 4})
// Not a text size: the wide (16:9) Home's pins a row (2026-10-07: 3 on the Smart Pro S; was 4). 0: 4.
#define HOME_WIDE_PIN_COLS ((TextPx){0, 0, 3})
// The main menu's List layout (the Consoles, Collections and Tools tabs as a list): the rows' text, and their pitch (the
// list fits as many as the pitch allows, each shrinking by up to 5%; the selection pill takes the row's height when
// it is shorter than PILL_SIZE). The Brick: 48 on a 90 pitch, 6 rows (2026-10-07: 41 on 80, 7 rows of 76, tested and
// reverted); the Brick Pro and Smart Pro keep the UI scale's (font.large, SCALE1(PILL_SIZE)).
#define TEXT_MENU_LIST ((TextPx){48, 40, 36})
#define MENU_LIST_PITCH ((TextPx){90, 75, 68})
// The apps' main lists (a ListView in the large font: Settings' categories, the tools' menus): this many rows, at
// TEXT_MENU_LIST, their height fitted between the page title and the hint bar (2026-10-07: 7 on the Brick tested with
// the main menu's, reverted); 0 keeps the UI scale's (SCALE1(PILL_SIZE) rows, as many as fit).
#define MENU_LIST_ROWS ((TextPx){0, 0, 0})
// The game lists' info line on its own row (the play time and achievements, shown where the hint bar's hints were
// while they are hidden). Was font.small (36) on the Brick (2026-10-07: Home's stats' 33); the Brick Pro and Smart
// Pro keep font.small (SCALE1(FONT_SMALL)).
#define TEXT_LIST_INFO ((TextPx){33, 28, 26})
// The main menu's Grid, Tools tab: each tool's name, and the tiles' width times the spec shape's (height unchanged).
// Were 31 px (14 sp at the tile scale) and 1x on the Brick (2026-10-07: the stats' 33, and 1.5x as wide); the Brick
// Pro and Smart Pro keep theirs (0: 14 sp at the tile scale). The name still shrinks (to 11 sp) until its longest word
// fits the tile.
#define TEXT_GRID_TOOL ((TextPx){33, 28, 26})
#define GRID_TOOLS_W_K ((TextPx){1.5f, 1.5f, 1.5f})
// The main menu's Carousel, Horizontal: the Consoles' and Collections' "N games" and a collection's name, in px at the
// selected (full-size) slot, the neighbours scaled from it as before (2026-10-07: the stats' 33 on the Brick; the name
// still shrinks until its longest word fits). 0 keeps the UI scale's (Row_countSp, ROW_COLL_NAME_SP at the slot scale).
#define TEXT_CAROUSEL_COUNT ((TextPx){33, 28, 26})
#define TEXT_CAROUSEL_COLL_NAME ((TextPx){48, 41, 0}) // 33, 42, then 48 (2026-10-07: above the count's 33)
// The Horizontal Collections slot's widest, times the spec's (300 dp; height unchanged): each collection takes its
// content's width up to it, one gap between neighbours (2026-10-07: 1.5x on the Brick).
#define CAROUSEL_COLL_W_K ((TextPx){1.5f, 1.5f, 1})
// The Horizontal Tools row's tool names, px at the selected slot (neighbours scaled from it; still shrunk until the
// longest word fits): 2026-10-07, the Grid's 33 on the Brick. 0 keeps the UI scale's (17 sp at the slot scale).
#define TEXT_CAROUSEL_TOOL ((TextPx){36, 31, 0}) // 33, then 36 (a test)
// The Horizontal Tools slot's widest, times the spec's (height and icon unchanged): each tool takes its content's width
// up to it, one gap between neighbours (rowview.c slotContentPx; 2026-10-07: 1.5x on the Brick, as Collections).
#define CAROUSEL_TOOL_W_K ((TextPx){1.5f, 1.5f, 1})
// The gap between neighbouring Horizontal Tools and Collections items (content-sized), times Row_sizes' (2026-10-07: 2x
// on the Brick).
#define CAROUSEL_ITEM_GAP_K ((TextPx){2, 2, 1})
// A game list's Carousel: the game's name and its info rows in the caption under the row (2026-10-07: the name 46,
// then 43, then 40; the info Home's stats' 33, was 36). 0 keeps the UI scale's (CAPTION_NAME_SP 18 sp,
// CAPTION_INFO_SP 14 sp; beside a Vertical stack CAPTION_SIDE_NAME_SP 16 sp). The Vertical Carousel's caption beside
// its stack and Backdrop's captions take them too (2026-10-07).
#define TEXT_CAROUSEL_GAME_NAME ((TextPx){40, 34, 0})
#define TEXT_CAROUSEL_GAME_INFO ((TextPx){33, 28, 26})
// A game list's Carousel: the selected game's picture times Row_sizes', its neighbours kept at their size
// (2026-10-07: 1.4x on the Brick).
#define CAROUSEL_GAME_SEL_K ((TextPx){1.4f, 1.4f, 1}) // 1.1, 1.2, then 1.4
// A game list's Carousel caption: its info lines under the name. 1: one line (time · trophies · Next); 2: the time and
// trophies, then Next on its own line, each left out when empty (2026-10-07: the 4:3 Brick and Brick Pro). Backdrop's
// caption under the row follows it where it is 2 (else keeps its time / trophies + Next rows).
#define CAROUSEL_CAPTION_INFO_LINES ((TextPx){2, 2, 1})

#endif
