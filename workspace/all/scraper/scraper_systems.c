#include "scraper_systems.h"
#include <string.h>
#include <strings.h>

typedef struct {
	const char* tag;
	int id;
	const char* name;
} SystemMapping;

static const SystemMapping systems[] = {
	// Nintendo
	{"NES", 3, "Nintendo Entertainment System"},
	{"FC", 3, "Famicom"},
	{"FDS", 106, "Famicom Disk System"},
	{"SNES", 4, "Super Nintendo"},
	{"SUPA", 4, "Super Famicom"},
	{"N64", 14, "Nintendo 64"},
	{"GB", 9, "Game Boy"},
	{"SGB", 9, "Super Game Boy"},
	{"GBC", 10, "Game Boy Color"},
	{"GBA", 12, "Game Boy Advance"},
	{"MGBA", 12, "Game Boy Advance"},
	{"NDS", 15, "Nintendo DS"},
	{"VB", 11, "Virtual Boy"},
	{"POKE", 4, "Pokemon Mini"},
	{"POKEMINI", 211, "Pokemon Mini"},

	// Sega
	{"SMS", 2, "Sega Master System"},
	{"GG", 21, "Sega Game Gear"},
	{"MD", 1, "Sega Mega Drive"},
	{"GENESIS", 1, "Sega Genesis"},
	{"32X", 19, "Sega 32X"},
	{"SEGACD", 20, "Sega CD"},
	{"SCD", 20, "Sega CD"},
	{"SATURN", 22, "Sega Saturn"},
	{"SG1000", 109, "Sega SG-1000"},

	// Sony
	{"PS", 57, "PlayStation"},
	{"PSX", 57, "PlayStation"},
	{"PSP", 61, "PlayStation Portable"},

	// NEC
	{"PCE", 31, "PC Engine"},
	{"PCECD", 114, "PC Engine CD"},
	{"TGFX", 31, "TurboGrafx-16"},
	{"TGFX16", 31, "TurboGrafx-16"},
	{"SGFX", 105, "SuperGrafx"},

	// Atari
	{"A2600", 26, "Atari 2600"},
	{"A5200", 40, "Atari 5200"},
	{"A7800", 41, "Atari 7800"},
	{"LYNX", 28, "Atari Lynx"},
	{"JAGUAR", 27, "Atari Jaguar"},
	{"ST", 42, "Atari ST"},

	// SNK
	{"NGP", 25, "Neo Geo Pocket"},
	{"NGPC", 82, "Neo Geo Pocket Color"},
	{"NEOGEO", 142, "Neo Geo"},

	// Arcade
	{"FBN", 75, "FinalBurn Neo"},
	{"FBNEO", 75, "FinalBurn Neo"},
	{"ARCADE", 75, "Arcade"},
	{"MAME", 75, "MAME"},
	{"CPS1", 6, "Capcom Play System"},
	{"CPS2", 7, "Capcom Play System II"},
	{"CPS3", 8, "Capcom Play System III"},

	// Home computers
	{"MSX", 113, "MSX"},
	{"MSX2", 116, "MSX2"},
	{"CPC", 65, "Amstrad CPC"},
	{"C64", 66, "Commodore 64"},
	{"C128", 66, "Commodore 128"},
	{"VIC", 73, "Commodore VIC-20"},
	{"PET", 80, "Commodore PET"},
	{"PLUS4", 99, "Commodore Plus/4"},
	{"AMIGA", 64, "Amiga"},
	{"ZX", 76, "ZX Spectrum"},
	{"ZXSPECTRUM", 76, "ZX Spectrum"},

	// Coleco / Mattel
	{"COLECO", 48, "ColecoVision"},
	{"INTELLIVISION", 115, "Intellivision"},
	{"INTV", 115, "Intellivision"},

	// Wonderswan
	{"WS", 45, "WonderSwan"},
	{"WSC", 46, "WonderSwan Color"},

	// Misc
	{"ODYSSEY", 104, "Videopac / Odyssey2"},
	{"O2EM", 104, "Videopac / Odyssey2"},
	{"SUPERVISION", 207, "Watara Supervision"},
	{"VECTREX", 102, "Vectrex"},
	{"CHANNELF", 80, "Fairchild Channel F"},
	{"ARDUBOY", 263, "Arduboy"},
	{"PICO8", 234, "PICO-8"},
	{"TIC80", 222, "TIC-80"},
	{"SCUMMVM", 123, "ScummVM"},
	{"DOS", 135, "MS-DOS"},

	{NULL, -1, NULL}};

int ScraperSystems_getId(const char* tag) {
	if (!tag)
		return -1;
	for (int i = 0; systems[i].tag != NULL; i++) {
		if (strcasecmp(systems[i].tag, tag) == 0)
			return systems[i].id;
	}
	return -1;
}

const char* ScraperSystems_getName(const char* tag) {
	if (!tag)
		return "Unknown";
	for (int i = 0; systems[i].tag != NULL; i++) {
		if (strcasecmp(systems[i].tag, tag) == 0)
			return systems[i].name;
	}
	return tag;
}
