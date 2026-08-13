#define _GNU_SOURCE
#include "radio_catalog.h"
#include "radio_catalog_parse.h"
#include "net_cache.h"
#include "tz_country.h"
#include "defines.h" // SHARED_USERDATA_PATH
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RB_BASE "https://all.api.radio-browser.info"
#define CACHE_DIR SHARED_USERDATA_PATH "/music-player/radio/cache"
#define COUNTRIES_URL RB_BASE "/json/countries"
#define COUNTRIES_CACHE CACHE_DIR "/countries.json"

#define MAX_CATALOG_COUNTRIES 300
#define MAX_CATALOG_STATIONS 256

static CuratedCountry catalog_countries[MAX_CATALOG_COUNTRIES];
static int catalog_country_count = 0;

static CuratedStation catalog_stations[MAX_CATALOG_STATIONS];
static int catalog_station_count = 0;
static char loaded_country_code[8] = "";

// Read an entire file into a malloc'd, NUL-terminated buffer (caller frees).
static char* read_file(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	char* buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[rd] = '\0';
	return buf;
}

void RadioCatalog_init(void) {
	NetCache_ensureDir(SHARED_USERDATA_PATH "/music-player");
	NetCache_ensureDir(SHARED_USERDATA_PATH "/music-player/radio");
	NetCache_ensureDir(CACHE_DIR);
	catalog_country_count = 0;
	catalog_station_count = 0;
	loaded_country_code[0] = '\0';
}

void RadioCatalog_cleanup(void) {
	catalog_country_count = 0;
	catalog_station_count = 0;
	loaded_country_code[0] = '\0';
}

int RadioCatalog_loadCountries(bool force, volatile bool* should_stop, volatile int* progress) {
	if (NetCache_ensure(COUNTRIES_URL, COUNTRIES_CACHE, force, should_stop, progress) != 0)
		return -1;
	char* json = read_file(COUNTRIES_CACHE);
	if (!json)
		return -1;
	char home[8];
	if (!TZ_currentCountryCode(home, sizeof(home)))
		home[0] = '\0';
	int n = RadioCatalog_parseCountries(json, catalog_countries, MAX_CATALOG_COUNTRIES, home);
	free(json);
	if (n < 0)
		return -1;
	catalog_country_count = n;
	return n;
}

int RadioCatalog_getCountryCount(void) {
	return catalog_country_count;
}
const CuratedCountry* RadioCatalog_getCountries(void) {
	return catalog_countries;
}

int RadioCatalog_loadCountryStations(const char* code, bool force,
									 volatile bool* should_stop, volatile int* progress) {
	if (!code || !code[0])
		return -1;
	char safe[8];
	RadioCatalog_sanitizeCode(code, safe, sizeof(safe));
	if (!safe[0])
		return -1;

	char url[256];
	char cache[600];
	snprintf(url, sizeof(url),
			 RB_BASE "/json/stations/bycountrycodeexact/%s"
					 "?hidebroken=true&order=votes&reverse=true&limit=100",
			 safe);
	snprintf(cache, sizeof(cache), "%s/%s.json", CACHE_DIR, safe);

	if (NetCache_ensure(url, cache, force, should_stop, progress) != 0)
		return -1;
	char* json = read_file(cache);
	if (!json)
		return -1;
	int n = RadioCatalog_parseStations(json, catalog_stations, MAX_CATALOG_STATIONS, safe);
	free(json);
	if (n < 0)
		return -1;
	catalog_station_count = n;
	snprintf(loaded_country_code, sizeof(loaded_country_code), "%s", safe);
	return n;
}

const CuratedStation* RadioCatalog_getStations(const char* code, int* count) {
	if (code && strcmp(code, loaded_country_code) == 0) {
		*count = catalog_station_count;
		return catalog_stations;
	}
	*count = 0;
	return catalog_stations;
}

int RadioCatalog_getStationCount(const char* code) {
	if (code && strcmp(code, loaded_country_code) == 0)
		return catalog_station_count;
	return 0;
}
