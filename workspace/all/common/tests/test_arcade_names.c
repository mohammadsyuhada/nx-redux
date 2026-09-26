// Host test for nextui/arcade_names.c: the launcher's fallback display names
// for arcade zips with no map.txt alias. Checks the loader against a small
// hand-written table (unsorted, CRLF, malformed lines) and the committed
// res/arcade tables against sets people actually have.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../nextui/arcade_names.h"

#define RES_ARCADE "../../../../skeleton/SYSTEM/res/arcade"

static void expect(const ArcadeNames* names, const char* filename, const char* title) {
	const char* got = ArcadeNames_get(names, filename);
	if (title == NULL ? got != NULL : (got == NULL || strcmp(got, title) != 0)) {
		fprintf(stderr, "%s: expected \"%s\", got \"%s\"\n", filename, title ? title : "(null)", got ? got : "(null)");
		assert(0);
	}
}

int main(void) {
	assert(ArcadeNames_load("/nonexistent/arcade.txt") == NULL);

	const char* fixture = "/tmp/nx_test_arcade_names.txt";
	FILE* f = fopen(fixture, "wb");
	assert(f);
	// out of order, a CRLF line, a BIOS marker, and lines the loader must skip
	fputs("zzz\tLast Game\n"
		  "mslug\tMetal Slug - Super Vehicle-001\r\n"
		  "neogeo\t.\n"
		  "notab\n"
		  "\tno stem\n"
		  "nostitle\t\n"
		  "\n"
		  "aaa\tFirst Game",
		  f);
	fclose(f);

	ArcadeNames* names = ArcadeNames_load(fixture);
	assert(names);
	expect(names, "mslug.zip", "Metal Slug - Super Vehicle-001");
	expect(names, "MSLUG.ZIP", "Metal Slug - Super Vehicle-001"); // FAT case
	expect(names, "mslug.7z", "Metal Slug - Super Vehicle-001");
	expect(names, "neogeo.zip", ".");		// BIOS: hide() drops it
	expect(names, "aaa.zip", "First Game"); // last line, no newline
	expect(names, "zzz.zip", "Last Game");
	// only .zip/.7z are arcade sets; disc images and bare stems keep their names
	expect(names, "mslug.chd", NULL);
	expect(names, "mslug", NULL);
	expect(names, ".zip", NULL);
	expect(names, "unknown.zip", NULL);
	expect(names, "notab.zip", NULL);
	expect(names, "nostitle.zip", NULL);
	expect(NULL, "mslug.zip", NULL);
	expect(names, NULL, NULL);
	ArcadeNames_free(names);
	ArcadeNames_free(NULL);
	remove(fixture);

	// committed tables (scripts/gen-arcade-names.sh)
	ArcadeNames* fbn = ArcadeNames_load(RES_ARCADE "/FBN.txt");
	assert(fbn);
	expect(fbn, "mslug.zip", "Metal Slug - Super Vehicle-001");
	expect(fbn, "sf2ce.zip", "Street Fighter II': Champion Edition"); // "(World 920513)" dropped
	expect(fbn, "1942.zip", "1942");
	expect(fbn, "dspirit.zip", "Dragon Spirit"); // nested "(new version (DS3))"
	expect(fbn, "88games.zip", "'88 Games");
	expect(fbn, "neogeo.zip", ".");
	expect(fbn, "pgm.zip", ".");
	ArcadeNames_free(fbn);

	ArcadeNames* dc = ArcadeNames_load(RES_ARCADE "/DC.txt");
	assert(dc);
	expect(dc, "ikaruga.zip", "Ikaruga");
	expect(dc, "mvsc2.zip", "Marvel vs. Capcom 2 New Age of Heroes");
	expect(dc, "brickppl.zip", "Brick People"); // first of " / " names; entry has a // comment
	expect(dc, "naomi.zip", ".");
	expect(dc, "awbios.zip", ".");
	expect(dc, "Shenmue.chd", NULL);
	ArcadeNames_free(dc);

	printf("test_arcade_names: OK\n");
	return 0;
}
