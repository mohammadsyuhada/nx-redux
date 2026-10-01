// Text for the selected game's info line (when played, play time, RetroAchievements) and count labels.
// SDL-free, host-tested by common/tests/test_gameinfo_text.c.

#include "gameinfo_text.h"

#include <stdio.h>
#include <string.h>

static void clearText(char* out, size_t size) {
	if (size)
		out[0] = '\0';
}

void GameInfo_whenText(time_t now, time_t then, char* out, size_t size) {
	long d = (long)((now - then) / 86400);
	if (d < 0)
		d = 0;
	if (d == 0)
		snprintf(out, size, "Today");
	else if (d == 1)
		snprintf(out, size, "Yesterday");
	else if (d < 7)
		snprintf(out, size, "%ld days ago", d);
	else if (d < 14)
		snprintf(out, size, "Last week");
	else if (d < 30)
		snprintf(out, size, "%ld weeks ago", d / 7);
	else if (d < 60)
		snprintf(out, size, "Last month");
	else if (d < 365)
		snprintf(out, size, "%ld months ago", d / 30);
	else if (d < 730)
		snprintf(out, size, "Last year");
	else
		snprintf(out, size, "%ld years ago", d / 365);
}

void GameInfo_durationText(int seconds, char* out, size_t size) {
	if (seconds < 0) {
		clearText(out, size);
		return;
	}
	int h = seconds / 3600;
	int m = seconds % 3600 / 60;
	int s = seconds % 60;
	if (h > 0)
		snprintf(out, size, "%dh %dm", h, m);
	else if (m > 0)
		snprintf(out, size, "%dm %ds", m, s);
	else
		snprintf(out, size, "%ds", s);
}

int GameInfo_segments(time_t now, time_t last, int seconds, int unlocked, int total, const char* next, bool full,
					  InfoSeg out[3]) {
	int n = 0;
	if (last > 0 || seconds > 0) {
		char when[64] = "";
		char dur[32] = "";
		if (last > 0)
			GameInfo_whenText(now, last, when, sizeof(when));
		if (seconds > 0)
			GameInfo_durationText(seconds, dur, sizeof(dur));
		out[n].kind = INFO_SEG_TIME;
		if (when[0] && dur[0])
			snprintf(out[n].text, sizeof(out[n].text), "%s - %s", when, dur);
		else
			snprintf(out[n].text, sizeof(out[n].text), "%s%s", when, dur);
		n++;
	}
	if (total > 0) {
		out[n].kind = INFO_SEG_ACH;
		snprintf(out[n].text, sizeof(out[n].text), "%d of %d", unlocked, total);
		n++;
		if (full && next && next[0]) {
			char line[512];
			snprintf(line, sizeof(line), "Next: %s", next);
			size_t keep = GameInfo_utf8Cut(line, sizeof(out[n].text) - 1);
			out[n].kind = INFO_SEG_NEXT;
			memcpy(out[n].text, line, keep);
			out[n].text[keep] = '\0';
			n++;
		}
	}
	return n;
}

void GameInfo_gamesLabel(int n, char* out, size_t size) {
	if (n < 0)
		clearText(out, size);
	else
		snprintf(out, size, n == 1 ? "%d game" : "%d games", n);
}

size_t GameInfo_utf8Cut(const char* text, size_t max_bytes) {
	size_t len = strlen(text);
	if (len <= max_bytes)
		return len;
	// text[max_bytes] is the first dropped byte; while it continues a sequence, drop that sequence's start too
	size_t cut = max_bytes;
	while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80)
		cut--;
	return cut;
}
