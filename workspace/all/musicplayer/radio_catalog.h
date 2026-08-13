#ifndef __RADIO_CATALOG_H__
#define __RADIO_CATALOG_H__

#include <stdbool.h>
#include "radio.h" // CuratedCountry, CuratedStation

void RadioCatalog_init(void);
void RadioCatalog_cleanup(void);

// Fetch (cache) + parse the radio-browser country list; home country first.
// Returns country count, or -1 on failure (no cache, no network).
int RadioCatalog_loadCountries(bool force, volatile bool* should_stop, volatile int* progress);
int RadioCatalog_getCountryCount(void);
const CuratedCountry* RadioCatalog_getCountries(void);

// Fetch (cache) + parse one country's stations into the shared buffer.
// Returns station count, or -1 on failure.
int RadioCatalog_loadCountryStations(const char* code, bool force,
									 volatile bool* should_stop, volatile int* progress);
const CuratedStation* RadioCatalog_getStations(const char* code, int* count);
int RadioCatalog_getStationCount(const char* code);

#endif // __RADIO_CATALOG_H__
