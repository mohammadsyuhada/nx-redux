#include "home_stats_model.h"

#include <stdio.h>
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
	tm.tm_mday = 1;
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

int HomeStats_localDay(long long now) {
	time_t tt = (time_t)now;
	struct tm tm;
	localtime_r(&tt, &tm);
	return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

static bool sigEqual(HomeStatsSig x, HomeStatsSig y) {
	return x.a == y.a && x.b == y.b;
}

bool HomeStats_keyEqual(const HomeStatsKey* x, const HomeStatsKey* y) {
	return x->day == y->day && x->signed_in == y->signed_in && sigEqual(x->db, y->db) &&
		   sigEqual(x->journal, y->journal) && sigEqual(x->confirmed, y->confirmed) &&
		   sigEqual(x->games, y->games) && sigEqual(x->sessions, y->sessions);
}

#define HOME_CACHE_MAGIC "homestats 2" // 2: the calendar month (1: the 35-day heatmap window)

// Format, one item per line:
//   homestats 2
//   key <day> <signed_in> <db> <journal> <confirmed> <games> <sessions>   (each sig = two numbers)
//   stats <window_start> <today> <total> <unlocks> <ntop>
//   days <35 numbers>
//   top <group> <seconds> <last> <title to end of line>   (ntop lines)
//   end
size_t HomeStats_format(const HomeStatsKey* key, const HomeStats* st, char* buf, size_t size) {
	if (!st->ready || size == 0)
		return 0;
	size_t len = 0;
	int n;
#define APPEND(...)                                       \
	do {                                                  \
		n = snprintf(buf + len, size - len, __VA_ARGS__); \
		if (n < 0 || (size_t)n >= size - len)             \
			return 0;                                     \
		len += (size_t)n;                                 \
	} while (0)
	APPEND("%s\nkey %d %d %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld\n", HOME_CACHE_MAGIC, key->day,
		   key->signed_in, key->db.a, key->db.b, key->journal.a, key->journal.b, key->confirmed.a, key->confirmed.b,
		   key->games.a, key->games.b, key->sessions.a, key->sessions.b);
	APPEND("stats %lld %d %d %d %d\ndays", st->window_start, st->today, st->total, st->unlocks, st->ntop);
	for (int i = 0; i < HOME_DAYS; i++)
		APPEND(" %d", st->days[i]);
	APPEND("\n");
	for (int i = 0; i < st->ntop; i++) {
		char title[sizeof(st->top[i].title)];
		snprintf(title, sizeof(title), "%s", st->top[i].title);
		for (char* c = title; *c; c++) {
			if (*c == '\n' || *c == '\r')
				*c = ' '; // one line per entry
		}
		APPEND("top %d %d %lld %s\n", st->top[i].group, st->top[i].seconds, st->top[i].last, title);
	}
	APPEND("end\n");
#undef APPEND
	return len;
}

// The next line of *text into line (without its newline); false at the end or when it doesn't fit.
static bool nextLine(const char** text, char* line, size_t size) {
	const char* p = *text;
	if (!*p)
		return false;
	const char* eol = strchr(p, '\n');
	size_t n = eol ? (size_t)(eol - p) : strlen(p);
	if (n >= size)
		return false;
	memcpy(line, p, n);
	line[n] = '\0';
	*text = eol ? eol + 1 : p + n;
	return true;
}

bool HomeStats_parse(const char* text, const HomeStatsKey* key, HomeStats* out) {
	if (!text)
		return false;
	char line[512];
	if (!nextLine(&text, line, sizeof(line)) || strcmp(line, HOME_CACHE_MAGIC) != 0)
		return false;

	HomeStatsKey k;
	int end = 0;
	if (!nextLine(&text, line, sizeof(line)) ||
		sscanf(line, "key %d %d %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld%n", &k.day, &k.signed_in, &k.db.a,
			   &k.db.b, &k.journal.a, &k.journal.b, &k.confirmed.a, &k.confirmed.b, &k.games.a, &k.games.b,
			   &k.sessions.a, &k.sessions.b, &end) != 12 ||
		line[end] != '\0' || !HomeStats_keyEqual(&k, key))
		return false;

	HomeStats st;
	memset(&st, 0, sizeof(st));
	end = 0;
	if (!nextLine(&text, line, sizeof(line)) ||
		sscanf(line, "stats %lld %d %d %d %d%n", &st.window_start, &st.today, &st.total, &st.unlocks, &st.ntop,
			   &end) != 5 ||
		line[end] != '\0' || st.today < 0 || st.today >= HOME_DAYS || st.ntop < 0 || st.ntop > 3)
		return false;

	if (!nextLine(&text, line, sizeof(line)) || strncmp(line, "days", 4) != 0)
		return false;
	const char* p = line + 4;
	for (int i = 0; i < HOME_DAYS; i++) {
		int used = 0;
		if (sscanf(p, " %d%n", &st.days[i], &used) != 1)
			return false;
		p += used;
	}
	if (*p)
		return false;

	for (int i = 0; i < st.ntop; i++) {
		HomeTop* t = &st.top[i];
		int title_at = 0;
		if (!nextLine(&text, line, sizeof(line)) ||
			sscanf(line, "top %d %d %lld %n", &t->group, &t->seconds, &t->last, &title_at) != 3 || title_at == 0)
			return false;
		snprintf(t->title, sizeof(t->title), "%s", line + title_at);
	}
	if (!nextLine(&text, line, sizeof(line)) || strcmp(line, "end") != 0)
		return false;

	st.ready = true;
	*out = st;
	return true;
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
