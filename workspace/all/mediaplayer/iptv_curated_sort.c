#include "iptv_curated.h"
#include <string.h>
#include <strings.h> // strcasecmp
#include <stdlib.h>

static const char* g_home = "";

static int cmp_home_first(const void* a, const void* b) {
	const CuratedTVCountry* ca = a;
	const CuratedTVCountry* cb = b;
	int ha = (g_home[0] && strcasecmp(ca->code, g_home) == 0);
	int hb = (g_home[0] && strcasecmp(cb->code, g_home) == 0);
	if (ha != hb)
		return hb - ha; // home country sorts before all others
	return strcasecmp(ca->name, cb->name);
}

void IPTV_curated_sortCountriesHomeFirst(CuratedTVCountry* arr, int n, const char* home_code) {
	g_home = home_code ? home_code : "";
	qsort(arr, n, sizeof(CuratedTVCountry), cmp_home_first);
	g_home = "";
}
