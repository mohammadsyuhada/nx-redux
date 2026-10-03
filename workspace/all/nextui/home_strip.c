// Home's stats strip: the lines' runs. SDL-free, host-tested by common/tests/test_home_strip.c.

#include "home_strip.h"

#include <stdio.h>
#include <string.h>

#include "gameinfo_text.h"

#define ELLIPSIS "\xE2\x80\xA6"
#define ENSP_EM 0.5f // the en space after "This month" (a pad: the UI font has no U+2002)

int HomeStrip_lineCount(const StripInput* in) {
	if (in->fresh)
		return 0;
	if (in->ready && in->total <= 0)
		return 1;
	return 2;
}

static StripRun* add(StripLine* l, StripTone tone, const char* text) {
	if (l->n >= (int)(sizeof(l->runs) / sizeof(l->runs[0])))
		return NULL;
	StripRun* r = &l->runs[l->n++];
	memset(r, 0, sizeof(*r));
	r->tone = tone;
	snprintf(r->text, sizeof(r->text), "%s", text);
	return r;
}

void HomeStrip_cleanTitle(const char* in, char* out, size_t size) {
	if (!out || size == 0)
		return;
	out[0] = '\0';
	if (!in)
		return;
	size_t o = 0;
	bool space = false;
	for (const char* p = in; *p; p++) {
		if (*p == '(' || *p == '[') { // a group to its first closing bracket (either kind), as the mockup's regex
			const char* q = p + 1;
			while (*q && *q != ')' && *q != ']')
				q++;
			if (*q) {
				p = q;
				continue;
			}
		}
		if (*p == ' ' || *p == '\t') {
			space = o > 0;
			continue;
		}
		if (space && o + 1 < size)
			out[o++] = ' ';
		space = false;
		if (o + 1 < size)
			out[o++] = *p;
	}
	out[o] = '\0';
}

void HomeStrip_build(const StripInput* in, StripLine* l1, StripLine* l2) {
	memset(l1, 0, sizeof(*l1));
	memset(l2, 0, sizeof(*l2));
	int lines = HomeStrip_lineCount(in);
	if (lines == 0)
		return;
	bool none = in->ready && in->total <= 0;
	char buf[64];

	// line 1: This month <time | No play yet | …> · <n achievements | Sign in>
	add(l1, STRIP_GREY, "This month");
	StripRun* v;
	if (none) {
		v = add(l1, STRIP_GREY, "No play yet");
	} else {
		if (in->ready)
			GameInfo_durationText(in->total, buf, sizeof(buf));
		else
			snprintf(buf, sizeof(buf), ELLIPSIS);
		v = add(l1, STRIP_WHITE, buf);
	}
	if (v)
		v->pad_l = ENSP_EM;
	StripRun* dot = add(l1, STRIP_DOT, "\xC2\xB7");
	if (dot)
		dot->pad_l = dot->pad_r = 0.28f;
	if (in->signed_in) {
		if (in->ready)
			snprintf(buf, sizeof(buf), "%d", none ? 0 : in->unlocks);
		else
			snprintf(buf, sizeof(buf), ELLIPSIS);
		add(l1, STRIP_WHITE, buf);
		add(l1, STRIP_GREY, " achievements");
	} else {
		add(l1, STRIP_GREY, "Sign in");
	}
	if (lines < 2)
		return;

	// line 2: Most played <title> <time>; before the first computation "…" and no time
	add(l2, STRIP_GREY, "Most played");
	StripRun* t = add(l2, STRIP_WHITE, "");
	if (!t)
		return;
	t->pad_l = 0.45f;
	if (!in->ready) {
		snprintf(t->text, sizeof(t->text), ELLIPSIS);
		return;
	}
	HomeStrip_cleanTitle(in->top_title, t->text, sizeof(t->text));
	t->gives_way = true;
	t->pad_r = 0.5f;
	GameInfo_durationText(in->top_seconds, buf, sizeof(buf));
	add(l2, STRIP_GREY, buf);
}
