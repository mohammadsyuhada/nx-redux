#include "../../nextui/menulogo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char tmp[256];

static void mkdirs(const char* rel) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", tmp, rel);
	assert(mkdir(path, 0755) == 0);
}

static void touch(const char* rel) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", tmp, rel);
	FILE* f = fopen(path, "w");
	assert(f);
	fclose(f);
}

// MenuLogo_fileFor(<tmp>/<folder>) is want, with "@" standing for <tmp>
static int file_is(const char* folder, const char* want) {
	char path[512], out[512], expect[512];
	snprintf(path, sizeof(path), "%s/%s", tmp, folder);
	const char* got = MenuLogo_fileFor(path, out, sizeof(out));
	if (!want)
		return got == NULL;
	if (want[0] == '@')
		snprintf(expect, sizeof(expect), "%s%s", tmp, want + 1);
	else
		snprintf(expect, sizeof(expect), "%s", want);
	return got == out && strcmp(got, expect) == 0;
}

// The user's <folder>/.media/logo.png first (#220), else the bundled logo, else none
static void file_for(void) {
	snprintf(tmp, sizeof(tmp), "/tmp/nx_test_menulogo.XXXXXX");
	assert(mkdtemp(tmp));
	mkdirs("Game Boy (GB)");
	mkdirs("DadMax (BKNDAD)");
	mkdirs("DadMax (BKNDAD)/.media");
	mkdirs("Game Boy Advance (GBA)");
	mkdirs("Game Boy Advance (GBA)/.media");

	assert(file_is("Game Boy (GB)", "menu_logo_gb.png"));
	assert(file_is("DadMax (BKNDAD)", NULL)); // an unknown tag, no logo of its own
	touch("DadMax (BKNDAD)/.media/logo.png");
	assert(file_is("DadMax (BKNDAD)", NULL)); // looked up once: the copy shows after a new generation
	MenuLogo_forget();
	assert(file_is("DadMax (BKNDAD)", "@/DadMax (BKNDAD)/.media/logo.png"));
	touch("Game Boy Advance (GBA)/.media/logo.png"); // replaces a stock logo
	assert(file_is("Game Boy Advance (GBA)", "@/Game Boy Advance (GBA)/.media/logo.png"));
	assert(file_is("Game Boy (GB)", "menu_logo_gb.png"));

	char out[20]; // too small for the user's path: the bundled logo, and too small for that: none
	char path[512];
	snprintf(path, sizeof(path), "%s/Game Boy Advance (GBA)", tmp);
	assert(MenuLogo_fileFor(path, out, sizeof(out)) == out && strcmp(out, "menu_logo_gba.png") == 0);
	assert(MenuLogo_fileFor(path, out, 12) == NULL);
	assert(MenuLogo_fileFor(NULL, out, sizeof(out)) == NULL);
	assert(MenuLogo_fileFor("", out, sizeof(out)) == NULL);

	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
	assert(system(cmd) == 0);
}

static int is(const char* folder, const char* id) {
	const char* got = MenuLogo_idForFolder(folder);
	return id ? (got && strcmp(got, id) == 0) : got == NULL;
}

int main(void) {
	assert(is("Game Boy Advance (GBA)", "gba"));
	assert(is("Game Boy Advance (MGBA)", "gba"));
	assert(is("Super Nintendo ES (SUPA)", "sfc"));
	assert(is("Sega 32X (32X)", "s32x"));
	assert(is("Nintendo 3DS (3DS)", "3ds"));
	assert(is("Xtra Games (EXTRAS)", "extras"));
	assert(is("Amiga (PUAE)", "amiga"));
	assert(is("Dreamcast Lite (DCX)", "dcx")); // its own logo: Dreamcast with a LITE badge
	assert(is("Amstrad CPC (CPC)", "cpc"));
	assert(is("Commodore 64 (C64)", "c64"));
	assert(is("Commodore 128 (C128)", "c128"));
	assert(is("Commodore PET (PET)", "pet"));
	assert(is("Commodore Plus4 (PLUS4)", "plus4"));
	assert(is("Commodore VIC20 (VIC)", "vic20"));
	assert(is("Microsoft MSX (MSX)", "msx"));
	assert(is("Ms-Dos (DOS)", "dos"));
	assert(is("Sony PlayStation (PS)", "ps"));
	assert(is("Sony PlayStation (PSX)", "ps")); // SwanStation
	assert(is("Sega Genesis (GPGX)", "md"));
	assert(is("Sega Master System (GPGX)", "sms"));
	assert(is("Sega Game Gear (GPGX)", "gg"));
	assert(is("Sega CD (GPGX)", "segacd"));
	assert(is("Sega SG-1000 (GPGX)", "sg1000"));
	assert(is("Ports (PORTS)", "ports"));
	assert(is("Unknown System (ZZZ)", NULL));
	assert(is("No tag here", NULL));
	assert(is("", NULL));
	assert(is(NULL, NULL));
	assert(is("001) Game Boy (GB)", "gb")); // sort prefix doesn't matter, the tag does
	file_for();
	printf("test_menulogo: ok\n");
	return 0;
}
