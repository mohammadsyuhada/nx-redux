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
	assert(is("Sony PlayStation (PS)", "ps"));
	assert(is("Sega Genesis (GPGX)", "md"));
	assert(is("Sega Master System (GPGX)", "sms"));
	assert(is("Sega Game Gear (GPGX)", "gg"));
	assert(is("Sega CD (GPGX)", "segacd"));
	assert(is("Sega SG-1000 (GPGX)", "sg1000"));
	assert(is("Amiga (PUAE)", NULL));
	assert(is("Ports (PORTS)", "ports"));
	assert(is("No tag here", NULL));
	assert(is("", NULL));
	assert(is(NULL, NULL));
	assert(is("001) Game Boy (GB)", "gb")); // sort prefix doesn't matter, the tag does
	printf("test_menulogo: ok\n");
	return 0;
}
