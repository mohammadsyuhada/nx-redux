#include "../../nextui/menulogo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
	printf("test_menulogo: ok\n");
	return 0;
}
