// Host unit test for ui_list_layout.h (pure, no SDL). Run via run_tests.sh (AddressSanitizer build).
#include <assert.h>
#include <stdio.h>
#include "../ui_list_layout.h"

// The two screens (measured with font1.ttf, the page title at NX_SP(16) bold):
//   Brick 1024x768 at 3x: title 41 px, height 55, ascent 43, in an 84 px strip -> letters' bottom (baseline) 57;
//                         hint bar 84 px, icons SCALE1(6) below its top -> 768 - 84 + 18 = 702.
//   SPS 1280x720 at 2x:   title 27 px, height 36, ascent 29, in a 56 px strip -> baseline 39;
//                         hint icons at 720 - 56 + 12 = 676.
#define BRICK_TOP 57
#define BRICK_BOTTOM 702
#define SPS_TOP 39
#define SPS_BOTTOM 676

static void check_invariants(UIListBlock b, int avail_top, int avail_bottom, int row_h) {
	assert(b.rows >= 0);
	assert(b.strip == row_h / 2);
	int block_top = b.top - b.strip;
	int block_bottom = b.top + b.rows * row_h + b.strip;
	// inside the available band
	assert(block_top >= avail_top);
	assert(block_bottom <= avail_bottom);
	// centred: the two gaps differ by at most a pixel
	int gap_top = block_top - avail_top;
	int gap_bottom = avail_bottom - block_bottom;
	assert(gap_top - gap_bottom <= 1 && gap_bottom - gap_top <= 1);
	// arrows centred in equal strips: up arrow as far above the first row as the down arrow below the last
	assert(b.top - b.up_y == b.down_y - (b.top + b.rows * row_h));
	assert(b.up_y - block_top == b.strip - b.strip / 2);
}

// Like check_invariants, for any strip height.
static void check_invariants_strip(UIListBlock b, int avail_top, int avail_bottom, int row_h) {
	int block_top = b.top - b.strip;
	int block_bottom = b.top + b.rows * row_h + b.strip;
	assert(block_top >= avail_top);
	assert(block_bottom <= avail_bottom);
	int gap_top = block_top - avail_top;
	int gap_bottom = avail_bottom - block_bottom;
	assert(gap_top - gap_bottom <= 1 && gap_bottom - gap_top <= 1);
	assert(b.top - b.up_y == b.down_y - (b.top + b.rows * row_h));
}

static void test_equal_strips_and_centring(void) {
	UIListBlock b = UI_listBlock(100, 500, 40, 0);
	// 400 px: 9 rows (360) + 2 x 20 strips = 400 exactly
	assert(b.rows == 9);
	assert(b.strip == 20);
	assert(b.top == 120);
	assert(b.up_y == 110);
	assert(b.down_y == 120 + 360 + 10);
	check_invariants(b, 100, 500, 40);
}

static void test_whole_rows_only(void) {
	// 439 px: 9 rows + strips = 400 leaves 39 (a tenth row needs 40): still 9, slack split 19 / 20
	UIListBlock b = UI_listBlock(100, 539, 40, 0);
	assert(b.rows == 9);
	assert(b.top == 100 + 19 + 20);
	check_invariants(b, 100, 539, 40);
}

static void test_rows_wanted_caps(void) {
	UIListBlock b = UI_listBlock(100, 500, 40, 4);
	assert(b.rows == 4);
	// block 4 x 40 + 40 = 200 in 400: centred at 200
	assert(b.top == 100 + 100 + 20);
	check_invariants(b, 100, 500, 40);
	// asking for more than fits gives what fits
	b = UI_listBlock(100, 500, 40, 50);
	assert(b.rows == 9);
}

static void test_degenerate(void) {
	UIListBlock b = UI_listBlock(100, 120, 40, 0); // band smaller than the strips
	assert(b.rows == 0);
	b = UI_listBlock(100, 500, 0, 0); // no row height
	assert(b.rows == 0);
	b = UI_listBlock(500, 100, 40, 0); // inverted band
	assert(b.rows == 0);
}

static void test_custom_strip(void) {
	// detail pages: 16 dp strips (NX_DP(16) = 34 at 3x)
	UIListBlock b = UI_listBlockStrip(BRICK_TOP, BRICK_BOTTOM, 90, 0, 34);
	assert(b.strip == 34);
	assert(b.rows == 6); // 645 - 68 = 577 -> 6 x 90
	assert(b.top - b.up_y == b.down_y - (b.top + b.rows * 90));
}

// The standard pill list (UI_calcListLayout): rows SCALE1(PILL_SIZE).
static void test_pill_list_vectors(void) {
	// Brick 3x: 90 px rows. 645 px: 6 rows (540) + 90 = 630, slack 15 (7 above, 8 below).
	UIListBlock b = UI_listBlock(BRICK_TOP, BRICK_BOTTOM, 90, 0);
	assert(b.rows == 6);
	assert(b.top == BRICK_TOP + 7 + 45);
	assert(b.up_y == b.top - 22);
	assert(b.down_y == b.top + 540 + 22);
	check_invariants(b, BRICK_TOP, BRICK_BOTTOM, 90);
	// SPS 2x: 60 px rows. 637 px: 9 rows (540) + 60 = 600, slack 37 (18 above).
	b = UI_listBlock(SPS_TOP, SPS_BOTTOM, 60, 0);
	assert(b.rows == 9);
	assert(b.top == SPS_TOP + 18 + 30);
	check_invariants(b, SPS_TOP, SPS_BOTTOM, 60);
}

// The description area under a settings page: desc_rows - 0.5 rows (none without descriptions).
static void test_settings_desc_height(void) {
	assert(UI_settingsDescHeight(67, 2) == 100);
	assert(UI_settingsDescHeight(45, 2) == 67);
	assert(UI_settingsDescHeight(67, 1) == 0);
	assert(UI_settingsDescHeight(0, 2) == 0);
}

// Options pages (LIST-LAYOUT §10.2 addition / spec "Options pages"): rows 0.75 x the pill list pitch; titled + fits
// -> no strips, the rows alone centred; titled + scrolls -> strips of the arrow's height + 2 dp air each side;
// untitled -> the first row on the pill list's first row (its top gutter).
static void test_options_row_and_strip(void) {
	assert(UI_optionsRowHeight(90) == 67);	   // Brick 3x
	assert(UI_optionsRowHeight(60) == 45);	   // SPS 2x
	assert(UI_optionsArrowStrip(18, 4) == 26); // 3x: SCALE1(6) + 2 x NX_DP(2)
	assert(UI_optionsArrowStrip(12, 3) == 18); // 2x
}

static void test_options_titled_fits(void) {
	// Brick 3x, no description: 645 px holds 9 rows of 67 without strips; a 5-row page sits centred
	UIListBlock b = UI_optionsBlock(BRICK_TOP, BRICK_BOTTOM, 67, 5, 26, -1);
	assert(b.rows == 5);
	assert(b.strip == 0);
	int gap_top = b.top - BRICK_TOP;
	int gap_bottom = BRICK_BOTTOM - (b.top + 5 * 67);
	assert(gap_top - gap_bottom <= 1 && gap_bottom - gap_top <= 1);
	// exactly as many as fit without strips: still no strips
	b = UI_optionsBlock(BRICK_TOP, BRICK_BOTTOM, 67, 9, 26, -1);
	assert(b.rows == 9 && b.strip == 0);
}

static void test_options_titled_scrolls(void) {
	// Brick 3x, no description, 20 items: 645 - 2 x 26 = 593 -> 8 rows
	UIListBlock b = UI_optionsBlock(BRICK_TOP, BRICK_BOTTOM, 67, 20, 26, -1);
	assert(b.rows == 8);
	assert(b.strip == 26);
	check_invariants_strip(b, BRICK_TOP, BRICK_BOTTOM, 67);
	// Brick with descriptions (1.5 rows = 100 px): 545 - 52 = 493 -> 7
	b = UI_optionsBlock(BRICK_TOP, BRICK_BOTTOM - 100, 67, 20, 26, -1);
	assert(b.rows == 7);
	// SPS 2x, no description: 637 - 36 = 601 -> 13 rows of 45
	b = UI_optionsBlock(SPS_TOP, SPS_BOTTOM, 45, 30, 18, -1);
	assert(b.rows == 13);
	check_invariants_strip(b, SPS_TOP, SPS_BOTTOM, 45);
	// SPS with descriptions (67 px): 570 - 36 = 534 -> 11
	b = UI_optionsBlock(SPS_TOP, SPS_BOTTOM - 67, 45, 30, 18, -1);
	assert(b.rows == 11);
}

static void test_options_untitled(void) {
	// the first row on the pill list's first row (Brick 109), top-anchored
	UIListBlock b = UI_optionsBlock(BRICK_TOP, BRICK_BOTTOM, 67, 4, 26, 109);
	assert(b.top == 109 && b.rows == 4 && b.strip == 0);
	b = UI_optionsBlock(BRICK_TOP, BRICK_BOTTOM, 67, 20, 26, 109);
	assert(b.top == 109);
	assert(b.rows == 8); // (702 - 109 - 26) / 67
	assert(b.top - b.up_y == b.down_y - (b.top + b.rows * 67));
}

// Rich rows (LIST-LAYOUT §10.2/§10.6): title = secondary (0.8 x label) capped at row / 2.16, second line =
// caption (0.64 x label), line boxes 1.2 x the size, the pair centred in the row.
static void test_rich_row_text(void) {
	// Brick 3x: label SCALE1(16) = 48 px; row 129 (fitted)
	UIRichRowText t = UI_richRowText(129, 48);
	assert(t.title_px == 38);  // 0.8 x 48 = 38.4; cap 129 / 2.16 = 59.7
	assert(t.second_px == 30); // 0.8 x 38 = 30.4
	assert(t.title_box == 46); // 1.2 x 38 = 45.6
	assert(t.second_box == 36);
	assert(t.title_top + t.title_box == t.second_top);
	int below = 129 - (t.second_top + t.second_box);
	assert(t.title_top == below || t.title_top + 1 == below);
	// a short row caps the title
	t = UI_richRowText(40, 48);
	assert(t.title_px == 18);  // 40 / 2.16 = 18.5
	assert(t.second_px == 14); // 0.8 x the capped title
	assert(t.title_box + t.second_box <= 40 + 1);
}

// Rich lists round their row count to the nearest whole number of nominal rows and stretch or shrink the rows to
// fill the band (§10.6: the pitch is "rounded to whole rows"), so the half-row strips don't cost a 1.5x row.
static void test_fit_row_height(void) {
	// Brick 3x: nominal 135 (1.5 x 90), 645 px band: 645 / 135 = 4.78 -> 5 pitches = 4 rows + the strips
	int h = UI_listFitRowHeight(BRICK_BOTTOM - BRICK_TOP, 135);
	assert(h == 129);
	UIListBlock b = UI_listBlock(BRICK_TOP, BRICK_BOTTOM, h, 0);
	assert(b.rows == 4);
	check_invariants(b, BRICK_TOP, BRICK_BOTTOM, h);
	// SPS 2x: nominal 90, 637 px: 7.08 -> 7 pitches = 6 rows, 91 px
	h = UI_listFitRowHeight(SPS_BOTTOM - SPS_TOP, 90);
	assert(h == 91);
	b = UI_listBlock(SPS_TOP, SPS_BOTTOM, h, 0);
	assert(b.rows == 6);
	check_invariants(b, SPS_TOP, SPS_BOTTOM, h);
	// at least one row; degenerate input returns the nominal height
	assert(UI_listFitRowHeight(100, 135) == 50);
	assert(UI_listFitRowHeight(0, 135) == 135);
	assert(UI_listFitRowHeight(500, 0) == 0);
}

// Achievement detail page (§10.6): the badge takes the height left over, 40..96 dp.
static void test_detail_badge(void) {
	// plenty of room: capped at max
	assert(UI_detailBadgeSize(1000, 300, 86, 206) == 206);
	// some room: the remainder
	assert(UI_detailBadgeSize(400, 300, 86, 206) == 100);
	assert(UI_detailBadgeSize(500, 300, 86, 206) == 200);
	// too little: min (the page scrolls)
	assert(UI_detailBadgeSize(320, 300, 86, 206) == 86);
}

int main(void) {
	test_equal_strips_and_centring();
	test_whole_rows_only();
	test_rows_wanted_caps();
	test_degenerate();
	test_custom_strip();
	test_pill_list_vectors();
	test_settings_desc_height();
	test_fit_row_height();
	test_options_row_and_strip();
	test_options_titled_fits();
	test_options_titled_scrolls();
	test_options_untitled();
	test_rich_row_text();
	test_detail_badge();
	printf("test_list_layout: all passed\n");
	return 0;
}
