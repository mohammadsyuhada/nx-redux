#ifndef __TZ_COUNTRY_H__
#define __TZ_COUNTRY_H__

#include <stdbool.h>

// Look up the ISO-3166-1 alpha-2 country code for an IANA zone name (e.g.
// "Asia/Kuala_Lumpur" -> "MY") using a zone.tab file
// (columns: country \t coords \t zone [\t comment]).
// Returns true and fills out (<= out_sz) on a match; false otherwise.
bool TZ_zoneToCountry(const char* zone, const char* zone_tab_path, char* out, int out_sz);

// Resolve the device's current country code from the system timezone
// (PLAT_getCurrentTimezone + /usr/share/zoneinfo/zone.tab). Returns false if
// unresolved (caller should then skip home-country pinning).
bool TZ_currentCountryCode(char* out, int out_sz);

#endif // __TZ_COUNTRY_H__
