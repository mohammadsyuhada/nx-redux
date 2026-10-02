// Host unit test for ui_title_fit.h (pure, no SDL). Run via run_tests.sh (AddressSanitizer build).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ui_title_fit.h"

#define ELL UI_TITLE_ELLIPSIS

// Fake font: every code point is 10 px wide (UTF-8 continuation bytes are free).
static int measure_cp(const char* s, void* ctx) {
	(void)ctx;
	int n = 0;
	for (; *s; s++)
		if (((unsigned char)*s & 0xC0) != 0x80)
			n++;
	return 10 * n;
}

static void test_fits_whole(void) {
	char out[128];
	int w = UI_titleFit("Consoles | Game Boy", NULL, 1000, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Consoles | Game Boy") == 0);
	assert(w == 190);
	w = UI_titleFit("RetroAchievements | Pokemon", " (12/39)", 1000, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "RetroAchievements | Pokemon (12/39)") == 0);
	assert(w == measure_cp(out, NULL));
}

static void test_middle_cut_keeps_prefix_and_suffix(void) {
	char out[128];
	// "Consoles | " = 11 cp, " (12/39)" = 8 cp, ellipsis 1 cp: 290 px -> 29 cp: 11 + middle + 1 + 8 <= 29 ->
	// middle = 9, a readable middle (>= UI_TITLE_MIN_MIDDLE_CPS), so the prefix stays.
	int w = UI_titleFit("Consoles | Pokemon Fire Red", " (12/39)", 290, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Consoles | Pokemon F" ELL " (12/39)") == 0);
	assert(w <= 290);
	assert(w == measure_cp(out, NULL));
}

static void test_middle_cut_trims_trailing_space(void) {
	char out[128];
	// 11 + middle + 1 <= 19 -> middle = 7 "Pokemon" ... with 8 it would be "Pokemon " -> trimmed.
	UI_titleFit("Consoles | Pokemon Fire Red", NULL, 200, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Consoles | Pokemon" ELL) == 0);
}

static void test_utf8_cut_on_code_point_boundary(void) {
	char out[128];
	// "Collections | " = 14 cp; middle "ポケモン赤バージョン" (10 cp, 3 bytes each); max 230 -> 14 + m + 1 <= 23 ->
	// m = 8.
	UI_titleFit("Collections | ポケモン赤バージョン", NULL, 230, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Collections | ポケモン赤バージ" ELL) == 0);
}

static void test_prefix_and_suffix_too_wide_drop_prefix(void) {
	char out[128];
	// "RetroAchievements | " = 20 cp, suffix " (12/39)" = 8 cp: no room for a readable middle within 20 cp -> the
	// prefix and its separator are dropped, and "Pokemon (12/39)" (15 cp) fits whole.
	int w = UI_titleFit("RetroAchievements | Pokemon", " (12/39)", 200, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Pokemon (12/39)") == 0);
	assert(w <= 200);
}

// The Brick (3x) RA game page: the title font is ~25 px a code point and the title gets ~684 px (~27 cp).
// "RetroAchievements | " (20) + 8 middle cps + ellipsis + " (0/66)" (7) = 36 > 27 -> the game name is kept
// instead of the prefix.
static void test_ra_game_title_at_3x(void) {
	char out[128];
	int w = UI_titleFit("RetroAchievements | Astro Boy: Omega Factor", " (0/66)", 270, measure_cp, NULL, out,
						sizeof(out));
	assert(strcmp(out, "Astro Boy: Omega Fa" ELL " (0/66)") == 0);
	assert(w <= 270);
	// a short name fits whole, without an ellipsis
	UI_titleFit("RetroAchievements | 1942", " (0/27)", 270, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "1942 (0/27)") == 0);
	// the same title at a 2x-like width (more room): the prefix stays and the middle ellipsizes
	w = UI_titleFit("RetroAchievements | Astro Boy: Omega Factor", " (0/66)", 380, measure_cp, NULL, out,
					sizeof(out));
	assert(strcmp(out, "RetroAchievements | Astro Boy:" ELL " (0/66)") == 0);
	assert(w <= 380);
	// exactly UI_TITLE_MIN_MIDDLE_CPS of the middle: still the prefix
	UI_titleFit("RetroAchievements | Astro Boy: Omega Factor", " (0/66)", 360, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "RetroAchievements | Astro Bo" ELL " (0/66)") == 0);
	// one fewer: dropped
	UI_titleFit("RetroAchievements | Astro Boy: Omega Factor", " (0/66)", 350, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Astro Boy: Omega Factor (0/66)") == 0);
}

// Even the middle alone doesn't fit: it ellipsizes before the suffix, which stays.
static void test_dropped_prefix_middle_still_cut(void) {
	char out[128];
	int w = UI_titleFit("RetroAchievements | Astro Boy: Omega Factor", " (0/66)", 120, measure_cp, NULL, out,
						sizeof(out));
	assert(strcmp(out, "Astr" ELL " (0/66)") == 0);
	assert(w <= 120);
	// the dropped prefix's middle keeps a later " | " unprotected (a plain end cut)
	UI_titleFit("A long parent page | B | Long middle", NULL, 100, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "B | Long" ELL) == 0);
}

static void test_no_separator_plain_end_cut(void) {
	char out[128];
	UI_titleFit("The Legend of Zelda", NULL, 100, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "The Legen" ELL) == 0);
	// with a suffix: the suffix stays
	UI_titleFit("The Legend of Zelda", " (3/9)", 100, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "The" ELL " (3/9)") == 0);
}

static void test_only_first_separator_protected(void) {
	char out[128];
	// "A | " protected (4 cp); "B | Long middle" is the middle; 140 px -> 4 + m + 1 <= 14 -> m = 9 "B | Long "
	// (its trailing space dropped)
	UI_titleFit("A | B | Long middle", NULL, 140, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "A | B | Long" ELL) == 0);
}

static void test_small_buffer_is_safe(void) {
	char out[8];
	UI_titleFit("Consoles | Game Boy", NULL, 1000, measure_cp, NULL, out, sizeof(out));
	assert(strlen(out) < sizeof(out));
	UI_titleFit("Collections | ポケモン赤", NULL, 100, measure_cp, NULL, out, sizeof(out));
	assert(strlen(out) < sizeof(out));
	// no torn code point at the end
	size_t n = strlen(out);
	assert(n == 0 || ((unsigned char)out[n - 1] & 0xC0) != 0xC0);
}

static void test_prefix_cut_drops_dangling_separator(void) {
	char out[128];
	// an empty middle ("Consoles | "): the last resort cuts the prefix's end, its " | " dropped first
	UI_titleFit("Consoles | ", " (1/2)", 150, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Consoles" ELL " (1/2)") == 0);
	// a separator without spaces before the cut point never dangles either
	UI_titleFit("Ab|Cd | ", NULL, 40, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Ab" ELL) == 0);
	// with a middle, a too-narrow title drops the prefix instead
	UI_titleFit("Consoles | Game Boy", " (1/2)", 150, measure_cp, NULL, out, sizeof(out));
	assert(strcmp(out, "Game Boy (1/2)") == 0);
}

static void ends_with(const char* s, const char* tail) {
	size_t n = strlen(s), t = strlen(tail);
	assert(n >= t && strcmp(s + n - t, tail) == 0);
}

static void test_title_longer_than_buffer(void) {
	char title[400], out[256];
	memset(title, 'a', 300);
	title[300] = '\0';
	// fits the width but not the buffer: still ellipsized, suffix kept
	UI_titleFit(title, " (12/39)", 1000000, measure_cp, NULL, out, sizeof(out));
	assert(strlen(out) < sizeof(out));
	ends_with(out, ELL " (12/39)");
	// with a protected prefix and a multi-byte middle
	char t2[1200] = "Consoles | ";
	for (int i = 0; i < 200; i++)
		strcat(t2, "ポ");
	UI_titleFit(t2, " (1/2)", 1000000, measure_cp, NULL, out, sizeof(out));
	assert(strlen(out) < sizeof(out));
	assert(strncmp(out, "Consoles | ポ", strlen("Consoles | ポ")) == 0);
	ends_with(out, "ポ" ELL " (1/2)");
}

static void test_ascii_ellipsis_option(void) {
	char out[128];
	UI_titleFitEx("The Legend of Zelda", NULL, 100, measure_cp, NULL, "...", out, sizeof(out));
	assert(strcmp(out, "The Leg...") == 0);
	assert(measure_cp(out, NULL) <= 100);
}

static void test_empty_and_null(void) {
	char out[32] = "x";
	int w = UI_titleFit("", NULL, 100, measure_cp, NULL, out, sizeof(out));
	assert(out[0] == '\0' && w == 0);
	w = UI_titleFit(NULL, NULL, 100, measure_cp, NULL, out, sizeof(out));
	assert(out[0] == '\0' && w == 0);
}

// The fit cache key covers the whole string: long titles sharing a 255-byte prefix differ.
static void test_key_covers_whole_input(void) {
	char a[400], b[400];
	memset(a, 'x', 300);
	a[300] = '\0';
	memcpy(b, a, sizeof(a));
	b[299] = 'y';
	assert(strncmp(a, b, 256) == 0);
	assert(!UI_titleFit_keyEq(UI_titleFit_key(a), UI_titleFit_key(b)));
	b[299] = 'x';
	b[300] = 'x';
	b[301] = '\0';
	assert(!UI_titleFit_keyEq(UI_titleFit_key(a), UI_titleFit_key(b)));
	assert(UI_titleFit_keyEq(UI_titleFit_key(a), UI_titleFit_key(a)));
	assert(UI_titleFit_keyEq(UI_titleFit_key(NULL), UI_titleFit_key("")));
}

int main(void) {
	test_fits_whole();
	test_middle_cut_keeps_prefix_and_suffix();
	test_middle_cut_trims_trailing_space();
	test_utf8_cut_on_code_point_boundary();
	test_prefix_and_suffix_too_wide_drop_prefix();
	test_ra_game_title_at_3x();
	test_dropped_prefix_middle_still_cut();
	test_no_separator_plain_end_cut();
	test_only_first_separator_protected();
	test_small_buffer_is_safe();
	test_prefix_cut_drops_dangling_separator();
	test_title_longer_than_buffer();
	test_ascii_ellipsis_option();
	test_empty_and_null();
	test_key_covers_whole_input();
	printf("test_title_fit: all passed\n");
	return 0;
}
