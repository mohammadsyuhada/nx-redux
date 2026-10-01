#ifndef ROW_MODEL_H
#define ROW_MODEL_H

#include <stdbool.h>

// row_model.h — Carousel/Backdrop geometry (dp) and curves. SDL-free.
// ROW_CAROUSEL: the game-list Carousel's tile row. The others are the frameless row: a game-list Backdrop's box art
// (BOX) or Tools listing (TOOL), and the main-menu Carousel on black: Consoles (LOGO), Collections (COLL), Tools (TOOL).
typedef enum { ROW_CAROUSEL,
			   ROW_BACKDROP_BOX,
			   ROW_BACKDROP_LOGO,
			   ROW_BACKDROP_TOOL,
			   ROW_BACKDROP_COLL } RowKind;
typedef struct {
	float item_w, item_h, scale, gap, f;
} RowSizes;
float Row_factor(float body_w, float body_h);			   // min(1, w/960, h/358)
RowSizes Row_sizes(RowKind k, float body_w, float body_h); // includes the Backdrop box-slot growth
typedef struct {
	float dx, scale, darken, alpha;
	bool visible;
} RowItem; // dx: centre offset from the row centre
// The Backdrop box slot (k == ROW_BACKDROP_BOX; other kinds untouched) gives way to its caption: when room + row +
// room + caption_gap + caption_h is taller than body_h, the slot shrinks (keeping its shape) until the block fits,
// never below the spec-scaled size (a block that still doesn't fit clamps under the header, see Row_top).
void Row_fitBoxSlot(RowSizes* s, RowKind k, float body_h, float room, float caption_gap, float caption_h);
RowItem Row_item(const RowSizes* s, RowKind k, float index, float pos);
void Row_visibleRange(int n, float pos, int* first, int* last); // items with d < 4, clamped; last < first when n == 0
// Block placement: returns the row's top y (dp). caption_h 0 = no caption reserved. room = ring/shadow room under/over.
float Row_top(float body_top, float body_h, float row_h, float room, float caption_gap, float caption_h);
float Row_shade(float y_frac); // the monotone cubic through the six keys

// The main-menu Carousel's "N games" line (sub-project 8). It has no caption block: each row is centred alone between
// the tab row and the hint bar (Row_top with no caption).
#define ROW_COUNT_SP 14.0f			   // "N games", in the accent: × the slot content scale (Row_countSp)
#define ROW_COUNT_MIN_SP 10.0f		   // ...never below the caption floor
#define ROW_LOGO_COUNT_GAP_DP 8.0f	   // Consoles: under the logo as drawn (unscaled)
#define ROW_COUNT_GLIDE_MS 300		   // Consoles: the count's y glides as logo heights differ (UI_easeStandard)
#define ROW_COLL_NAME_SP 30.0f		   // Collections: the name starts here × the slot content scale
#define ROW_COLL_NAME_FLOOR 0.75f	   // ...and shrinks (whole sp) until its longest word fits, never below 0.75 × start
#define ROW_COLL_NAME_OVER_COUNT 1.25f // ...nor below 1.25 × the count: the name always reads larger than its count
#define ROW_COLL_LINE 1.15f			   // its line height, × the font size
#define ROW_COLL_LINES 2			   // at most, the last ellipsised
#define ROW_COLL_COUNT_GAP_DP 6.0f	   // the count under the name (unscaled; × the side scale on a side item)
#define ROW_COUNT_FADE_MS 180		   // Collections: the selected item's count fades in
// The height art of aspect w/h draws at, contained in a box_w × box_h box (MenuArt's fit; any unit).
float Row_containH(float box_w, float box_h, float aspect);
// Consoles: the count line's top, gap under what's drawn (drawn_h) centred on the row's centre row_cy (any unit).
float Row_logoCountY(float row_cy, float drawn_h, float gap);
// The "N games" size (sp) for a slot content scale k (Consoles and Collections): max(10, 14 × k).
float Row_countSp(float k);
// The collection name's floor (sp): max(0.75 × start_sp, 1.25 × count_sp).
float Row_collNameFloor(float start_sp, float count_sp);
// Collections' name size (sp): start_sp (30 sp × the slot content scale) itself, then whole-sp steps down, until the
// longest word fits avail px (word_w: its width at start_sp; widths scale with the size); Row_collNameFloor when
// nothing above it fits (the floor itself, even above start_sp). Only then does the name wrap (≤ 2 lines, "…").
float Row_collNameSp(float start_sp, float word_w, float avail, float count_sp);
// A line step of `line` × the font size px, rounded (Collections' 1.15).
int Row_lineStep(int font_px, float line);
// Collections: a name of `lines` lines (clamped to 1..ROW_COLL_LINES) at line_h each, then gap, then the count
// line's reserved count_h, as one group centred in a slot_h slot. The count line is reserved whether or not it
// shows, so the name sits at the same y on every item (px; tops from the slot's top).
typedef struct {
	int name_y, name_h, count_y, block_h;
} RowCollText;
RowCollText Row_collText(int slot_h, int lines, int line_h, int gap, int count_h);
// Separable box blur of an 8-bit alpha buffer, `passes` times with radius r (in px). In place via tmp (same size).
void Row_boxBlurAlpha(unsigned char* a, unsigned char* tmp, int w, int h, int r, int passes);

#endif
