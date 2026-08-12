#include "tz_country.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward-declare the platform accessor (avoids pulling in heavy api.h here).
// Returns a malloc'd zone name (caller frees), or NULL.
extern char* PLAT_getCurrentTimezone(void);

#ifndef TZ_ZONE_TAB_PATH
#define TZ_ZONE_TAB_PATH "/usr/share/zoneinfo/zone.tab"
#endif

bool TZ_zoneToCountry(const char* zone, const char* zone_tab_path, char* out, int out_sz) {
	if (!zone || !zone[0])
		return false;
	FILE* f = fopen(zone_tab_path, "r");
	if (!f)
		return false;

	char line[512];
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#')
			continue;
		char* cc = strtok(line, "\t");
		if (!cc)
			continue;
		(void)strtok(NULL, "\t"); // coords
		char* zn = strtok(NULL, "\t\n");
		if (!zn)
			continue;
		if (strcmp(zn, zone) == 0) {
			snprintf(out, out_sz, "%s", cc);
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

bool TZ_currentCountryCode(char* out, int out_sz) {
	char* zone = PLAT_getCurrentTimezone();
	if (!zone)
		return false;
	bool ok = TZ_zoneToCountry(zone, TZ_ZONE_TAB_PATH, out, out_sz);
	free(zone);
	return ok;
}
