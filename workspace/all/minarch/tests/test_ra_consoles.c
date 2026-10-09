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
	// N64 runs on mupen64plus-next in minarch (system N64.pak)
	CHECK(RA_getConsoleId("N64") == RC_CONSOLE_NINTENDO_64, "N64 -> RC_CONSOLE_NINTENDO_64");
	CHECK(RA_getConsoleId("DC") == RC_CONSOLE_DREAMCAST, "DC -> Dreamcast (control)");
	CHECK(RA_getConsoleId("DCX") == RC_CONSOLE_DREAMCAST, "DCX -> Dreamcast");
	CHECK(RA_getConsoleId("PS") == RC_CONSOLE_PLAYSTATION, "PS -> PlayStation (control)");
	CHECK(RA_getConsoleId("PSX") == RC_CONSOLE_PLAYSTATION, "PSX -> PlayStation (SwanStation)");
	CHECK(RA_getConsoleId("WS") == RC_CONSOLE_WONDERSWAN, "WS -> WonderSwan");
	CHECK(RA_getConsoleId("WSC") == RC_CONSOLE_WONDERSWAN, "WSC -> WonderSwan (Color shares the console)");
	CHECK(RA_getConsoleId("NOPE") == RC_CONSOLE_UNKNOWN, "unknown tag -> unknown");
	CHECK(RA_getConsoleId("") == RC_CONSOLE_UNKNOWN, "empty tag -> unknown");

	// --- RA_detectConsole: every bundled pak resolves as before ---
	struct {
		const char *tag, *core, *rom;
		int want;
	} cases[] = {
		{"32X", "picodrive", "a.32x", RC_CONSOLE_SEGA_32X},
		{"32X", "picodrive", "a.bin", RC_CONSOLE_SEGA_32X}, // tag decides a .bin
		{"A2600", "stella2014", "a.a26", RC_CONSOLE_ATARI_2600},
		{"A2600", "stella2014", "a.bin", RC_CONSOLE_ATARI_2600},
		{"A5200", "a5200", "a.a52", RC_CONSOLE_ATARI_5200},
		{"A7800", "prosystem", "a.a78", RC_CONSOLE_ATARI_7800},
		{"C128", "vice_x128", "a.d64", RC_CONSOLE_COMMODORE_64},
		{"C64", "vice_x64", "a.d64", RC_CONSOLE_COMMODORE_64},
		{"COLECO", "gearcoleco", "a.col", RC_CONSOLE_COLECOVISION},
		{"CPC", "cap32", "a.dsk", RC_CONSOLE_AMSTRAD_PC},
		{"DC", "flycast", "a.chd", RC_CONSOLE_DREAMCAST},
		{"DC", "flycast", "a.gdi", RC_CONSOLE_DREAMCAST},
		{"DCX", "flycast_legacy", "a.chd", RC_CONSOLE_DREAMCAST},
		{"DCX", "flycast_legacy", "a.gdi", RC_CONSOLE_DREAMCAST},
		{"DCX", "flycast_legacy", "mslug6.zip", RC_CONSOLE_ARCADE},
		{"FBN", "fbneo", "sf2.zip", RC_CONSOLE_ARCADE},
		{"FC", "fceumm", "a.nes", RC_CONSOLE_NINTENDO},
		{"FDS", "fceumm", "a.fds", RC_CONSOLE_FAMICOM_DISK_SYSTEM},
		{"GB", "gambatte", "a.gb", RC_CONSOLE_GAMEBOY},
		{"GBA", "gpsp", "a.gba", RC_CONSOLE_GAMEBOY_ADVANCE},
		{"GBC", "gambatte", "a.gbc", RC_CONSOLE_GAMEBOY_COLOR},
		{"GG", "picodrive", "a.gg", RC_CONSOLE_GAME_GEAR},
		{"GPGX", "genesis_plus_gx", "a.md", RC_CONSOLE_MEGA_DRIVE},
		{"GPGX", "genesis_plus_gx", "a.bin", RC_CONSOLE_MEGA_DRIVE},
		{"GPGX", "genesis_plus_gx", "a.sms", RC_CONSOLE_MASTER_SYSTEM},
		{"GPGX", "genesis_plus_gx", "a.gg", RC_CONSOLE_GAME_GEAR},
		{"GPGX", "genesis_plus_gx", "a.sg", RC_CONSOLE_SG1000},
		{"GPGX", "genesis_plus_gx", "a.chd", RC_CONSOLE_SEGA_CD},
		{"GPGX", "genesis_plus_gx", "a.cue", RC_CONSOLE_SEGA_CD},
		{"LYNX", "handy", "a.lnx", RC_CONSOLE_ATARI_LYNX},
		{"MD", "picodrive", "a.md", RC_CONSOLE_MEGA_DRIVE},
		{"MD", "picodrive", "a.bin", RC_CONSOLE_MEGA_DRIVE},
		{"MGBA", "mgba", "a.gba", RC_CONSOLE_GAMEBOY_ADVANCE},
		{"MSX", "bluemsx", "a.rom", RC_CONSOLE_MSX},
		{"NGP", "race", "a.ngp", RC_CONSOLE_NEOGEO_POCKET},
		{"NGPC", "race", "a.ngc", RC_CONSOLE_NEOGEO_POCKET},
		{"P8", "fake08", "a.p8.png", RC_CONSOLE_PICO},
		{"PCE", "mednafen_pce_fast", "a.pce", RC_CONSOLE_PC_ENGINE},
		{"PCE", "mednafen_pce_fast", "a.chd", RC_CONSOLE_PC_ENGINE_CD},
		{"PCE", "mednafen_pce_fast", "a.m3u", RC_CONSOLE_PC_ENGINE_CD},
		{"PKM", "pokemini", "a.min", RC_CONSOLE_POKEMON_MINI},
		{"PS", "pcsx_rearmed", "a.chd", RC_CONSOLE_PLAYSTATION},
		{"PS", "pcsx_rearmed", "a.bin", RC_CONSOLE_PLAYSTATION},
		{"PS", "pcsx_rearmed", "a.pbp", RC_CONSOLE_PLAYSTATION}, // EBOOT: core decides
		{"PSX", "swanstation", "a.chd", RC_CONSOLE_PLAYSTATION},
		{"PSX", "swanstation", "a.m3u", RC_CONSOLE_PLAYSTATION},
		{"PSP", "ppsspp", "a.iso", RC_CONSOLE_PSP},
		{"PSP", "ppsspp", "a.cso", RC_CONSOLE_PSP},
		{"PUAE", "puae2021", "a.adf", RC_CONSOLE_AMIGA},
		{"SEGACD", "picodrive", "a.chd", RC_CONSOLE_SEGA_CD},
		{"SEGACD", "picodrive", "a.cue", RC_CONSOLE_SEGA_CD},
		{"SFC", "snes9x", "a.sfc", RC_CONSOLE_SUPER_NINTENDO},
		{"SG1000", "picodrive", "a.sg", RC_CONSOLE_SG1000},
		{"SGB", "mgba", "a.gb", RC_CONSOLE_GAMEBOY},
		{"SMS", "picodrive", "a.sms", RC_CONSOLE_MASTER_SYSTEM},
		{"SUPA", "mednafen_supafaust", "a.smc", RC_CONSOLE_SUPER_NINTENDO},
		{"VB", "mednafen_vb", "a.vb", RC_CONSOLE_VIRTUAL_BOY},
		{"VIC", "vice_xvic", "a.prg", RC_CONSOLE_VIC20},
		{"WSC", "mednafen_wswan", "a.wsc", RC_CONSOLE_WONDERSWAN},
		// RA-less tags stay off whatever the core
		{"PET", "vice_xpet", "a.prg", RC_CONSOLE_UNKNOWN},
		{"PLUS4", "vice_xplus4", "a.prg", RC_CONSOLE_UNKNOWN},
		{"PRBOOM", "prboom", "doom.wad", RC_CONSOLE_UNKNOWN},
		// --- new: what the tag alone got wrong ---
		{"DC", "flycast", "naomi.zip", RC_CONSOLE_ARCADE},		  // Naomi/Atomiswave sets
		{"MGBA", "mgba", "a.gbc", RC_CONSOLE_GAMEBOY_COLOR},	  // mGBA runs GB/GBC too
		{"GBA", "mgba", "a.gb", RC_CONSOLE_GAMEBOY},			  // core swapped in a stock pak
		{"GB", "gambatte", "a.gbc", RC_CONSOLE_GAMEBOY_COLOR},	  // GBC game in the GB folder
		{"WSX", "mednafen_wswan", "a.ws", RC_CONSOLE_WONDERSWAN}, // custom pak, known core
		{"N64X", "mupen64plus_next", "a.z64", RC_CONSOLE_NINTENDO_64},
		{"MYGB", "unknown_core", "a.gb", RC_CONSOLE_GAMEBOY},	 // custom everything: extension
		{"MYSEGA", "unknown_core", "a.bin", RC_CONSOLE_UNKNOWN}, // ambiguous: stays off
		{"MYCD", NULL, "a.chd", RC_CONSOLE_UNKNOWN},			 // disc, no hint
		{NULL, NULL, "a.sfc", RC_CONSOLE_SUPER_NINTENDO},		 // no tag at all
		{"", "", "", RC_CONSOLE_UNKNOWN},
		// --- tag-only fallback (stock tag, unknown core) keeps the old rules ---
		{"PCE", "some_pce_core", "a.cue", RC_CONSOLE_PC_ENGINE_CD},
		{"GPGX", "", "a.gg", RC_CONSOLE_GAME_GEAR},
		{"DC", NULL, "a.7z", RC_CONSOLE_ARCADE},
		{"DCX", NULL, "a.chd", RC_CONSOLE_DREAMCAST},
		{"DCX", NULL, "a.7z", RC_CONSOLE_ARCADE},
		{"PS", NULL, "a.chd", RC_CONSOLE_PLAYSTATION},
		{"PSX", NULL, "a.cue", RC_CONSOLE_PLAYSTATION},
		// case-insensitive extension and core prefix match
		{"GB", "GAMBATTE", "A.GBC", RC_CONSOLE_GAMEBOY_COLOR},
		{"FOO", "genesis_plus_gx_wide", "a.SMS", RC_CONSOLE_MASTER_SYSTEM},
		// a dot in a directory name is not an extension
		{"MYGB", NULL, "/Roms/v1.0/game", RC_CONSOLE_UNKNOWN},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int got = RA_detectConsole(cases[i].tag, cases[i].core, cases[i].rom);
		char msg[160];
		snprintf(msg, sizeof(msg), "detect(%s, %s, %s) = %d (want %d)", cases[i].tag ? cases[i].tag : "NULL",
				 cases[i].core ? cases[i].core : "NULL", cases[i].rom, got, cases[i].want);
		CHECK(got == cases[i].want, msg);
	}

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
	return fails ? 1 : 0;
}
