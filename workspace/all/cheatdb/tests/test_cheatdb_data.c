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
	CHECK(CHEATDB_MAP_COUNT == 31, "31 tag mappings");

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
