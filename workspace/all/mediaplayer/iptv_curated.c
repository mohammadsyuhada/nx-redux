#define _GNU_SOURCE
#include "iptv_curated.h"
#include "iptv_net.h"
#include "m3u.h"
#include "tz_country.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vp_defines.h" // defines.h -> SHARED_USERDATA_PATH; APP_DATA_DIR
#include "parson/parson.h"

#define IPTV_ORG_BASE "https://iptv-org.github.io"
#define CACHE_DIR APP_DATA_DIR "/tv/cache"
#define COUNTRIES_URL IPTV_ORG_BASE "/api/countries.json"
#define COUNTRIES_CACHE CACHE_DIR "/countries.json"

#define MAX_CURATED_COUNTRIES 300
#define MAX_CURATED_CHANNELS 512

static CuratedTVCountry curated_countries[MAX_CURATED_COUNTRIES];
static int curated_country_count = 0;

static CuratedTVChannel curated_channels[MAX_CURATED_CHANNELS];
static int curated_channel_count = 0;
static char loaded_country_code[8] = ""; // which country curated_channels holds

void IPTV_curated_init(void) {
	IPTV_net_ensureCacheDir(APP_DATA_DIR);
	IPTV_net_ensureCacheDir(APP_DATA_DIR "/tv");
	IPTV_net_ensureCacheDir(CACHE_DIR);
	curated_country_count = 0;
	curated_channel_count = 0;
	loaded_country_code[0] = '\0';
}

void IPTV_curated_cleanup(void) {
	curated_country_count = 0;
	curated_channel_count = 0;
	loaded_country_code[0] = '\0';
}

int IPTV_curated_loadCountries(bool force, volatile bool* should_stop, volatile int* progress) {
	if (IPTV_net_ensure(COUNTRIES_URL, COUNTRIES_CACHE, force, should_stop, progress) != 0)
		return -1;

	JSON_Value* root = json_parse_file(COUNTRIES_CACHE);
	if (!root)
		return -1;
	JSON_Array* arr = json_value_get_array(root);
	if (!arr) {
		json_value_free(root);
		return -1;
	}

	curated_country_count = 0;
	size_t n = json_array_get_count(arr);
	for (size_t i = 0; i < n && curated_country_count < MAX_CURATED_COUNTRIES; i++) {
		JSON_Object* o = json_array_get_object(arr, i);
		if (!o)
			continue;
		const char* name = json_object_get_string(o, "name");
		const char* code = json_object_get_string(o, "code");
		if (!name || !code)
			continue;
		CuratedTVCountry* c = &curated_countries[curated_country_count++];
		snprintf(c->name, sizeof(c->name), "%s", name);
		snprintf(c->code, sizeof(c->code), "%s", code);
	}
	json_value_free(root);

	char home[8];
	if (!TZ_currentCountryCode(home, sizeof(home)))
		home[0] = '\0';
	IPTV_curated_sortCountriesHomeFirst(curated_countries, curated_country_count, home);

	return curated_country_count;
}

int IPTV_curated_get_country_count(void) {
	return curated_country_count;
}

const CuratedTVCountry* IPTV_curated_get_countries(void) {
	return curated_countries;
}

// Lowercase a country code into dst for the URL/cache filename, keeping only
// [a-z0-9]. The code is third-party data (iptv-org countries.json) and flows
// into the fetch URL and a cache file path, so stripping other characters
// prevents shell-metacharacter injection and path traversal (e.g. "../").
static void lc_code(const char* code, char* dst, int dst_sz) {
	int j = 0;
	for (int i = 0; code[i] && j < dst_sz - 1; i++) {
		char ch = code[i];
		if (ch >= 'A' && ch <= 'Z')
			ch = (char)(ch - 'A' + 'a');
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
			dst[j++] = ch;
	}
	dst[j] = '\0';
}

int IPTV_curated_loadCountryChannels(const char* country_code, bool force,
									 volatile bool* should_stop, volatile int* progress) {
	if (!country_code || !country_code[0])
		return -1;

	char lc[8];
	lc_code(country_code, lc, sizeof(lc));

	char url[256];
	char cache[600];
	snprintf(url, sizeof(url), IPTV_ORG_BASE "/iptv/countries/%s.m3u", lc);
	snprintf(cache, sizeof(cache), "%s/%s.m3u", CACHE_DIR, lc);

	if (IPTV_net_ensure(url, cache, force, should_stop, progress) != 0)
		return -1;

	int n = M3U_parseFile(cache, curated_channels, MAX_CURATED_CHANNELS, country_code);
	if (n < 0)
		return -1;
	curated_channel_count = n;
	snprintf(loaded_country_code, sizeof(loaded_country_code), "%s", country_code);
	return n;
}

const CuratedTVChannel* IPTV_curated_get_channels(const char* country_code, int* count) {
	if (country_code && strcmp(country_code, loaded_country_code) == 0) {
		*count = curated_channel_count;
		return curated_channels;
	}
	*count = 0;
	return curated_channels;
}

int IPTV_curated_get_channel_count(const char* country_code) {
	if (country_code && strcmp(country_code, loaded_country_code) == 0)
		return curated_channel_count;
	return 0;
}
