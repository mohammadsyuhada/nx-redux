// Home's stats strip (docs/home-b2.md, The stats strip): the runs of each state and the title cleaning.
#include "../../nextui/home_strip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ELLIPSIS "\xE2\x80\xA6"

static int run(const StripLine* l, int i, StripTone tone, const char* text) {
	return i < l->n && l->runs[i].tone == tone && strcmp(l->runs[i].text, text) == 0;
}

static void clean(const char* in, const char* want) {
	char out[160];
	HomeStrip_cleanTitle(in, out, sizeof(out));
	assert(strcmp(out, want) == 0);
}

int main(void) {
	StripLine a, b;
	// played this month: two lines
	StripInput in = {false, true, true, 12 * 3600 + 38 * 60, 2, "The Legend of Zelda - The Minish Cap (USA)", 3 * 3600 + 55 * 60};
	assert(HomeStrip_lineCount(&in) == 2);
	HomeStrip_build(&in, &a, &b);
	assert(a.n == 5 && run(&a, 0, STRIP_GREY, "This month") && run(&a, 1, STRIP_WHITE, "12h 38m"));
	assert(a.runs[1].pad_l == 0.5f); // the en space
	assert(run(&a, 2, STRIP_DOT, "\xC2\xB7") && a.runs[2].pad_l == 0.28f && a.runs[2].pad_r == 0.28f);
	assert(run(&a, 3, STRIP_WHITE, "2") && run(&a, 4, STRIP_GREY, " achievements"));
	assert(b.n == 3 && run(&b, 0, STRIP_GREY, "Most played"));
	assert(run(&b, 1, STRIP_WHITE, "The Legend of Zelda - The Minish Cap") && b.runs[1].gives_way);
	assert(run(&b, 2, STRIP_GREY, "3h 55m") && !b.runs[0].gives_way && !b.runs[2].gives_way);
	// not signed in: a grey "Sign in" for the achievements
	in.signed_in = false;
	HomeStrip_build(&in, &a, &b);
	assert(a.n == 4 && run(&a, 3, STRIP_GREY, "Sign in"));
	// nothing played this month: one line, "No play yet", 0 achievements
	in = (StripInput){false, true, true, 0, 0, NULL, 0};
	assert(HomeStrip_lineCount(&in) == 1);
	HomeStrip_build(&in, &a, &b);
	assert(run(&a, 0, STRIP_GREY, "This month") && run(&a, 1, STRIP_GREY, "No play yet") && a.runs[1].pad_l == 0.5f);
	assert(run(&a, 3, STRIP_WHITE, "0") && b.n == 0);
	// not computed yet: every value "…", both lines, no time on line 2
	in = (StripInput){false, false, true, 0, 0, NULL, 0};
	assert(HomeStrip_lineCount(&in) == 2);
	HomeStrip_build(&in, &a, &b);
	assert(run(&a, 1, STRIP_WHITE, ELLIPSIS) && run(&a, 3, STRIP_WHITE, ELLIPSIS));
	assert(b.n == 2 && run(&b, 1, STRIP_WHITE, ELLIPSIS) && !b.runs[1].gives_way);
	// fresh: no strip
	in.fresh = true;
	assert(HomeStrip_lineCount(&in) == 0);
	HomeStrip_build(&in, &a, &b);
	assert(a.n == 0 && b.n == 0);

	clean("Super Mario World (USA)", "Super Mario World");
	clean("Pokemon - Emerald Version (USA, Europe) [!] (Rev 1)", "Pokemon - Emerald Version");
	clean("  Tetris   [b1]  DX ", "Tetris DX");
	clean("Unclosed (paren", "Unclosed (paren");
	clean("", "");
	printf("test_home_strip: all passed\n");
	return 0;
}
