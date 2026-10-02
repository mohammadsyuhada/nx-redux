// The List geometry under the info band: a fixed band reaching into the hint bar down to its ink, for main-menu tabs
// and game lists alike, with whole px rows filling the slot above it (InfoBand_fixedLayout, InfoBand_listFit).
// InfoBand_layout (the band on the bar, bottom to top: the hint bar, dp2 of solid ground, the text line, dp12 of fade)
// is the former game-list geometry, kept as the tests' reference. Pure, host-tested.

#include "infoband_layout.h"
#include "ui_hintbar_layout.h"

#include <stdbool.h>
#include <string.h>

InfoBandLayout InfoBand_layout(int screen_h, int bar_h, int list_top, int row_h, int text_h, int dp2, int dp12) {
	InfoBandLayout l;
	l.list_top = list_top;
	l.row_h = row_h;
	l.arrow_x = 0;
	l.text_h = text_h;
	l.band_bottom = screen_h - bar_h;
	l.fill_bottom = l.band_bottom;
	l.text_top = l.band_bottom - dp2 - text_h;
	l.band_top = l.text_top - dp12;
	l.rows = row_h > 0 ? (l.text_top - list_top) / row_h : 1;
	if (l.rows < 1)
		l.rows = 1;
	return l;
}

InfoBandLayout InfoBand_fixedLayout(int screen_h, int bar_h, int list_top, int pitch, int line_h, int pad,
									int overlap) {
	InfoBandLayout l;
	l.list_top = list_top;
	l.arrow_x = 0;
	l.text_h = line_h;
	l.fill_bottom = screen_h - bar_h;
	l.band_bottom = l.fill_bottom + overlap;
	l.band_top = l.band_bottom - (line_h + 2 * pad);
	l.text_top = l.band_top + pad;
	l.rows = InfoBand_listFit(l.band_top - list_top, pitch, &l.row_h);
	return l;
}

int InfoBand_listFit(int slot, int pitch, int* row_h) {
	// floor(slot / (0.95 * pitch)) in integers: a row may shrink by up to 5%, so a pixel-level shortfall never costs one
	int n = pitch > 0 && slot > 0 ? (slot * 20) / (pitch * 19) : 0;
	if (n < 2)
		n = 2;
	if (row_h)
		*row_h = slot > 0 ? slot / n : 0;
	return n;
}

int InfoBand_hintInkTop(int bar_h, int icon_h) {
	return UI_hintBarIconOffset(bar_h, icon_h) + INFOBAND_HINT_GLYPH_INK_MARGIN;
}

int InfoBand_overlap(int max, int ink_top) {
	int o = ink_top < max ? ink_top : max;
	return o > 0 ? o : 0;
}

int InfoBand_cutText(char* text, size_t size, int room, size_t min_bytes, InfoBand_measureFn measure, void* ctx) {
	if (!text || !measure || size < 4)
		return -1;
	char buf[512];
	if (size > sizeof(buf))
		size = sizeof(buf);
	size_t k = strlen(text);
	while (k > 0) {
		k--; // back one code point: past its continuation bytes to its lead byte
		while (k > 0 && ((unsigned char)text[k] & 0xC0) == 0x80)
			k--;
		while (k > 0 && text[k - 1] == ' ')
			k--;
		if (k <= min_bytes)
			return -1; // shorter cuts keep even less
		if (k + 4 > size)
			continue; // "<kept>..." must fit the buffer
		memcpy(buf, text, k);
		memcpy(buf + k, "...", 4);
		int w = measure(ctx, buf);
		if (w <= room) {
			memcpy(text, buf, k + 4);
			return w;
		}
	}
	return -1;
}

#define NEXT_PREFIX "Next: "
#define TIME_JOIN " - " // GameInfo_segments: "<when> - <duration>"

static int lineWidth(const InfoSeg* segs, int n, int sep_w, int trophy_w, InfoBand_measureFn measure, void* ctx) {
	int total = 0;
	for (int i = 0; i < n; i++) {
		total += measure(ctx, segs[i].text) + (segs[i].kind == INFO_SEG_ACH ? trophy_w : 0);
		if (i > 0)
			total += sep_w;
	}
	return total;
}

int InfoBand_fitSegments(InfoSeg* segs, int n, int avail, int sep_w, int trophy_w, InfoBand_measureFn measure,
						 void* ctx) {
	if (!segs || !measure)
		return 0;
	while (n > 0) {
		int total = lineWidth(segs, n, sep_w, trophy_w, measure, ctx);
		if (total <= avail)
			return n;
		InfoSeg* last = &segs[n - 1];
		bool cuttable = last->kind == INFO_SEG_NEXT || last->kind == INFO_SEG_COUNT;
		int room = avail - (total - measure(ctx, last->text));
		if (cuttable && room > 0) {
			size_t min_bytes = 0;
			if (last->kind == INFO_SEG_NEXT && strncmp(last->text, NEXT_PREFIX, strlen(NEXT_PREFIX)) == 0)
				min_bytes = strlen(NEXT_PREFIX);
			if (InfoBand_cutText(last->text, sizeof(last->text), room, min_bytes, measure, ctx) >= 0)
				return n;
		}
		if (last->kind != INFO_SEG_NEXT && segs[0].kind == INFO_SEG_TIME) {
			// time (+ "n of m"): the duration alone, then no time
			char* join = strstr(segs[0].text, TIME_JOIN);
			if (join) {
				memmove(segs[0].text, join + strlen(TIME_JOIN), strlen(join + strlen(TIME_JOIN)) + 1);
				continue;
			}
			memmove(segs, segs + 1, sizeof(InfoSeg) * (size_t)(n - 1));
			n--;
			continue;
		}
		n--; // drop the last segment and its separator
	}
	return 0;
}
