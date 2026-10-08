// Host test for common/arcade_names.c: the launcher's fallback display names
// for arcade zips with no map.txt alias. Checks the loader against a small
// hand-written table (unsorted, CRLF, malformed lines) and the committed
// res/arcade tables against sets people actually have.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../arcade_names.h"

#define RES_ARCADE "../../../../skeleton/SYSTEM/res/arcade"

static void expect(const ArcadeNames* names, const char* filename, const char* title) {
	const char* got = ArcadeNames_get(names, filename);
	if (title == NULL ? got != NULL : (got == NULL || strcmp(got, title) != 0)) {
		fprintf(stderr, "%s: expected \"%s\", got \"%s\"\n", filename, title ? title : "(null)", got ? got : "(null)");
		assert(0);
	}
}

static void expectQualifier(const ArcadeNames* names, const char* filename, const char* qualifier) {
	const char* got = ArcadeNames_getQualifier(names, filename);
	if (qualifier == NULL ? got != NULL : (got == NULL || strcmp(got, qualifier) != 0)) {
		fprintf(stderr, "%s: expected qualifier \"%s\", got \"%s\"\n", filename, qualifier ? qualifier : "(null)", got ? got : "(null)");
		assert(0);
	}
}

// filenames == NULL: rows are "s0.zip", "s1.zip", ...; labels == NULL:
// expect ArcadeNames_disambiguate to fail
static void expectSuffixesFor(const char** qualifiers, const char** filenames, int n, const char** labels) {
	char bufs[8][128];
	char* out[8];
	char names[8][16];
	const char* files[8];
	for (int i = 0; i < n; i++) {
		out[i] = bufs[i];
		snprintf(names[i], sizeof(names[i]), "s%d.zip", i);
		files[i] = filenames ? filenames[i] : names[i];
	}
	int ok = ArcadeNames_disambiguate(qualifiers, files, n, out, sizeof(bufs[0]));
	if (!labels) {
		if (ok) {
			fprintf(stderr, "disambiguate: expected failure for \"%s\"...\n", files[0]);
			assert(0);
		}
		return;
	}
	assert(ok);
	for (int i = 0; i < n; i++) {
		if (strcmp(out[i], labels[i]) != 0) {
			fprintf(stderr, "disambiguate[%d]: expected \"%s\", got \"%s\"\n", i, labels[i], out[i]);
			assert(0);
		}
	}
}

static void expectSuffixes(const char** qualifiers, int n, const char** labels) {
	expectSuffixesFor(qualifiers, NULL, n, labels);
}

int main(void) {
	// the table a folder tag reads: Dreamcast Lite (DCX) shares DC's Naomi/Atomiswave names
	assert(strcmp(ArcadeNames_tableTag("DCX"), "DC") == 0);
	assert(strcmp(ArcadeNames_tableTag("DC"), "DC") == 0);
	assert(strcmp(ArcadeNames_tableTag("FBN"), "FBN") == 0);

	// cheap pre-check callers use before loading a table
	assert(ArcadeNames_isArcadeFile("mslug6.zip"));
	assert(ArcadeNames_isArcadeFile("MSLUG6.ZIP"));
	assert(ArcadeNames_isArcadeFile("kof98.7z"));
	assert(ArcadeNames_isArcadeFile("KOF98.7Z"));
	assert(!ArcadeNames_isArcadeFile("Soulcalibur (USA).chd"));
	assert(!ArcadeNames_isArcadeFile("mslug6"));
	assert(!ArcadeNames_isArcadeFile(".zip"));
	assert(!ArcadeNames_isArcadeFile("a.zip.chd"));
	assert(!ArcadeNames_isArcadeFile(NULL));

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
		  "avsp\tAlien vs. Predator\t(Europe 940520)\n"
		  "avspj\tAlien vs. Predator\t(Japan 940520)\r\n"
		  "emptyq\tEmpty Qualifier\t\n"
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
	// optional third column: the DAT's trailing bracket groups
	expect(names, "avsp.zip", "Alien vs. Predator");
	expectQualifier(names, "avsp.zip", "(Europe 940520)");
	expectQualifier(names, "AVSPJ.ZIP", "(Japan 940520)"); // CRLF, FAT case
	expect(names, "emptyq.zip", "Empty Qualifier");
	expectQualifier(names, "emptyq.zip", NULL);
	expectQualifier(names, "mslug.zip", NULL); // two-column line
	expectQualifier(names, "unknown.zip", NULL);
	expectQualifier(names, "avsp.chd", NULL);
	expectQualifier(NULL, "avsp.zip", NULL);

	// rows sharing one title (issue #127): region word when every row has a
	// distinct one, else the whole qualifier when those are distinct, else fail
	expectSuffixes((const char*[]){"(Europe 940520)", "(Japan 940520)"}, 2, (const char*[]){"(Europe)", "(Japan)"});
	expectSuffixes((const char*[]){"(World 930422)", "(Japan 930422)"}, 2, (const char*[]){"(World)", "(Japan)"});
	expectSuffixes((const char*[]){"(US, rev A)", "(Hong Kong 940520)", "(Asia)"}, 3, (const char*[]){"(US)", "(Hong Kong)", "(Asia)"});
	expectSuffixes((const char*[]){"(korea)", "(New Zealand)"}, 2, (const char*[]){"(korea)", "(New Zealand)"});
	// "(World?)" is not a confident region, "(New version)" not New Zealand
	expectSuffixes((const char*[]){"(World?)", "(New version)"}, 2, (const char*[]){"(World?)", "(New version)"});
	// same region twice -> whole qualifiers
	expectSuffixes((const char*[]){"(Japan 940520)", "(Japan, rev A)"}, 2, (const char*[]){"(Japan 940520)", "(Japan, rev A)"});
	// one row without a region -> whole qualifiers
	expectSuffixes((const char*[]){"(Japan)", "(bootleg)"}, 2, (const char*[]){"(Japan)", "(bootleg)"});
	expectSuffixes((const char*[]){"(set 1)", "(set 2)"}, 2, (const char*[]){"(set 1)", "(set 2)"});
	expectSuffixes((const char*[]){"(World 910522) (bootleg)", "(World 910522)"}, 2, (const char*[]){"(World 910522) (bootleg)", "(World 910522)"});
	// one row without a qualifier (usually the parent set) keeps the bare
	// title: "" label; the others are told apart among themselves
	expectSuffixes((const char*[]){NULL, "(Japan)"}, 2, (const char*[]){"", "(Japan)"});
	expectSuffixes((const char*[]){"(Rev A)", ""}, 2, (const char*[]){"(Rev A)", ""});
	expectSuffixes((const char*[]){"(USA)", NULL, "(prototype)"}, 3, (const char*[]){"(USA)", "", "(prototype)"});
	expectSuffixes((const char*[]){"(World)", NULL, "(Japan)"}, 3, (const char*[]){"(World)", "", "(Japan)"});
	// qualifiers cannot tell every row apart: every row gets its file stem
	expectSuffixesFor((const char*[]){NULL, NULL}, (const char*[]){"vtennis2.zip", "vtenis2c.zip"}, 2, (const char*[]){"(vtennis2)", "(vtenis2c)"});
	expectSuffixes((const char*[]){"", NULL, "(Japan)"}, 3, (const char*[]){"(s0)", "(s1)", "(s2)"});
	expectSuffixes((const char*[]){"(Japan)", "(Japan)"}, 2, (const char*[]){"(s0)", "(s1)"});
	expectSuffixes((const char*[]){NULL, "(Japan)", "(Japan)"}, 3, (const char*[]){"(s0)", "(s1)", "(s2)"});
	// ... or the whole filename when two stems collide
	expectSuffixesFor((const char*[]){NULL, NULL}, (const char*[]){"avsp.zip", "avsp.7z"}, 2, (const char*[]){"(avsp.zip)", "(avsp.7z)"});
	// nothing to tell apart, or no filename to fall back on
	expectSuffixes((const char*[]){"(Japan)"}, 1, NULL);
	expectSuffixesFor((const char*[]){NULL, NULL}, (const char*[]){"a.zip", NULL}, 2, NULL);
	// labels never truncate: a qualifier that does not fit falls back to the
	// stem, and a stem that does not fit gives up
	{
		char small[2][8];
		char* out[2] = {small[0], small[1]};
		assert(ArcadeNames_disambiguate((const char*[]){"(set one long)", "(set two long)"}, (const char*[]){"a.zip", "b.zip"}, 2, out, sizeof(small[0])));
		assert(strcmp(small[0], "(a)") == 0 && strcmp(small[1], "(b)") == 0);
		assert(!ArcadeNames_disambiguate((const char*[]){NULL, NULL}, (const char*[]){"longname1.zip", "longname2.zip"}, 2, out, sizeof(small[0])));
	}
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
	// issue #127: region clones keep the plain title plus their qualifier
	expect(fbn, "avspj.zip", "Alien vs. Predator");
	expectQualifier(fbn, "avsp.zip", "(Europe 940520)");
	expectQualifier(fbn, "avspj.zip", "(Japan 940520)");
	expectQualifier(fbn, "punisher.zip", "(World 930422)");
	expectQualifier(fbn, "punisherj.zip", "(Japan 930422)");
	expectQualifier(fbn, "dspirit.zip", "(new version (DS3))");
	expectQualifier(fbn, "88games.zip", NULL);
	expectQualifier(fbn, "neogeo.zip", NULL);
	ArcadeNames_free(fbn);

	ArcadeNames* dc = ArcadeNames_load(RES_ARCADE "/DC.txt");
	assert(dc);
	expect(dc, "ikaruga.zip", "Ikaruga");
	expect(dc, "mvsc2.zip", "Marvel vs. Capcom 2 New Age of Heroes");
	expect(dc, "brickppl.zip", "Brick People"); // first of " / " names; entry has a // comment
	expect(dc, "naomi.zip", ".");
	expect(dc, "awbios.zip", ".");
	expect(dc, "Shenmue.chd", NULL);
	// clones of an unqualified parent set
	expect(dc, "ngbcj.zip", "NeoGeo Battle Coliseum");
	expectQualifier(dc, "ngbc.zip", NULL);
	expectQualifier(dc, "ngbcj.zip", "(Japan)");
	ArcadeNames_free(dc);

	printf("test_arcade_names: OK\n");
	return 0;
}
