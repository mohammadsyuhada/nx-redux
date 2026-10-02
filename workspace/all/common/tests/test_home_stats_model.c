#include "../../nextui/home_stats_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long long localTime(int y, int m, int d, int H, int M) {
	struct tm tm = {0};
	tm.tm_year = y - 1900;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_hour = H;
	tm.tm_min = M;
	tm.tm_isdst = -1;
	return (long long)mktime(&tm);
}

static void window_starts_monday_four_weeks_back(void) {
	long long now = localTime(2026, 10, 1, 15, 0); // Thursday
	long long ws = HomeStats_windowStart(now);
	assert(ws == localTime(2026, 8, 31, 0, 0)); // this week's Monday 2026-09-28, minus 4 weeks
}

static void midnight_split_and_clip(void) {
	long long now = localTime(2026, 10, 1, 15, 0);
	HomeSession s[] = {
		{1, "A", localTime(2026, 9, 30, 23, 30), localTime(2026, 10, 1, 0, 30)}, // 30 min each side of midnight
		{2, "B", localTime(2026, 8, 30, 23, 0), localTime(2026, 8, 31, 1, 0)},	 // starts before the window: 1 h in
		{3, "C", localTime(2026, 10, 1, 14, 0), 0},								 // open: clamps to now (1 h)
		{4, "D", localTime(2026, 9, 2, 10, 0), localTime(2026, 9, 3, 12, 0)},	 // 26 h: ignored
	};
	HomeStats st;
	HomeStats_compute(s, 4, now, &st);
	assert(st.ready && st.today == 31); // 2026-10-01 is the 32nd day from 2026-08-31
	assert(st.days[30] == 1800 && st.days[31] == 1800 + 3600);
	assert(st.days[0] == 3600);
	assert(st.total == 1800 + 1800 + 3600 + 3600);
	// A, B and C all have 3600 s in the window; ties go to the more recent last play: C (now), A, B
	assert(st.ntop == 3 && st.top[0].seconds == 3600);
	assert(st.top[0].group == 3 && st.top[1].group == 1 && st.top[2].group == 2);
}

static void dst_day_is_split_by_calendar(void) {
	// 2026-10-25 03:00 CEST → 02:00 CET: that local day is 25 h long
	long long now = localTime(2026, 10, 26, 12, 0);
	HomeSession s[] = {{1, "A", localTime(2026, 10, 25, 23, 0), localTime(2026, 10, 26, 1, 0)}};
	HomeStats st;
	HomeStats_compute(s, 1, now, &st);
	assert(st.days[st.today] == 3600 && st.days[st.today - 1] == 3600);
}

static void bad_lengths_ignored(void) {
	long long now = localTime(2026, 10, 1, 15, 0);
	long long t = localTime(2026, 9, 10, 12, 0);
	HomeSession s[] = {
		{1, "Zero", t, t},			  // zero length
		{2, "Negative", t, t - 60},	  // negative length
		{3, "Stale", now - 90000, 0}, // open for over 24 h: dropped
	};
	HomeStats st;
	HomeStats_compute(s, 3, now, &st);
	assert(st.ready && st.total == 0 && st.ntop == 0);
}

static void exactly_one_day_kept(void) {
	long long now = localTime(2026, 10, 1, 15, 0);
	HomeSession s[] = {{1, "A", localTime(2026, 9, 10, 12, 0), localTime(2026, 9, 11, 12, 0)}};
	HomeStats st;
	HomeStats_compute(s, 1, now, &st);
	assert(st.total == 86400);
	assert(st.days[10] == 43200 && st.days[11] == 43200); // 2026-09-10 is day 10 from 2026-08-31
	assert(st.ntop == 1 && st.top[0].seconds == 86400);
}

static void group_title_from_latest_session(void) {
	long long now = localTime(2026, 10, 1, 15, 0);
	HomeSession s[] = {
		{7, "New", localTime(2026, 9, 20, 10, 0), localTime(2026, 9, 20, 11, 0)},
		{7, "Old", localTime(2026, 9, 5, 10, 0), localTime(2026, 9, 5, 10, 30)},
		{8, "Other", localTime(2026, 9, 25, 10, 0), localTime(2026, 9, 25, 10, 10)},
	};
	HomeStats st;
	HomeStats_compute(s, 3, now, &st);
	assert(st.ntop == 2);
	assert(st.top[0].group == 7 && st.top[0].seconds == 5400);
	assert(strcmp(st.top[0].title, "New") == 0);
	assert(st.top[0].last == localTime(2026, 9, 20, 11, 0));
	assert(st.top[1].group == 8 && st.top[1].seconds == 600);
}

static void shade_steps(void) {
	assert(HomeStats_shadeAlpha(0) == 20);
	assert(HomeStats_shadeAlpha(1) == 51);
	assert(HomeStats_shadeAlpha(1799) == 51);
	assert(HomeStats_shadeAlpha(1800) == 115);
	assert(HomeStats_shadeAlpha(7199) == 115);
	assert(HomeStats_shadeAlpha(7200) == 204);
}

static HomeStatsKey sampleKey(void) {
	HomeStatsKey k = {20261001, 1, {1759300000, 40960}, {1759200000, 512}, {-1, -1}, {1759100000, 42}, {1759100500, 17}};
	return k;
}

static HomeStats sampleStats(void) {
	long long now = localTime(2026, 10, 1, 15, 0);
	HomeSession s[] = {
		{1, "Alpha", localTime(2026, 9, 30, 20, 0), localTime(2026, 9, 30, 21, 30)},
		{2, "Beta: Two\nLines", localTime(2026, 9, 1, 10, 0), localTime(2026, 9, 1, 10, 20)},
		{3, "", localTime(2026, 10, 1, 9, 0), localTime(2026, 10, 1, 9, 5)},
	};
	HomeStats st;
	HomeStats_compute(s, 3, now, &st);
	st.unlocks = 12;
	return st;
}

static void cache_round_trip(void) {
	HomeStatsKey key = sampleKey();
	HomeStats st = sampleStats();
	char buf[4096];
	size_t len = HomeStats_format(&key, &st, buf, sizeof(buf));
	assert(len > 0 && len == strlen(buf));
	HomeStats back;
	memset(&back, 0, sizeof(back));
	assert(HomeStats_parse(buf, &key, &back));
	assert(back.ready && back.window_start == st.window_start && back.today == st.today);
	assert(back.total == st.total && back.unlocks == 12 && back.ntop == 3);
	assert(memcmp(back.days, st.days, sizeof(st.days)) == 0);
	for (int i = 0; i < 3; i++) {
		assert(back.top[i].group == st.top[i].group && back.top[i].seconds == st.top[i].seconds);
		assert(back.top[i].last == st.top[i].last);
	}
	// the newline in a title is flattened so the file stays one entry per line; an empty title survives
	for (int i = 0; i < 3; i++) {
		if (st.top[i].group == 2)
			assert(strcmp(back.top[i].title, "Beta: Two Lines") == 0);
		else
			assert(strcmp(back.top[i].title, st.top[i].title) == 0);
	}
	// signed out (-1) round-trips too
	st.unlocks = -1;
	assert(HomeStats_format(&key, &st, buf, sizeof(buf)) > 0);
	assert(HomeStats_parse(buf, &key, &back) && back.unlocks == -1);
	// no room: nothing (never a truncated file)
	assert(HomeStats_format(&key, &st, buf, 40) == 0);
	// a not-ready result isn't cached
	st.ready = false;
	assert(HomeStats_format(&key, &st, buf, sizeof(buf)) == 0);
}

static void cache_key_mismatch_is_stale(void) {
	HomeStatsKey key = sampleKey();
	HomeStats st = sampleStats();
	char buf[4096];
	assert(HomeStats_format(&key, &st, buf, sizeof(buf)) > 0);
	HomeStats back;
	back.ready = false;
	HomeStatsKey k;
	k = key, k.day = 20261002;
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.signed_in = 0;
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.db.a++; // a game was played: the play-time DB changed
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.db.b += 4096;
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.journal.b = -1;
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.confirmed.a = 5;
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.games.b = 43; // a new RA game cached
	assert(!HomeStats_parse(buf, &k, &back));
	k = key, k.sessions.a++;
	assert(!HomeStats_parse(buf, &k, &back));
	assert(!back.ready); // untouched
	assert(HomeStats_keyEqual(&key, &key));
	k = key, k.sessions.b++;
	assert(!HomeStats_keyEqual(&key, &k));
}

static void cache_malformed_is_stale(void) {
	HomeStatsKey key = sampleKey();
	HomeStats st = sampleStats();
	char good[4096];
	size_t len = HomeStats_format(&key, &st, good, sizeof(good));
	assert(len > 0);
	HomeStats back;
	back.ready = false;
	assert(!HomeStats_parse(NULL, &key, &back));
	assert(!HomeStats_parse("", &key, &back));
	assert(!HomeStats_parse("homestats 2\n", &key, &back));
	// every truncation (a torn write that somehow survived) is rejected
	char buf[4096];
	for (size_t cut = 0; cut < len - 1; cut++) {
		memcpy(buf, good, cut);
		buf[cut] = '\0';
		assert(!HomeStats_parse(buf, &key, &back));
	}
	// a garbled number
	snprintf(buf, sizeof(buf), "%s", good);
	char* days = strstr(buf, "days ");
	assert(days);
	days[5] = 'x';
	assert(!HomeStats_parse(buf, &key, &back));
	// out-of-range fields
	snprintf(buf, sizeof(buf), "%s", good);
	char* stats = strstr(buf, "stats ");
	char* nl = strchr(stats, '\n');
	char rest[4096];
	snprintf(rest, sizeof(rest), "%s", nl);
	snprintf(stats, sizeof(buf) - (stats - buf), "stats %lld 99 %d %d %d%s", st.window_start, st.total, st.unlocks,
			 st.ntop, rest);
	assert(!HomeStats_parse(buf, &key, &back));
	snprintf(stats, sizeof(buf) - (stats - buf), "stats %lld %d %d %d 4%s", st.window_start, st.today, st.total,
			 st.unlocks, rest);
	assert(!HomeStats_parse(buf, &key, &back));
	// trailing junk on the key line
	snprintf(buf, sizeof(buf), "%s", good);
	char* key_end = strchr(strchr(buf, '\n') + 1, '\n');
	snprintf(rest, sizeof(rest), "%s", key_end);
	snprintf(key_end, sizeof(buf) - (key_end - buf), " 7%s", rest);
	assert(!HomeStats_parse(buf, &key, &back));
	assert(!back.ready);
	// the good text still parses
	assert(HomeStats_parse(good, &key, &back) && back.ready);
}

static void day_rolls_at_local_midnight(void) {
	HomeStatsKey before = sampleKey(), after = sampleKey();
	before.day = HomeStats_localDay(localTime(2026, 10, 1, 23, 59));
	after.day = HomeStats_localDay(localTime(2026, 10, 2, 0, 0));
	assert(before.day == 20261001 && after.day == 20261002);
	assert(!HomeStats_keyEqual(&before, &after)); // shown after midnight: recompute
	assert(HomeStats_localDay(localTime(2026, 12, 31, 23, 0)) == 20261231);
	assert(HomeStats_localDay(localTime(2027, 1, 1, 0, 30)) == 20270101);
}

int main(void) {
	setenv("TZ", "Europe/Berlin", 1);
	tzset();
	window_starts_monday_four_weeks_back();
	midnight_split_and_clip();
	dst_day_is_split_by_calendar();
	bad_lengths_ignored();
	exactly_one_day_kept();
	group_title_from_latest_session();
	shade_steps();
	cache_round_trip();
	cache_key_mismatch_is_stale();
	cache_malformed_is_stale();
	day_rolls_at_local_midnight();
	printf("test_home_stats_model: ok\n");
	return 0;
}
