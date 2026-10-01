#include "../../nextui/infoband_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void brick_3x(void) { // 768 px tall, bar 84, rows 90 px, text 36 px, dp2 = 4, dp12 = 26
	InfoBandLayout l = InfoBand_layout(768, 84, 90, 90, 36, 4, 26);
	assert(l.band_bottom == 684);
	assert(l.text_top == 684 - 4 - 36);
	assert(l.band_top == l.text_top - 26);
	assert(l.rows == (l.text_top - 90) / 90); // 6
	assert(l.list_top == 90);
}

// NX_DP / SCALE1 at a given FIXED_SCALE (defines.h), so the vectors below follow the real rounding.
#define DP(x, s) ((int)((x) * (s) * 30.0f / 42.0f + 0.5f))
#define BAR(s) ((16 + 6 * 2) * (s))		// the hint bar and the tab row: SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2)
#define GLYPH_TOP(s) (BAR(s) - 6 * (s)) // the hint glyphs' top, from the screen bottom (UI_buttonHintIconTop)

// The main-menu List on one screen: the list 12 dp under the tab row, a 26 dp band (18 dp line, 4 dp each side) with
// its bottom 12 dp inside the hint bar, whole rows only. `font_h` is font.small's height (the old band's text line).
static void main_menu_screen(int screen_h, int s, int font_h, int rows_before, int rows_after) {
	int bar = BAR(s), row_h = 30 * s; // SCALE1(PILL_SIZE)
	InfoBandLayout l = InfoBand_fixedLayout(screen_h, bar, bar + DP(12, s), row_h, DP(18, s), DP(4, s), DP(12, s));
	assert(l.list_top == bar + DP(12, s));							// 12 dp below the tab row
	assert(l.fill_bottom == screen_h - bar);						// the 80% fill stops at the bar's edge
	assert(l.band_bottom == screen_h - bar + DP(12, s));			// 12 dp inside the bar
	assert(l.band_bottom - l.band_top == DP(18, s) + 2 * DP(4, s)); // 26 dp tall
	assert(l.text_top == l.band_top + DP(4, s) && l.text_h == DP(18, s));
	assert(l.text_top + l.text_h + DP(4, s) == l.band_bottom);
	// the text line stays clear of the hint glyphs (they start BUTTON_MARGIN below the bar's top)
	assert(l.text_top + l.text_h <= screen_h - GLYPH_TOP(s));
	// whole rows only: the last row ends at or above the band's top, and one more would cross it
	assert(l.rows == (l.band_top - l.list_top) / row_h);
	assert(l.list_top + l.rows * row_h <= l.band_top);
	assert(l.list_top + (l.rows + 1) * row_h > l.band_top);
	assert(l.rows == rows_after);
	// before: rows under the tab row, ending on the band's text line, the band sitting on the bar
	InfoBandLayout b = InfoBand_layout(screen_h, bar, bar, row_h, font_h, DP(2, s), DP(12, s));
	assert(b.rows == rows_before);
	assert(b.fill_bottom == b.band_bottom && b.band_bottom == screen_h - bar);
	printf("  %dpx tall at %dx: main-menu rows %d -> %d (list %d..%d, band %d..%d, fill to %d)\n", screen_h, s,
		   b.rows, l.rows, l.list_top, l.list_top + l.rows * row_h, l.band_top, l.band_bottom, l.fill_bottom);
}

static void main_menu_fixed_band(void) {
	// Brick 1024x768 at 3x: bar 84, list 110, band 653..710 (text 662..701, glyphs from 702), rows 6 -> 6
	main_menu_screen(768, 3, 48, 6, 6);
	InfoBandLayout brick = InfoBand_fixedLayout(768, 84, 110, 90, 39, 9, 26);
	assert(brick.band_top == 653 && brick.band_bottom == 710 && brick.text_top == 662 && brick.rows == 6);
	// Smart Pro S 1280x720 at 2x: bar 56, list 73, band 643..681 (text 649..675, glyphs from 676), rows 9 -> 9
	main_menu_screen(720, 2, 32, 9, 9);
	InfoBandLayout sps = InfoBand_fixedLayout(720, 56, 73, 60, 26, 6, 17);
	assert(sps.band_top == 643 && sps.band_bottom == 681 && sps.text_top == 649 && sps.rows == 9);
	// the band never depends on the list length: the same inputs, the same band (no count argument at all)
	InfoBandLayout again = InfoBand_fixedLayout(720, 56, 73, 60, 26, 6, 17);
	assert(again.band_top == sps.band_top && again.rows == sps.rows);
	// at least one row on a cramped screen
	assert(InfoBand_fixedLayout(200, 56, 73, 60, 26, 6, 17).rows == 1);
}

// A game-list List: rows right under the header (as before), down to the same fixed band in whole rows.
static void game_list_screen(int screen_h, int s, int font_h, int rows_before, int rows_after) {
	int bar = BAR(s), row_h = 30 * s;
	InfoBandLayout l = InfoBand_fixedLayout(screen_h, bar, bar, row_h, DP(18, s), DP(4, s), DP(12, s));
	assert(l.list_top == bar); // the list top is unchanged
	assert(l.band_bottom == screen_h - bar + DP(12, s) && l.fill_bottom == screen_h - bar);
	assert(l.list_top + l.rows * row_h <= l.band_top && l.list_top + (l.rows + 1) * row_h > l.band_top);
	assert(l.rows == rows_after);
	InfoBandLayout b = InfoBand_layout(screen_h, bar, bar, row_h, font_h, DP(2, s), DP(12, s));
	assert(b.rows == rows_before);
	printf("  %dpx tall at %dx: game-list rows %d -> %d (list %d..%d, band %d..%d)\n", screen_h, s, b.rows, l.rows,
		   l.list_top, l.list_top + l.rows * row_h, l.band_top, l.band_bottom);
}

static void game_list_fixed_band(void) {
	game_list_screen(768, 3, 48, 6, 6); // Brick: list 84..624, band 653..710
	game_list_screen(720, 2, 32, 9, 9); // SPS: list 56..596, band 643..681
}

static void layout_independent_of_text(void) {
	// same inputs → same rows whether or not the caller has text to show: there is no text argument at all
	InfoBandLayout a = InfoBand_layout(720, 56, 60, 60, 24, 3, 17);
	InfoBandLayout b = InfoBand_layout(720, 56, 60, 60, 24, 3, 17);
	assert(a.rows == b.rows && a.text_top == b.text_top);
}

static void at_least_one_row(void) {
	InfoBandLayout l = InfoBand_layout(100, 50, 40, 60, 20, 2, 10);
	assert(l.rows == 1);
}

// 10 px per code point (so "..." is 30 px and a 2-byte or 3-byte character is 10 px, like one glyph)
static int measureCodePoints(void* ctx, const char* text) {
	(void)ctx;
	int n = 0;
	for (const unsigned char* p = (const unsigned char*)text; *p; p++)
		if ((*p & 0xC0) != 0x80)
			n++;
	return n * 10;
}

static void cut_whole_code_points(void) {
	// "Next: Pok\xC3\xA9mon": a room that would split \xC3\xA9 by bytes keeps it whole or drops it whole
	char t[160] = "Next: Pok\xC3\xA9mon";									 // 13 code points, 14 bytes
	int w = InfoBand_cutText(t, sizeof(t), 100, 6, measureCodePoints, NULL); // 10 cps: 7 kept + "..."
	assert(w == 100);
	assert(strcmp(t, "Next: P...") == 0);
	char u[160] = "Next: Pok\xC3\xA9mon";
	w = InfoBand_cutText(u, sizeof(u), 130, 6, measureCodePoints, NULL); // 13 cps: the \xC3\xA9 kept whole
	assert(w == 130);
	assert(strcmp(u, "Next: Pok\xC3\xA9...") == 0);
	// every result ends on a whole sequence: no lead byte without its continuation
	for (int room = 0; room <= 200; room += 5) {
		char v[160] = "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3 Red";
		if (InfoBand_cutText(v, sizeof(v), room, 0, measureCodePoints, NULL) < 0)
			continue;
		size_t len = strlen(v);
		assert(len >= 4 && strcmp(v + len - 3, "...") == 0);
		unsigned char last = (unsigned char)v[len - 4];
		assert(last < 0x80 || (last & 0xC0) == 0x80); // ASCII or a continuation byte, never a dangling lead
	}
}

static void cut_trims_spaces_and_keeps_a_title_char(void) {
	char t[160] = "Next: Super Mario";
	// 12 cps: "Next: Super" (11) + "..." would be 14; "Next: Supe" 10 + 3 = 13; "Next: Su" 8 + 3 = 11 fits
	int w = InfoBand_cutText(t, sizeof(t), 110, 6, measureCodePoints, NULL);
	assert(w == 110 && strcmp(t, "Next: Su...") == 0);
	char sp[160] = "Last week ago";
	w = InfoBand_cutText(sp, sizeof(sp), 120, 0, measureCodePoints, NULL); // "Last week" 9 + 3 = 12
	assert(w == 120 && strcmp(sp, "Last week...") == 0);
	char sp2[160] = "Last week ago";
	w = InfoBand_cutText(sp2, sizeof(sp2), 110, 0, measureCodePoints, NULL); // "Last wee..." never "Last week ..."
	assert(w == 110 && strcmp(sp2, "Last wee...") == 0);
	char sp3[160] = "Last  week";
	w = InfoBand_cutText(sp3, sizeof(sp3), 80, 0, measureCodePoints, NULL); // "Last  w" too wide; "Last..." fits
	assert(w == 70 && strcmp(sp3, "Last...") == 0);
	// "Next: ..." / "Next:..." / "Ne..." are refused: no title character survives
	char n[160] = "Next: Zelda";
	assert(InfoBand_cutText(n, sizeof(n), 90, 6, measureCodePoints, NULL) == -1);
	assert(strcmp(n, "Next: Zelda") == 0);											// untouched on refusal
	assert(InfoBand_cutText(n, sizeof(n), 100, 6, measureCodePoints, NULL) == 100); // "Next: Z..."
	assert(strcmp(n, "Next: Z...") == 0);
	char e[160] = "x";
	assert(InfoBand_cutText(e, sizeof(e), 1000, 0, measureCodePoints, NULL) == -1); // nothing left to keep
}

static InfoSeg seg(InfoSegKind kind, const char* text) {
	InfoSeg s = {.kind = kind};
	snprintf(s.text, sizeof(s.text), "%s", text);
	return s;
}

// 10 px a code point, separators 30 px, the trophy 20 px.
static int fit(InfoSeg* segs, int n, int avail) {
	return InfoBand_fitSegments(segs, n, avail, 30, 20, measureCodePoints, NULL);
}

static void fit_time_falls_back_to_duration_then_drops(void) {
	// Grid tile, 3x (device): "Today - 18m 25s · 🏆 0 of 30" wider than the tile used to lose the whole line
	InfoSeg a[2] = {seg(INFO_SEG_TIME, "Today - 18m 25s"), seg(INFO_SEG_ACH, "0 of 30")}; // 150 + 30 + 70 + 20
	assert(fit(a, 2, 270) == 2 && strcmp(a[0].text, "Today - 18m 25s") == 0);			  // fits whole
	assert(fit(a, 2, 200) == 2);														  // "18m 25s" 70 + 30 + 90 = 190
	assert(strcmp(a[0].text, "18m 25s") == 0 && a[1].kind == INFO_SEG_ACH);
	InfoSeg b[2] = {seg(INFO_SEG_TIME, "Today - 18m 25s"), seg(INFO_SEG_ACH, "0 of 30")};
	assert(fit(b, 2, 120) == 1); // the time goes, the achievements stay
	assert(b[0].kind == INFO_SEG_ACH && strcmp(b[0].text, "0 of 30") == 0);
	InfoSeg c[2] = {seg(INFO_SEG_TIME, "Today - 18m 25s"), seg(INFO_SEG_ACH, "0 of 30")};
	assert(fit(c, 2, 80) == 0); // not even the achievements
	// the time alone: its duration, then nothing
	InfoSeg d[1] = {seg(INFO_SEG_TIME, "2 weeks ago - 3h 55m")};
	assert(fit(d, 1, 100) == 1 && strcmp(d[0].text, "3h 55m") == 0);
	InfoSeg e[1] = {seg(INFO_SEG_TIME, "2 weeks ago - 3h 55m")};
	assert(fit(e, 1, 50) == 0);
	// no " - " (only a when or only a duration): never cut, dropped whole
	InfoSeg f[2] = {seg(INFO_SEG_TIME, "Yesterday"), seg(INFO_SEG_ACH, "3 of 40")};
	assert(fit(f, 2, 100) == 1 && f[0].kind == INFO_SEG_ACH);
}

static void fit_next_gives_way_before_the_time(void) {
	// the full line: Next is cut first and the time keeps its when
	InfoSeg a[3] = {seg(INFO_SEG_TIME, "Today - 1m"), seg(INFO_SEG_ACH, "3 of 40"),
					seg(INFO_SEG_NEXT, "Next: Zelda Ocarina")}; // 100 + 30 + 90 + 30 + 190
	assert(fit(a, 3, 350) == 3);
	assert(strcmp(a[0].text, "Today - 1m") == 0 && strcmp(a[2].text, "Next: Z...") == 0);
	// too narrow for any of Next: it goes, then the count-only rule
	InfoSeg b[3] = {seg(INFO_SEG_TIME, "Today - 1m"), seg(INFO_SEG_ACH, "3 of 40"), seg(INFO_SEG_NEXT, "Next: Zelda")};
	assert(fit(b, 3, 220) == 2 && strcmp(b[0].text, "Today - 1m") == 0); // 100 + 30 + 90
	InfoSeg c[3] = {seg(INFO_SEG_TIME, "Today - 1m"), seg(INFO_SEG_ACH, "3 of 40"), seg(INFO_SEG_NEXT, "Next: Zelda")};
	assert(fit(c, 3, 160) == 2 && strcmp(c[0].text, "1m") == 0); // 20 + 30 + 90
}

static void fit_count_line_is_cut(void) {
	InfoSeg a[1] = {seg(INFO_SEG_COUNT, "38 games")};
	assert(fit(a, 1, 80) == 1 && strcmp(a[0].text, "38 games") == 0);
	assert(fit(a, 1, 60) == 1 && strcmp(a[0].text, "38...") == 0);
	assert(fit(a, 0, 60) == 0);
}

int main(void) {
	fit_time_falls_back_to_duration_then_drops();
	fit_next_gives_way_before_the_time();
	fit_count_line_is_cut();
	brick_3x();
	main_menu_fixed_band();
	game_list_fixed_band();
	layout_independent_of_text();
	at_least_one_row();
	cut_whole_code_points();
	cut_trims_spaces_and_keeps_a_title_char();
	printf("test_infoband_layout: ok\n");
	return 0;
}
