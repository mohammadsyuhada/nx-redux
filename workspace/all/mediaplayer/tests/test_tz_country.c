// Host-compiled unit test for tz_country.c.
// Build & run:
//   cc -I. -I../common ../common/tz_country.c tests/test_tz_country.c -o /tmp/test_tz && /tmp/test_tz
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../common/tz_country.h"

// Stub the platform call so the device convenience wrapper links on host.
char* PLAT_getCurrentTimezone(void) {
	char* s = malloc(64);
	strcpy(s, "Asia/Kuala_Lumpur");
	return s;
}

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(content, f);
	fclose(f);
}

int main(void) {
	const char* tab = "/tmp/test_zone.tab";
	write_file(tab,
			   "# comment line\n"
			   "AD\t+4230+00131\tEurope/Andorra\n"
			   "MY\t+0310+10142\tAsia/Kuala_Lumpur\n"
			   "US\t+404251-0740023\tAmerica/New_York\tEastern\n");

	char cc[8];
	assert(TZ_zoneToCountry("Asia/Kuala_Lumpur", tab, cc, sizeof(cc)) == true);
	assert(strcmp(cc, "MY") == 0);

	assert(TZ_zoneToCountry("America/New_York", tab, cc, sizeof(cc)) == true);
	assert(strcmp(cc, "US") == 0);

	// Unknown zone -> false.
	assert(TZ_zoneToCountry("Mars/Olympus", tab, cc, sizeof(cc)) == false);
	// Missing file -> false.
	assert(TZ_zoneToCountry("Asia/Kuala_Lumpur", "/tmp/no-such.tab", cc, sizeof(cc)) == false);
	// NULL/empty zone -> false.
	assert(TZ_zoneToCountry("", tab, cc, sizeof(cc)) == false);

	// Device wrapper uses the stub zone -> resolves via the DEFAULT system path,
	// which won't exist on host, so it returns false. That's the correct
	// "unresolved -> no pinning" behavior; just confirm it doesn't crash.
	(void)TZ_currentCountryCode(cc, sizeof(cc));

	printf("test_tz_country: OK\n");
	return 0;
}
