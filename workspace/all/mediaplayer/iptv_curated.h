#ifndef __IPTV_CURATED_H__
#define __IPTV_CURATED_H__

#include <stdbool.h>
#include "iptv.h"

typedef struct {
	char name[64];
	char code[8];
} CuratedTVCountry;

typedef struct {
	char name[IPTV_MAX_NAME];
	char url[IPTV_MAX_URL];
	char category[IPTV_MAX_GROUP];
	char logo[IPTV_MAX_LOGO];
	char decryption_key[IPTV_MAX_KEY];
	char country_code[8];
} CuratedTVChannel;

void IPTV_curated_init(void);
void IPTV_curated_cleanup(void);

// Fetch (cache) + parse the iptv-org country list; orders the device's own
// country first. Returns country count, or -1 on failure (no cache, no network).
int IPTV_curated_loadCountries(bool force, volatile bool* should_stop, volatile int* progress);
int IPTV_curated_get_country_count(void);
const CuratedTVCountry* IPTV_curated_get_countries(void);

// Fetch (cache) + parse one country's channels into the shared buffer.
// Returns channel count, or -1 on failure.
int IPTV_curated_loadCountryChannels(const char* country_code, bool force,
									 volatile bool* should_stop, volatile int* progress);
// Channels for the currently-loaded country (0 if country_code isn't the loaded one).
const CuratedTVChannel* IPTV_curated_get_channels(const char* country_code, int* count);
int IPTV_curated_get_channel_count(const char* country_code);

// Pure: sort countries with home_code first (case-insensitive), rest A-Z by name.
void IPTV_curated_sortCountriesHomeFirst(CuratedTVCountry* arr, int n, const char* home_code);

#endif // __IPTV_CURATED_H__
