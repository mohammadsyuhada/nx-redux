// caption_fit.c — the long "Next:" split in game-list captions (LIST-LAYOUT §8c.4). SDL-free.
#include "caption_fit.h"

#include <stdio.h>
#include <string.h>

#define NEXT_PREFIX "Next: "

int CaptionFit_rowWidth(const InfoSeg* segs, int n, int sep_w, int trophy_w, InfoBand_measureFn measure, void* ctx) {
	if (!segs || !measure)
		return 0;
	int total = 0;
	for (int i = 0; i < n; i++)
		total += (i ? sep_w : 0) + measure(ctx, segs[i].text) + (segs[i].kind == INFO_SEG_ACH ? trophy_w : 0);
	return total;
}

// The width of text's first `len` bytes.
static int prefixWidth(const char* text, size_t len, InfoBand_measureFn measure, void* ctx) {
	char buf[sizeof(((InfoSeg*)0)->text)];
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	memcpy(buf, text, len);
	buf[len] = '\0';
	return measure(ctx, buf);
}

// The next code point's end from byte i.
static size_t nextCodePoint(const char* t, size_t i) {
	if (!t[i])
		return i;
	i++;
	while (t[i] && ((unsigned char)t[i] & 0xC0) == 0x80)
		i++;
	return i;
}

// How many bytes of `text` go on one line of avail px: up to the last space where the text before it fits, else (a
// word wider than the line) the code points that fit, one at least.
static size_t lineBreak(const char* text, int avail, InfoBand_measureFn measure, void* ctx) {
	size_t len = strlen(text), best = 0;
	for (size_t i = 1; i < len; i++) {
		if (text[i] != ' ' || text[i - 1] == ' ')
			continue;
		if (prefixWidth(text, i, measure, ctx) > avail)
			break;
		best = i;
	}
	if (best > 0)
		return best;
	size_t k = nextCodePoint(text, 0);
	for (size_t j = nextCodePoint(text, k); j <= len && j > k; j = nextCodePoint(text, k)) {
		if (prefixWidth(text, j, measure, ctx) > avail)
			break;
		k = j;
	}
	return k;
}

bool CaptionFit_split(const InfoSeg* segs, int n, int avail, int sep_w, int trophy_w, int free_lines,
					  InfoBand_measureFn measure, void* ctx, CaptionFitRow* out) {
	if (!out)
		return false;
	memset(out, 0, sizeof(*out));
	out->head_n = n > 0 ? n : 0;
	if (!segs || !measure || n < 2 || avail <= 0 || free_lines <= 0)
		return false; // nothing before Next (or no Next), or no free line: no split
	if (segs[n - 1].kind != INFO_SEG_NEXT)
		return false;
	if (CaptionFit_rowWidth(segs, n, sep_w, trophy_w, measure, ctx) <= avail)
		return false; // it fits

	int lines = free_lines < CAPTION_FIT_NEXT_LINES ? free_lines : CAPTION_FIT_NEXT_LINES;
	const char* p = segs[n - 1].text;
	size_t min_bytes = strncmp(p, NEXT_PREFIX, strlen(NEXT_PREFIX)) == 0 ? strlen(NEXT_PREFIX) : 0;
	int made = 0;
	while (*p && made < lines) {
		char* line = out->next[made];
		size_t size = sizeof(out->next[0]);
		if (measure(ctx, p) <= avail) {
			snprintf(line, size, "%s", p);
			made++;
			break;
		}
		if (made == lines - 1) { // the last line: what's left, cut with "..."
			snprintf(line, size, "%s", p);
			InfoBand_cutText(line, size, avail, made == 0 ? min_bytes : 0, measure, ctx);
			made++;
			break;
		}
		size_t k = lineBreak(p, avail, measure, ctx);
		snprintf(line, size, "%.*s", (int)k, p);
		made++;
		p += k;
		while (*p == ' ')
			p++;
	}
	out->head_n = n - 1;
	out->next_lines = made;
	return made > 0;
}
