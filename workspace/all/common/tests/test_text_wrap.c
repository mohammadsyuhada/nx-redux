// Host unit test for text_wrap.h (pure, no SDL). Run via run_tests.sh, which
// builds it with AddressSanitizer so buffer overruns fail loudly.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../text_wrap.h"

// Fake font: every byte is 10 px wide.
static int measure_10px(void* ctx, const char* s) {
	(void)ctx;
	return 10 * (int)strlen(s);
}

static int count_lines(const char* s) {
	int n = 1;
	for (; *s; s++)
		if (*s == '\n')
			n++;
	return n;
}

// Regression: the v1.10.0 release notes on the Brick. A long paragraph capped
// at a few lines left a >512-byte tail for the final ellipsis pass, which was
// copied into a fixed 512-byte stack buffer and crashed Settings.
static void test_long_tail_does_not_overflow(void) {
	// 200 words of 4 chars = 1000 bytes; at 10 px/byte and 100 px max width
	// two words fit per line, so with max_lines=2 the tail is ~990 bytes.
	char* str = malloc(1100);
	str[0] = '\0';
	for (int i = 0; i < 200; i++)
		strcat(str, i ? " word" : "word");
	assert(strlen(str) == 999);

	TextWrap_wrap(measure_10px, NULL, str, 100, 2);

	assert(count_lines(str) == 2);
	const char* last = strchr(str, '\n') + 1;
	assert(measure_10px(NULL, last) <= 100);
	assert(strlen(last) >= 3 && strcmp(last + strlen(last) - 3, "...") == 0);
	free(str);
}

// A first word wider than the line used to dereference a NULL "previous
// space" pointer when the wrapper tried to break before it.
static void test_first_word_wider_than_line(void) {
	char str[64] = "supercalifragilistic and more";

	TextWrap_wrap(measure_10px, NULL, str, 100, 2);

	assert(count_lines(str) <= 2);
	assert(strncmp(str, "supercalifragilistic", 20) == 0);
}

// Regression (device, title tiles and the Backdrop placeholder): a name needing 3+ lines lost a line and the next
// line's first letter ("Pasta La" / "" / "ista Sup..."). After a break the "previous space" pointed at the new
// line's first character, so the next break overwrote that letter with '\n'.
static void test_three_lines_keep_every_word(void) {
	char str[64] = "Pasta La Vista Super Mario Bros";
	// 10 px a byte, 90 px lines: "Pasta La" (80) fits, "Pasta La Vista" (140) doesn't
	TextWrap_wrap(measure_10px, NULL, str, 90, 3);
	assert(count_lines(str) == 3);
	char* l1 = str;
	char* l2 = strchr(l1, '\n') + 1;
	char* l3 = strchr(l2, '\n') + 1;
	assert(strncmp(l1, "Pasta La\n", 9) == 0);
	assert(strncmp(l2, "Vista\n", 6) == 0);
	assert(strncmp(l3, "Super", 5) == 0 && strstr(l3, "...")); // "Super Mario Bros" cut to 90 px
	assert(measure_10px(NULL, l3) <= 90);
}

// Unlimited lines: every word lands on a line of its own or with neighbours, none is lost or cut.
static void test_unlimited_lines_lose_nothing(void) {
	char str[128] = "Digimon Digital Monsters Anode Cathode Tamer";
	TextWrap_wrap(measure_10px, NULL, str, 100, 0);
	char joined[128];
	snprintf(joined, sizeof(joined), "%s", str);
	for (char* p = joined; *p; p++)
		if (*p == '\n')
			*p = ' ';
	assert(strcmp(joined, "Digimon Digital Monsters Anode Cathode Tamer") == 0);
	for (char* line = str; line;) {
		char* nl = strchr(line, '\n');
		int len = nl ? (int)(nl - line) : (int)strlen(line);
		assert(len > 0);		 // no blank line
		assert(len * 10 <= 100); // every word fits on its own here
		line = nl ? nl + 1 : NULL;
	}
}

// A word wider than the line in the middle of a 3-line block: it takes a line of its own and the rest follows.
static void test_wide_middle_word(void) {
	char str[64] = "ab supercalifragilistic cd ef";
	TextWrap_wrap(measure_10px, NULL, str, 60, 4);
	assert(strncmp(str, "ab\nsupercalifragilistic\ncd ef", 30) == 0);
}

// True when every multi-byte UTF-8 sequence in s is complete (no orphan lead byte, no stray continuation).
static int utf8_whole(const char* s) {
	const unsigned char* p = (const unsigned char*)s;
	while (*p) {
		int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1
							: (*p & 0xF0) == 0xE0	? 2
							: (*p & 0xF8) == 0xF0	? 3
													: -1;
		if (n < 0)
			return 0;
		p++;
		for (int i = 0; i < n; i++, p++)
			if ((*p & 0xC0) != 0x80)
				return 0;
	}
	return 1;
}

// The ellipsis pass drops whole code points: "Pokémon Red" cut where the old byte-wise loop kept only
// the lead byte of "é" ("Pok\xC3...") now gives "Pok...". Every width keeps the output whole UTF-8.
static void test_truncate_keeps_whole_code_points(void) {
	const char* in = "Pok\xC3\xA9mon Red";
	char out[32];
	TextWrap_truncate(measure_10px, NULL, in, out, sizeof(out), 75, 0);
	assert(strcmp(out, "Pok...") == 0);

	const char* cjk = "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3 Red"; // "ポケモン Red"
	for (int w = 0; w <= 200; w += 5) {
		TextWrap_truncate(measure_10px, NULL, in, out, sizeof(out), w, 0);
		assert(utf8_whole(out));
		TextWrap_truncate(measure_10px, NULL, cjk, out, sizeof(out), w, 0);
		assert(utf8_whole(out));
	}
	// fits whole: unchanged
	TextWrap_truncate(measure_10px, NULL, in, out, sizeof(out), 1000, 0);
	assert(strcmp(out, in) == 0);
}

int main(void) {
	test_long_tail_does_not_overflow();
	test_first_word_wider_than_line();
	test_three_lines_keep_every_word();
	test_unlimited_lines_lose_nothing();
	test_wide_middle_word();
	test_truncate_keeps_whole_code_points();
	printf("test_text_wrap: OK\n");
	return 0;
}
