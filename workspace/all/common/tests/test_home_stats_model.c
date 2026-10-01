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
	printf("test_home_stats_model: ok\n");
	return 0;
}
