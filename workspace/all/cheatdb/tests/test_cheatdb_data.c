#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cheatdb_data.h"

static int fails = 0;
#define CHECK(c, m)                  \
	do {                             \
		if (c)                       \
			printf("PASS: %s\n", m); \
		else {                       \
			printf("FAIL: %s\n", m); \
			fails++;                 \
		}                            \
	} while (0)

int main(void) {
	CheatdbPaths p;
	Cheatdb_initPaths(&p);

	CHECK(!Cheatdb_installed(&p), "not installed when no manifest");

	// Extract two mapped systems from the fixture zip (env CHEATDB_TEST_ZIP)
	// into their Cheats/<TAG> dirs, building the manifest from the archive index.
	Cheatdb_truncateManifest(&p);
	char dest[600];
	snprintf(dest, sizeof(dest), "%s/GBA", p.cheats_dir);
	Cheatdb_extractFolder(&p, "Nintendo - Game Boy Advance", dest);
	Cheatdb_appendManifest(&p, "Nintendo - Game Boy Advance", dest);
	snprintf(dest, sizeof(dest), "%s/MD", p.cheats_dir);
	Cheatdb_extractFolder(&p, "Sega - Mega Drive - Genesis", dest);
	Cheatdb_appendManifest(&p, "Sega - Mega Drive - Genesis", dest);

	CHECK(Cheatdb_installed(&p), "installed after extract");
	CHECK(Cheatdb_totalCount(&p) == 2, "total count = 2 pack files");

	// Hand-made file present, NOT in the manifest.
	char handmade[700];
	snprintf(handmade, sizeof(handmade), "%s/GBA/MyHomebrew.cht", p.cheats_dir);
	FILE* f = fopen(handmade, "w");
	if (f) {
		fputs("user cheat\n", f);
		fclose(f);
	}

	Cheatdb_writeDbVersion(&p, "Mon, 14 Sep 2026 18:05:15 GMT");
	char v[128] = {0};
	CHECK(Cheatdb_readDbVersion(&p, v, sizeof(v)) && strcmp(v, "Mon, 14 Sep 2026 18:05:15 GMT") == 0, "db version round-trips");

	Cheatdb_removeAll(&p);
	CHECK(!Cheatdb_installed(&p), "not installed after removeAll");
	f = fopen(handmade, "r");
	CHECK(f != NULL, "hand-made cheat preserved by removeAll");
	if (f)
		fclose(f);

	// mapping sanity
	CHECK(CHEATDB_MAP_COUNT == 35, "35 tag mappings");
	const char* psp = NULL;
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++)
		if (strcmp(CHEATDB_MAP[i].tag, "PSP") == 0)
			psp = CHEATDB_MAP[i].folder;
	CHECK(psp && strcmp(psp, "Sony - PlayStation Portable") == 0, "PSP -> Sony - PlayStation Portable");
	const char* wsc = NULL;
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++)
		if (strcmp(CHEATDB_MAP[i].tag, "WSC") == 0)
			wsc = CHEATDB_MAP[i].folder;
	CHECK(wsc && strcmp(wsc, "Bandai - WonderSwan Color") == 0, "WSC -> Bandai - WonderSwan Color");

	// Last-Modified probe: TLS verified against the CA bundle when one is
	// given (the vendored wget needs --check-certificate=on, --ca-certificate
	// alone doesn't verify); no bundle, or one whose path holds a quote, keeps
	// the no-verification fallback. The wget shim logs each call's argv
	// (one line per call) to $WGET_ARGS_LOG.
	const char* log = getenv("WGET_ARGS_LOG");
	char lm[128], line[1024] = {0};
	if (log) {
		remove(log);
		int got = Cheatdb_remoteLastModified("/sd/.system/shared/ssl/ca-certificates.crt", lm, sizeof(lm));
		CHECK(got > 0 && strcmp(lm, "Mon, 14 Sep 2026 18:05:15 GMT") == 0, "Last-Modified parsed");
		FILE* lf = fopen(log, "r");
		if (lf && fgets(line, sizeof(line), lf)) {}
		if (lf)
			fclose(lf);
		CHECK(strstr(line, "--check-certificate=on --ca-certificate=/sd/.system/shared/ssl/ca-certificates.crt") &&
				  !strstr(line, "--no-check-certificate"),
			  "Last-Modified probe verifies TLS against the bundle");

		remove(log);
		Cheatdb_remoteLastModified(NULL, lm, sizeof(lm));
		line[0] = '\0';
		lf = fopen(log, "r");
		if (lf && fgets(line, sizeof(line), lf)) {}
		if (lf)
			fclose(lf);
		CHECK(strstr(line, "--no-check-certificate") && !strstr(line, "--check-certificate=on"),
			  "no bundle -> no-verification fallback");

		remove(log);
		Cheatdb_remoteLastModified("/sd/it's.crt", lm, sizeof(lm));
		line[0] = '\0';
		lf = fopen(log, "r");
		if (lf && fgets(line, sizeof(line), lf)) {}
		if (lf)
			fclose(lf);
		CHECK(strstr(line, "--no-check-certificate") != NULL, "bundle path with a quote is not spliced into the shell");
	} else {
		CHECK(0, "WGET_ARGS_LOG not set (run via scripts/tests/test-cheatdb-data.sh)");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
