#include "home_stats_model.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HOME_MAX_SESSION 86400 // longer sessions are bogus (gametimedb's MAX_PLAUSIBLE_PLAY_TIME)

// Local midnight `days` calendar days after the date of `t` (DST-safe: mktime normalises the date).
static long long dayStart(long long t, int days) {
	time_t tt = (time_t)t;
	struct tm tm;
	localtime_r(&tt, &tm);
	tm.tm_mday += days;
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;
	return (long long)mktime(&tm);
}

long long HomeStats_windowStart(long long now) {
	time_t tt = (time_t)now;
	struct tm tm;
	localtime_r(&tt, &tm);
	int days_since_monday = (tm.tm_wday + 6) % 7;
	tm.tm_mday -= days_since_monday + 28;
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;
	return (long long)mktime(&tm);
}

static int compareTop(const void* a, const void* b) {
	const HomeTop* x = a;
	const HomeTop* y = b;
	if (x->seconds != y->seconds)
		return x->seconds > y->seconds ? -1 : 1;
	if (x->last != y->last)
		return x->last > y->last ? -1 : 1;
	return x->group < y->group ? -1 : x->group > y->group;
}

void HomeStats_compute(const HomeSession* s, int n, long long now, HomeStats* out) {
	memset(out, 0, sizeof(*out));
	out->unlocks = -1;
	out->window_start = HomeStats_windowStart(now);

	long long edge[HOME_DAYS + 1];
	for (int i = 0; i <= HOME_DAYS; i++)
		edge[i] = dayStart(out->window_start, i);
	out->today = HOME_DAYS - 1;
	for (int i = 0; i < HOME_DAYS; i++) {
		if (now < edge[i + 1]) {
			out->today = i;
			break;
		}
	}

	HomeTop* groups = n > 0 ? calloc(n, sizeof(HomeTop)) : NULL;
	int ngroups = 0;
	for (int k = 0; k < n; k++) {
		long long start = s[k].start;
		long long end = s[k].end > 0 ? s[k].end : now;
		if (end - start <= 0 || end - start > HOME_MAX_SESSION)
			continue;
		if (start < out->window_start)
			start = out->window_start;
		if (end > now)
			end = now;
		if (end <= start)
			continue;

		int secs = 0;
		for (int i = 0; i < HOME_DAYS; i++) {
			long long lo = start > edge[i] ? start : edge[i];
			long long hi = end < edge[i + 1] ? end : edge[i + 1];
			if (hi > lo) {
				out->days[i] += (int)(hi - lo);
				secs += (int)(hi - lo);
			}
		}
		if (secs <= 0 || !groups)
			continue;

		HomeTop* g = NULL;
		for (int j = 0; j < ngroups; j++) {
			if (groups[j].group == s[k].group) {
				g = &groups[j];
				break;
			}
		}
		if (!g) {
			g = &groups[ngroups++];
			g->group = s[k].group;
		}
		g->seconds += secs;
		if (end >= g->last) { // the title of the most recent session
			g->last = end;
			strncpy(g->title, s[k].title, sizeof(g->title) - 1);
			g->title[sizeof(g->title) - 1] = '\0';
		}
	}

	for (int i = 0; i < HOME_DAYS; i++)
		out->total += out->days[i];

	if (ngroups > 1)
		qsort(groups, ngroups, sizeof(HomeTop), compareTop);
	out->ntop = ngroups < 3 ? ngroups : 3;
	for (int i = 0; i < out->ntop; i++)
		out->top[i] = groups[i];
	free(groups);
	out->ready = true;
}

int HomeStats_shadeAlpha(int seconds) {
	if (seconds <= 0)
		return 20; // 8%
	if (seconds < 30 * 60)
		return 51; // 20%
	if (seconds < 2 * 3600)
		return 115; // 45%
	return 204;		// 80%
}
