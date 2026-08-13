// Host unit test for radio_catalog_parse.c (pure, no network).
// Build & run (from workspace/all/musicplayer/):
//   cc -I. -I../include radio_catalog_parse.c ../include/parson/parson.c \
//      tests/test_radio_catalog.c -o /tmp/test_radio_catalog && /tmp/test_radio_catalog
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../radio_catalog_parse.h"

static const char* COUNTRIES =
	"[{\"name\":\"Andorra\",\"iso_3166_1\":\"AD\",\"stationcount\":8},"
	" {\"name\":\"Emptyland\",\"iso_3166_1\":\"EL\",\"stationcount\":0},"
	" {\"name\":\"Malaysia\",\"iso_3166_1\":\"MY\",\"stationcount\":42}]";

static const char* STATIONS =
	"[{\"name\":\"Jazz Lounge\",\"url\":\"http://x/\",\"url_resolved\":\"http://res/\","
	"  \"codec\":\"MP3\",\"bitrate\":320,\"tags\":\"jazz,smooth jazz\",\"countrycode\":\"MY\"},"
	" {\"name\":\"NoBitrate\",\"url\":\"http://y/\",\"url_resolved\":\"\","
	"  \"codec\":\"AAC\",\"bitrate\":0,\"tags\":\"\",\"countrycode\":\"MY\"}]";

static void test_countries_filter_and_map(void) {
	CuratedCountry c[8];
	int n = RadioCatalog_parseCountries(COUNTRIES, c, 8, "");
	assert(n == 2); // Emptyland (0 stations) filtered out
	// With empty home_code, order is A-Z by name: Andorra, Malaysia
	assert(strcmp(c[0].name, "Andorra") == 0);
	assert(strcmp(c[0].code, "AD") == 0);
	assert(strcmp(c[1].code, "MY") == 0);
}

static void test_countries_home_first(void) {
	CuratedCountry c[8];
	int n = RadioCatalog_parseCountries(COUNTRIES, c, 8, "my"); // lower-case home
	assert(n == 2);
	assert(strcmp(c[0].code, "MY") == 0); // home first, case-insensitive
	assert(strcmp(c[1].code, "AD") == 0);
}

static void test_stations_url_and_slogan(void) {
	CuratedStation s[8];
	int n = RadioCatalog_parseStations(STATIONS, s, 8, "MY");
	assert(n == 2);
	assert(strcmp(s[0].url, "http://res/") == 0);			  // url_resolved preferred
	assert(strcmp(s[0].genre, "jazz") == 0);				  // first tag only
	assert(strcmp(s[0].slogan, "MP3 \xC2\xB7 320kbps") == 0); // "MP3 · 320kbps"
	assert(strcmp(s[0].country_code, "MY") == 0);
	assert(strcmp(s[1].url, "http://y/") == 0); // fallback to url when resolved empty
	assert(strcmp(s[1].slogan, "AAC") == 0);	// bitrate 0 -> codec only
	assert(s[1].genre[0] == '\0');				// empty tags
}

// Stations whose fields contain the persistence delimiters '|'/newline
// (stations.txt is '|'-delimited and newline-terminated) — the parser must
// replace those bytes with spaces so saved stations round-trip intact.
static const char* STATIONS_DELIMS =
	"[{\"name\":\"REYFM | #original\\nX\",\"url\":\"http://z/\",\"url_resolved\":\"\","
	"  \"codec\":\"MP3\",\"bitrate\":128,\"tags\":\"rock|pop,news\",\"countrycode\":\"MY\"}]";

static void test_stations_delimiter_safety(void) {
	CuratedStation s[4];
	int n = RadioCatalog_parseStations(STATIONS_DELIMS, s, 4, "MY");
	assert(n == 1);
	assert(strchr(s[0].name, '|') == NULL);
	assert(strchr(s[0].name, '\n') == NULL);
	assert(strcmp(s[0].name, "REYFM   #original X") == 0); // '|' and '\n' -> ' '
	assert(strchr(s[0].genre, '|') == NULL);
	assert(strcmp(s[0].genre, "rock pop") == 0); // first tag, '|' -> ' '
}

// radio-browser lists the same stream under multiple rows (often with slightly
// different names). The saved store is URL-keyed (Radio_stationExists/add/remove
// all match by URL), so duplicate-URL rows would all show the same [+] state.
// The parser must drop later rows whose URL was already emitted, keeping the
// first (highest-voted, since the server pre-sorts by votes).
static const char* STATIONS_DUP =
	"[{\"name\":\"Jei FM Klang\",\"url\":\"http://a/\",\"url_resolved\":\"http://dup/\",\"countrycode\":\"MY\"},"
	" {\"name\":\"JEI FM\",\"url\":\"http://b/\",\"url_resolved\":\"http://dup/\",\"countrycode\":\"MY\"},"
	" {\"name\":\"Gamma\",\"url\":\"http://dup/\",\"url_resolved\":\"\",\"countrycode\":\"MY\"},"
	" {\"name\":\"Unique\",\"url\":\"http://uniq/\",\"url_resolved\":\"\",\"countrycode\":\"MY\"}]";

static void test_stations_dedup_by_url(void) {
	CuratedStation s[8];
	int n = RadioCatalog_parseStations(STATIONS_DUP, s, 8, "MY");
	assert(n == 2);									// three rows share http://dup/ (incl. Gamma via url fallback)
	assert(strcmp(s[0].name, "Jei FM Klang") == 0); // first occurrence kept
	assert(strcmp(s[0].url, "http://dup/") == 0);
	assert(strcmp(s[1].name, "Unique") == 0);
	assert(strcmp(s[1].url, "http://uniq/") == 0);
}

// radio-browser uses "unknown" as a placeholder tag; it must not surface as a
// genre (shown neither in the list nor the player).
static const char* STATIONS_UNKNOWN =
	"[{\"name\":\"A\",\"url\":\"http://a/\",\"url_resolved\":\"\",\"tags\":\"unknown\",\"countrycode\":\"MY\"},"
	" {\"name\":\"B\",\"url\":\"http://b/\",\"url_resolved\":\"\",\"tags\":\"UNKNOWN,rock\",\"countrycode\":\"MY\"},"
	" {\"name\":\"C\",\"url\":\"http://c/\",\"url_resolved\":\"\",\"tags\":\"jazz\",\"countrycode\":\"MY\"}]";

static void test_stations_unknown_genre(void) {
	CuratedStation s[8];
	int n = RadioCatalog_parseStations(STATIONS_UNKNOWN, s, 8, "MY");
	assert(n == 3);
	assert(s[0].genre[0] == '\0');			 // "unknown" -> empty
	assert(s[1].genre[0] == '\0');			 // "UNKNOWN" (case-insensitive) -> empty
	assert(strcmp(s[2].genre, "jazz") == 0); // a real tag is kept
}

static void test_sanitize_code(void) {
	char out[8];
	RadioCatalog_sanitizeCode("m-y/../x", out, sizeof(out));
	assert(strcmp(out, "myx") == 0);
	RadioCatalog_sanitizeCode("MY", out, sizeof(out));
	assert(strcmp(out, "MY") == 0); // case preserved
	RadioCatalog_sanitizeCode("", out, sizeof(out));
	assert(out[0] == '\0');
}

static void test_malformed(void) {
	CuratedCountry c[4];
	assert(RadioCatalog_parseCountries("{not an array}", c, 4, "") == -1);
	assert(RadioCatalog_parseCountries("", c, 4, "") == -1);
	CuratedStation s[4];
	assert(RadioCatalog_parseStations("null", s, 4, "MY") == -1);
}

int main(void) {
	test_countries_filter_and_map();
	test_countries_home_first();
	test_stations_url_and_slogan();
	test_stations_delimiter_safety();
	test_stations_dedup_by_url();
	test_stations_unknown_genre();
	test_sanitize_code();
	test_malformed();
	printf("all radio_catalog_parse tests passed\n");
	return 0;
}
