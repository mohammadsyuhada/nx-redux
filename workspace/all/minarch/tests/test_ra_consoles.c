// Host test for ra_consoles.h: EMU tag -> RetroAchievements console id.
// mkdir -p /tmp/rcinc && ln -sfn "$PWD/workspace/all/minarch/rcheevos/src/include" /tmp/rcinc/rcheevos
// cc -I workspace/all/minarch -I /tmp/rcinc workspace/all/minarch/tests/test_ra_consoles.c \
//    -o /tmp/test_ra_consoles && /tmp/test_ra_consoles
#include <stdio.h>
#include <string.h>
#include <rcheevos/rc_consoles.h>
#include "ra_consoles.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

int main(void) {
	// PSP runs on the PPSSPP libretro core in minarch (system PSP.pak)
	CHECK(RA_getConsoleId("PSP") == RC_CONSOLE_PSP, "PSP -> RC_CONSOLE_PSP");
	CHECK(RA_getConsoleId("DC") == RC_CONSOLE_DREAMCAST, "DC -> Dreamcast (control)");
	CHECK(RA_getConsoleId("PS") == RC_CONSOLE_PLAYSTATION, "PS -> PlayStation (control)");
	CHECK(RA_getConsoleId("NOPE") == RC_CONSOLE_UNKNOWN, "unknown tag -> unknown");
	CHECK(RA_getConsoleId("") == RC_CONSOLE_UNKNOWN, "empty tag -> unknown");

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
	return fails ? 1 : 0;
}
