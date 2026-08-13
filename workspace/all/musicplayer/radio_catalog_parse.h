#ifndef __RADIO_CATALOG_PARSE_H__
#define __RADIO_CATALOG_PARSE_H__

#include "radio.h" // CuratedCountry, CuratedStation

// Parse radio-browser /json/countries text into out[] (max entries).
// Drops entries with stationcount==0; orders home_code
// first (case-insensitive), the rest A-Z by name. Returns count or -1.
int RadioCatalog_parseCountries(const char* json, CuratedCountry* out, int max,
								const char* home_code);

// Parse radio-browser /json/stations/bycountrycodeexact text into out[].
// url_resolved (fallback url) -> url; first tag -> genre;
// "<CODEC> · <bitrate>kbps" -> slogan (bitrate omitted if 0); code -> country_code.
// Returns count or -1.
int RadioCatalog_parseStations(const char* json, CuratedStation* out, int max,
							   const char* code);

// Keep only [A-Za-z0-9], preserve case, NUL-terminate. Blocks shell/path injection.
void RadioCatalog_sanitizeCode(const char* in, char* out, int out_sz);

// Sort: home_code first (case-insensitive), rest A-Z by name (case-insensitive).
void RadioCatalog_sortCountriesHomeFirst(CuratedCountry* arr, int n,
										 const char* home_code);

#endif // __RADIO_CATALOG_PARSE_H__
