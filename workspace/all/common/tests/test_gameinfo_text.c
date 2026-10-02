#include "../../nextui/gameinfo_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DAY 86400
static char buf[256];

// 2026-09-30 12:00 UTC
static const time_t NOW = 1790769600;

static const char* when(long days_ago, long extra_secs) {
	GameInfo_whenText(NOW, NOW - days_ago * DAY - extra_secs, buf, sizeof(buf));
	return buf;
}

static void when_counts_24h_spans(void) {
	assert(strcmp(when(0, 3600), "Today") == 0);
	assert(strcmp(when(0, 13 * 3600), "Today") == 0); // 13 h ago is still within 24 h
	assert(strcmp(when(1, 0), "Yesterday") == 0);
	assert(strcmp(when(1, 23 * 3600), "Yesterday") == 0); // 47 h
	assert(strcmp(when(2, 0), "2 days ago") == 0);
	assert(strcmp(when(6, 0), "6 days ago") == 0);
	assert(strcmp(when(7, 0), "Last week") == 0);
	assert(strcmp(when(13, 0), "Last week") == 0);
	assert(strcmp(when(14, 0), "2 weeks ago") == 0);
	assert(strcmp(when(29, 0), "4 weeks ago") == 0);
	assert(strcmp(when(30, 0), "Last month") == 0);
	assert(strcmp(when(59, 0), "Last month") == 0);
	assert(strcmp(when(60, 0), "2 months ago") == 0);
	assert(strcmp(when(364, 0), "12 months ago") == 0);
	assert(strcmp(when(365, 0), "Last year") == 0);
	assert(strcmp(when(730, 0), "2 years ago") == 0);
	GameInfo_whenText(NOW, NOW + DAY, buf, sizeof(buf)); // clock went backwards
	assert(strcmp(buf, "Today") == 0);
}

static void duration_text(void) {
	GameInfo_durationText(91, buf, sizeof(buf));
	assert(strcmp(buf, "1m 31s") == 0);
	GameInfo_durationText(45, buf, sizeof(buf));
	assert(strcmp(buf, "45s") == 0);
	GameInfo_durationText(3600 * 25 + 51 * 60 + 9, buf, sizeof(buf));
	assert(strcmp(buf, "25h 51m") == 0);
	GameInfo_durationText(0, buf, sizeof(buf));
	assert(strcmp(buf, "0s") == 0);
	GameInfo_durationText(-1, buf, sizeof(buf));
	assert(buf[0] == '\0');
}

static void segments_full_and_partial(void) {
	InfoSeg s[3];
	int n = GameInfo_segments(NOW, NOW - 600, 91, 0, 35, "That Was Easy", true, s);
	assert(n == 3 && s[0].kind == INFO_SEG_TIME && strcmp(s[0].text, "Today - 1m 31s") == 0);
	assert(s[1].kind == INFO_SEG_ACH && strcmp(s[1].text, "0 of 35") == 0);
	assert(s[2].kind == INFO_SEG_NEXT && strcmp(s[2].text, "Next: That Was Easy") == 0);
	n = GameInfo_segments(NOW, NOW - 600, 91, 0, 35, "That Was Easy", false, s); // tile: no Next
	assert(n == 2);
	n = GameInfo_segments(NOW, 0, 91, 0, 0, NULL, true, s); // duration alone
	assert(n == 1 && strcmp(s[0].text, "1m 31s") == 0);
	n = GameInfo_segments(NOW, NOW - 2 * DAY, 0, 0, 0, NULL, true, s); // when alone
	assert(n == 1 && strcmp(s[0].text, "2 days ago") == 0);
	n = GameInfo_segments(NOW, 0, 0, 3, 10, "", true, s); // RA only, no next title
	assert(n == 1 && s[0].kind == INFO_SEG_ACH && strcmp(s[0].text, "3 of 10") == 0);
	n = GameInfo_segments(NOW, 0, 0, 0, 0, NULL, true, s);
	assert(n == 0);
}

static void segments_cut_utf8_safe(void) {
	char longname[400];
	memset(longname, 'a', sizeof(longname));
	// é é at 152: after the 6-byte "Next: " prefix they sit at 158..161, straddling the 159-byte cap
	memcpy(longname + 152, "\xC3\xA9\xC3\xA9", 4);
	longname[sizeof(longname) - 1] = '\0';
	InfoSeg s[3];
	int n = GameInfo_segments(NOW, 0, 0, 1, 2, longname, true, s);
	assert(n == 2);
	size_t len = strlen(s[1].text);
	assert(len < sizeof(s[1].text));
	// never ends on a lead or continuation byte of a cut sequence
	unsigned char last = (unsigned char)s[1].text[len - 1];
	assert(last < 0x80 || (last & 0xC0) == 0x80);
	assert(GameInfo_utf8Cut("ab\xC3\xA9", 3) == 2);
	assert(GameInfo_utf8Cut("ab\xC3\xA9", 4) == 4);
}

static void games_label(void) {
	GameInfo_gamesLabel(1, buf, sizeof(buf));
	assert(strcmp(buf, "1 game") == 0);
	GameInfo_gamesLabel(24, buf, sizeof(buf));
	assert(strcmp(buf, "24 games") == 0);
	GameInfo_gamesLabel(0, buf, sizeof(buf));
	assert(strcmp(buf, "0 games") == 0);
	GameInfo_gamesLabel(-1, buf, sizeof(buf));
	assert(buf[0] == '\0');
}

int main(void) {
	when_counts_24h_spans();
	duration_text();
	segments_full_and_partial();
	segments_cut_utf8_safe();
	games_label();
	printf("test_gameinfo_text: ok\n");
	return 0;
}
