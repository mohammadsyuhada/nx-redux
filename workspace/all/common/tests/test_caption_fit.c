#include "../../nextui/caption_fit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// A fixed-pitch measure: ctx is the px per code point (a 14 sp caption font's average advance).
static int measure(void* ctx, const char* text) {
	int per = *(int*)ctx, n = 0;
	for (const unsigned char* p = (const unsigned char*)text; *p; p++)
		if ((*p & 0xC0) != 0x80)
			n++;
	return n * per;
}

static InfoSeg seg(InfoSegKind k, const char* t) {
	InfoSeg s;
	s.kind = k;
	snprintf(s.text, sizeof(s.text), "%s", t);
	return s;
}

// The screens. Brick 1024×768 at scale 3: 14 sp ≈ 36 px, ~18 px a character; the separator " · " 3 characters; the
// trophy (12 dp × the line's ratio) and its 6 dp ≈ 26 + 13 px. Captions: the Backdrop-Vertical's side caption 446 px
// (527 … 973), the Carousel-Vertical's 410 px (the 40% guard), the horizontal captions 922 px (24 dp gutters).
// Smart Pro S 1280×720 at scale 2: ~12 px a character, trophy 17 + 9 px; side captions 658 / 553 px, horizontal 1212.
#define BRICK_CH 18
#define BRICK_SEP (3 * BRICK_CH)
#define BRICK_TROPHY 39
#define SPS_CH 12
#define SPS_SEP (3 * SPS_CH)
#define SPS_TROPHY 26

static void fits_unchanged(void) {
	int ch = BRICK_CH;
	CaptionFitRow r;
	// Brick Backdrop-V trophy row: "🏆 3 of 35 · Next: Win" = 39 + 7·18 + 54 + 9·18 = 381 ≤ 446
	InfoSeg row[2] = {seg(INFO_SEG_ACH, "3 of 35"), seg(INFO_SEG_NEXT, "Next: Win")};
	assert(CaptionFit_rowWidth(row, 2, BRICK_SEP, BRICK_TROPHY, measure, &ch) == 381);
	assert(!CaptionFit_split(row, 2, 446, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r));
	assert(r.head_n == 2 && r.next_lines == 0);
	// rows without Next are never split, however wide
	InfoSeg nonext[2] = {seg(INFO_SEG_TIME, "Yesterday - 1h 15m"), seg(INFO_SEG_ACH, "12 of 140")};
	assert(!CaptionFit_split(nonext, 2, 100, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r) && r.head_n == 2);
	// nothing before Next: not split (the draw cuts it as before)
	InfoSeg only[1] = {seg(INFO_SEG_NEXT, "Next: Defeat the Elite Four and become the Champion")};
	assert(!CaptionFit_split(only, 1, 446, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r) && r.head_n == 1);
}

static void splits(void) {
	int ch = BRICK_CH;
	CaptionFitRow r;
	// Brick Backdrop-V (446 px): "🏆 3 of 35 · Next: Defeat Brock" = 39 + 126 + 54 + 324 = 543 > 446: line 1 "🏆 3 of
	// 35", line 2 the whole Next (324 ≤ 446)
	InfoSeg row[2] = {seg(INFO_SEG_ACH, "3 of 35"), seg(INFO_SEG_NEXT, "Next: Defeat Brock")};
	assert(CaptionFit_split(row, 2, 446, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r));
	assert(r.head_n == 1 && r.next_lines == 1 && strcmp(r.next[0], "Next: Defeat Brock") == 0);
	// Next longer than a line: two lines, broken at a space, each fitting (Brick Carousel-V: 410 px = 22 characters)
	InfoSeg longer[2] = {seg(INFO_SEG_ACH, "3 of 35"), seg(INFO_SEG_NEXT, "Next: Defeat the Elite Four in one go")};
	assert(CaptionFit_split(longer, 2, 410, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r));
	assert(r.head_n == 1 && r.next_lines == 2);
	assert(strcmp(r.next[0], "Next: Defeat the Elite") == 0 && measure(&ch, r.next[0]) <= 410);
	assert(strcmp(r.next[1], "Four in one go") == 0);
	// longer than two lines: the second is cut with "..." to fit
	InfoSeg longest[2] = {seg(INFO_SEG_ACH, "3 of 35"),
						  seg(INFO_SEG_NEXT, "Next: Defeat the Elite Four and the Champion without using any items")};
	assert(CaptionFit_split(longest, 2, 410, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r));
	assert(r.next_lines == 2 && strcmp(r.next[0], "Next: Defeat the Elite") == 0);
	size_t l1 = strlen(r.next[1]);
	assert(l1 > 3 && strcmp(r.next[1] + l1 - 3, "...") == 0 && measure(&ch, r.next[1]) <= 410);
	assert(strncmp(r.next[1], "Four and the Champion", 10) == 0);
	// one free line only: Next on one line, cut
	assert(CaptionFit_split(longest, 2, 410, BRICK_SEP, BRICK_TROPHY, 1, measure, &ch, &r));
	assert(r.next_lines == 1 && strncmp(r.next[0], "Next: ", 6) == 0 && measure(&ch, r.next[0]) <= 410);
	assert(strcmp(r.next[0] + strlen(r.next[0]) - 3, "...") == 0);
	// no free line below the reserve: no split (the row is drawn and cut as before)
	assert(!CaptionFit_split(longest, 2, 410, BRICK_SEP, BRICK_TROPHY, 0, measure, &ch, &r));
	assert(r.head_n == 2 && r.next_lines == 0);
	// a word wider than the line breaks between code points (UTF-8 kept whole)
	InfoSeg word[2] = {seg(INFO_SEG_ACH, "1 of 2"), seg(INFO_SEG_NEXT, "Next: \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9")};
	int narrow = 5 * ch; // "Next:" fits, then 5 code points a line
	assert(CaptionFit_split(word, 2, narrow, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r));
	assert(r.next_lines == 2 && strcmp(r.next[0], "Next:") == 0);
	assert(measure(&ch, r.next[1]) <= narrow && (unsigned char)r.next[1][0] != 0x80);
	InfoSeg glued[2] = {seg(INFO_SEG_ACH, "1 of 2"),
						seg(INFO_SEG_NEXT, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9 ab")};
	assert(CaptionFit_split(glued, 2, narrow, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r));
	assert(r.next_lines == 2 && strlen(r.next[0]) == 10 && measure(&ch, r.next[0]) == narrow); // 5 code points
	assert(strcmp(r.next[1], "\xC3\xA9\xC3\xA9...") == 0);
}

static void measured_as_drawn(void) {
	int ch = BRICK_CH;
	CaptionFitRow r;
	// 7 + 12 characters + the separator: 396 px without the trophy, 435 with it. At 420 px it splits only because the
	// trophy and its gap are measured.
	InfoSeg row[2] = {seg(INFO_SEG_ACH, "3 of 35"), seg(INFO_SEG_NEXT, "Next: Badge!")};
	assert(CaptionFit_rowWidth(row, 2, BRICK_SEP, 0, measure, &ch) == 396);
	assert(!CaptionFit_split(row, 2, 420, BRICK_SEP, 0, 2, measure, &ch, &r));
	assert(CaptionFit_split(row, 2, 420, BRICK_SEP, BRICK_TROPHY, 2, measure, &ch, &r) && r.head_n == 1);
}

static void carousel_rows(void) {
	// the Carousel's one info line: "time · 🏆 n of m · Next: …" splits to "time · 🏆 n of m" + Next
	int ch = SPS_CH;
	CaptionFitRow r;
	InfoSeg row[3] = {seg(INFO_SEG_TIME, "Today - 1m 31s"), seg(INFO_SEG_ACH, "3 of 35"),
					  seg(INFO_SEG_NEXT, "Next: Catch a Pokemon in the Safari Zone and defeat the Elite Four")};
	// SPS horizontal Carousel (1212 px): 168 + 36 + 26 + 84 + 36 + 792 = 1142 fits
	assert(!CaptionFit_split(row, 3, 1212, SPS_SEP, SPS_TROPHY, 1, measure, &ch, &r) && r.head_n == 3);
	// SPS Carousel-V side caption (553 px): splits, Next on two lines
	assert(CaptionFit_split(row, 3, 553, SPS_SEP, SPS_TROPHY, 2, measure, &ch, &r));
	assert(r.head_n == 2 && r.next_lines == 2 && measure(&ch, r.next[0]) <= 553 && measure(&ch, r.next[1]) <= 553);
	assert(strncmp(r.next[0], "Next: Catch", 11) == 0);
	// SPS Backdrop-V side caption (658 px), a short Next on the trophy row: fits
	InfoSeg trophy[2] = {seg(INFO_SEG_ACH, "3 of 35"), seg(INFO_SEG_NEXT, "Next: Catch a Pokemon")};
	assert(!CaptionFit_split(trophy, 2, 658, SPS_SEP, SPS_TROPHY, 2, measure, &ch, &r));
	// Brick horizontal Carousel (922 px) with a long Next and one free line: one Next line, cut
	ch = BRICK_CH;
	assert(CaptionFit_split(row, 3, 922, BRICK_SEP, BRICK_TROPHY, 1, measure, &ch, &r));
	assert(r.head_n == 2 && r.next_lines == 1 && measure(&ch, r.next[0]) <= 922);
}

int main(void) {
	fits_unchanged();
	splits();
	measured_as_drawn();
	carousel_rows();
	printf("test_caption_fit: all passed\n");
	return 0;
}
