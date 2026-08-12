#define _GNU_SOURCE
#include "m3u.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Extract attr="value" from an #EXTINF line into out. Returns 1 if found.
static int extinf_attr(const char* line, const char* attr, char* out, int out_sz) {
	char needle[64];
	snprintf(needle, sizeof(needle), "%s=\"", attr);
	const char* p = strstr(line, needle);
	if (!p)
		return 0;
	p += strlen(needle);
	const char* end = strchr(p, '"');
	if (!end)
		return 0;
	int n = (int)(end - p);
	if (n >= out_sz)
		n = out_sz - 1;
	memcpy(out, p, n);
	out[n] = '\0';
	return 1;
}

static void rtrim(char* s) {
	int len = (int)strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' '))
		s[--len] = '\0';
}

// Display name = text after the last comma on the #EXTINF line.
static void extinf_name(const char* line, char* out, int out_sz) {
	const char* comma = strrchr(line, ',');
	const char* name = comma ? comma + 1 : line;
	snprintf(out, out_sz, "%s", name);
	rtrim(out);
}

int M3U_parseFile(const char* path, CuratedTVChannel* out, int max, const char* country_code) {
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;

	char line[2048];
	int count = 0;
	bool have_meta = false;
	CuratedTVChannel cur;
	memset(&cur, 0, sizeof(cur));

	while (count < max && fgets(line, sizeof(line), f)) {
		if (strncmp(line, "#EXTINF", 7) == 0) {
			memset(&cur, 0, sizeof(cur));
			char buf[512];
			if (extinf_attr(line, "tvg-logo", buf, sizeof(buf)))
				snprintf(cur.logo, IPTV_MAX_LOGO, "%s", buf);
			if (extinf_attr(line, "group-title", buf, sizeof(buf)))
				snprintf(cur.category, IPTV_MAX_GROUP, "%s", buf);
			extinf_name(line, buf, sizeof(buf));
			snprintf(cur.name, IPTV_MAX_NAME, "%s", buf);
			snprintf(cur.country_code, sizeof(cur.country_code), "%s", country_code ? country_code : "");
			have_meta = true;
		} else if (line[0] == '#') {
			continue; // other directives (#EXTM3U, #EXTVLCOPT, ...)
		} else {
			rtrim(line);
			if (line[0] == '\0')
				continue; // blank
			if (!have_meta)
				continue; // URL with no preceding #EXTINF
			snprintf(cur.url, IPTV_MAX_URL, "%s", line);
			out[count++] = cur;
			have_meta = false;
		}
	}

	fclose(f);
	return count;
}
