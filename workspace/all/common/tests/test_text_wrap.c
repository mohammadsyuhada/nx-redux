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

int main(void) {
	test_long_tail_does_not_overflow();
	test_first_word_wider_than_line();
	printf("test_text_wrap: OK\n");
	return 0;
}
