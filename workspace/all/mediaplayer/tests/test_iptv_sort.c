// Host-compiled unit test for IPTV_curated_sortCountriesHomeFirst.
// Build & run:
//   cc -I. iptv_curated_sort.c tests/test_iptv_sort.c -o /tmp/test_iptv_sort && /tmp/test_iptv_sort
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../iptv_curated.h"

int main(void) {
	CuratedTVCountry c[4];
	strcpy(c[0].name, "United States");
	strcpy(c[0].code, "US");
	strcpy(c[1].name, "Andorra");
	strcpy(c[1].code, "AD");
	strcpy(c[2].name, "Malaysia");
	strcpy(c[2].code, "MY");
	strcpy(c[3].name, "Brazil");
	strcpy(c[3].code, "BR");

	IPTV_curated_sortCountriesHomeFirst(c, 4, "my"); // case-insensitive
	assert(strcmp(c[0].code, "MY") == 0);			 // home first
	assert(strcmp(c[1].name, "Andorra") == 0);		 // rest A-Z
	assert(strcmp(c[2].name, "Brazil") == 0);
	assert(strcmp(c[3].name, "United States") == 0);

	// No/unknown home code -> fully alphabetical.
	IPTV_curated_sortCountriesHomeFirst(c, 4, "");
	assert(strcmp(c[0].name, "Andorra") == 0);
	assert(strcmp(c[3].name, "United States") == 0);

	printf("test_iptv_sort: OK\n");
	return 0;
}
