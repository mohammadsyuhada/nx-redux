// Host test for ma_save_paths.c: state slot paths and the netplay save mode.
//   cc -std=gnu99 -Wall -Werror -I workspace/all/minarch -I workspace/all/common \
//      -I workspace/tg5040/platform -DPLATFORM=\"tg5040\" \
//      workspace/all/minarch/ma_save_paths.c workspace/all/minarch/tests/test_save_paths.c \
//      -o /tmp/test_save_paths && /tmp/test_save_paths
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "defines.h"
#include "ma_save_paths.h"

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

static int path_is(int format, int slot, const char* want) {
	char out[MAX_PATH];
	if (SavePaths_state(out, sizeof(out), "/s", "Pokemon Ruby (USA).gba", format, slot) != 0)
		return 0;
	if (strcmp(out, want) != 0) {
		printf("  got %s\n", out);
		return 0;
	}
	return 1;
}

int main(void) {
	// the auto-resume slot in every format (what a netplay session drops)
	CHECK(path_is(STATE_FORMAT_SRM, AUTO_RESUME_SLOT, "/s/Pokemon Ruby (USA).state.auto"), "SRM auto slot");
	CHECK(path_is(STATE_FORMAT_SRM_UNCOMPRESSED, AUTO_RESUME_SLOT, "/s/Pokemon Ruby (USA).state.auto"), "SRM uncompressed auto slot");
	CHECK(path_is(STATE_FORMAT_SRM_EXTRADOT, AUTO_RESUME_SLOT, "/s/Pokemon Ruby (USA).state.auto"), "extradot auto slot");
	CHECK(path_is(STATE_FORMAT_SAV, AUTO_RESUME_SLOT, "/s/Pokemon Ruby (USA).gba.st9"), "MinUI auto slot");
	// manual slots keep their names
	CHECK(path_is(STATE_FORMAT_SRM, 0, "/s/Pokemon Ruby (USA).state"), "SRM slot 0");
	CHECK(path_is(STATE_FORMAT_SRM, 3, "/s/Pokemon Ruby (USA).state3"), "SRM slot 3");
	CHECK(path_is(STATE_FORMAT_SRM_UNCOMPRESSED_EXTRADOT, 8, "/s/Pokemon Ruby (USA).state.8"), "extradot slot 8");
	CHECK(path_is(STATE_FORMAT_SAV, 0, "/s/Pokemon Ruby (USA).gba.st0"), "MinUI slot 0");

	char small[8];
	CHECK(SavePaths_state(small, sizeof(small), "/s", "game.gba", STATE_FORMAT_SRM, 9) == -1, "overflow reported");

	char name[] = "Game.v1.2.zip";
	SavePaths_stripExtension(name);
	CHECK(strcmp(name, "Game.v1.2") == 0, "strip 3-letter extension");
	char long_ext[] = "Game.backup";
	SavePaths_stripExtension(long_ext);
	CHECK(strcmp(long_ext, "Game.backup") == 0, "keep a 6-letter suffix");

	CHECK(SavePaths_netplayMode(NULL, NULL) == NETPLAY_SAVES_NONE, "plain launch");
	CHECK(SavePaths_netplayMode("", "") == NETPLAY_SAVES_NONE, "empty env is a plain launch");
	CHECK(SavePaths_netplayMode("host", NULL) == NETPLAY_SAVES_REAL, "host plays its real save");
	CHECK(SavePaths_netplayMode("client", "") == NETPLAY_SAVES_REAL, "link client plays its real save");
	CHECK(SavePaths_netplayMode("client", "/tmp/netplay-saves") == NETPLAY_SAVES_COPY, "lockstep client on a copy");
	CHECK(SavePaths_netplayMode(NULL, "/tmp/netplay-saves") == NETPLAY_SAVES_COPY, "flycast client (no role) on a copy");

	CHECK(SavePaths_autoResumeIsGame("/Roms/GBA/a.gba", "/mnt/SDCARD/Roms/GBA/a.gba", "/mnt/SDCARD"), "marker names this game");
	CHECK(!SavePaths_autoResumeIsGame("/Roms/GBA/b.gba", "/mnt/SDCARD/Roms/GBA/a.gba", "/mnt/SDCARD"), "marker names another game");
	CHECK(!SavePaths_autoResumeIsGame("", "/mnt/SDCARD/Roms/GBA/a.gba", "/mnt/SDCARD"), "empty marker");
	CHECK(!SavePaths_autoResumeIsGame("/tmp/a.gba", "/tmp/a.gba", "/mnt/SDCARD"), "game outside the card");

	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
