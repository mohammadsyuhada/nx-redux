#include "scraper_systems.h"
#include <string.h>
#include <strings.h>

typedef struct {
	const char* tag;
	int id;
	const char* name;
} SystemMapping;

// ScreenScraper ids the per-ROM refinement below hands out.
#define SS_ID_MEGA_DRIVE 1
#define SS_ID_MASTER_SYSTEM 2
#define SS_ID_SEGA_CD 20
#define SS_ID_GAME_GEAR 21
#define SS_ID_SG1000 109
#define SS_ID_DREAMCAST 23
#define SS_ID_ARCADE 75

static const SystemMapping systems[] = {
	// Nintendo
	{"NES", 3, "Nintendo Entertainment System"},
	{"FC", 3, "Famicom"},
	{"FAMICOM", 3, "Famicom"},
	{"FDS", 106, "Famicom Disk System"},
	{"SNES", 4, "Super Nintendo"},
	{"SFC", 4, "Super Famicom"},
	{"SUPA", 4, "Super Famicom"},
	{"N64", 14, "Nintendo 64"},
	{"GB", 9, "Game Boy"},
	{"SGB", 9, "Super Game Boy"},
	{"GBC", 10, "Game Boy Color"},
	{"GBA", 12, "Game Boy Advance"},
	{"MGBA", 12, "Game Boy Advance"},
	{"NDS", 15, "Nintendo DS"},
	{"DS", 15, "Nintendo DS"},
	{"VB", 11, "Virtual Boy"},
	{"PKM", 211, "Pokemon Mini"},
	{"POKE", 211, "Pokemon Mini"},
	{"POKEMINI", 211, "Pokemon Mini"},

	// Sega
	{"SMS", 2, "Sega Master System"},
	{"GG", 21, "Sega Game Gear"},
	{"MD", 1, "Sega Mega Drive"},
	// Genesis Plus GX: one tag for every Sega system; ScraperSystems_getIdForRom
	// refines this Mega Drive base by the ROM's extension (see below).
	{"GPGX", 1, "Sega Mega Drive"},
	{"GENESIS", 1, "Sega Genesis"},
	{"GEN", 1, "Sega Genesis"},
	{"SMD", 1, "Sega Mega Drive"},
	{"32X", 19, "Sega 32X"},
	{"SEGA32X", 19, "Sega 32X"},
	{"SEGACD", 20, "Sega CD"},
	{"SCD", 20, "Sega CD"},
	{"MEGACD", 20, "Mega CD"},
	{"MDCD", 20, "Mega CD"},
	{"SATURN", 22, "Sega Saturn"},
	{"SS", 22, "Sega Saturn"},
	{"SAT", 22, "Sega Saturn"},
	{"DC", 23, "Dreamcast"},
	{"SG1000", 109, "Sega SG-1000"},
	{"SG", 109, "Sega SG-1000"},

	// Sony
	{"PS", 57, "PlayStation"},
	{"PSX", 57, "PlayStation"},
	{"PS1", 57, "PlayStation"},
	{"PSP", 61, "PlayStation Portable"},
	{"PPSSPP", 61, "PlayStation Portable"},

	// NEC
	{"PCE", 31, "PC Engine"},
	{"PCECD", 114, "PC Engine CD"},
	{"PCCD", 114, "PC Engine CD"},
	{"TGCD", 114, "TurboGrafx-CD"},
	{"TG16CD", 114, "TurboGrafx-CD"},
	{"TGFXCD", 114, "TurboGrafx-CD"},
	{"TGFX", 31, "TurboGrafx-16"},
	{"TGFX16", 31, "TurboGrafx-16"},
	{"TG16", 31, "TurboGrafx-16"},
	{"PCENGINE", 31, "PC Engine"},
	{"SGFX", 105, "SuperGrafx"},
	{"SGX", 105, "SuperGrafx"},
	{"PCFX", 72, "PC-FX"},

	// Atari
	{"A2600", 26, "Atari 2600"},
	{"ATARI", 26, "Atari 2600"},
	{"ATARI2600", 26, "Atari 2600"},
	{"A800", 43, "Atari 800"},
	{"ATARI800", 43, "Atari 800"},
	{"A5200", 40, "Atari 5200"},
	{"A7800", 41, "Atari 7800"},
	{"LYNX", 28, "Atari Lynx"},
	{"LNX", 28, "Atari Lynx"},
	{"JAGUAR", 27, "Atari Jaguar"},
	{"JAG", 27, "Atari Jaguar"},
	{"ST", 42, "Atari ST"},
	{"ATARIST", 42, "Atari ST"},

	// SNK
	{"NGP", 25, "Neo Geo Pocket"},
	{"NGPC", 82, "Neo Geo Pocket Color"},
	{"NEOGEO", 142, "Neo Geo"},
	{"NEO", 142, "Neo Geo"},
	{"NEOCD", 70, "Neo Geo CD"},
	{"NEOGEOCD", 70, "Neo Geo CD"},
	{"NGCD", 70, "Neo Geo CD"},

	// Arcade
	{"FBN", 75, "FinalBurn Neo"},
	{"FBNEO", 75, "FinalBurn Neo"},
	{"ARCADE", 75, "Arcade"},
	{"MAME", 75, "MAME"},
	{"FBA", 75, "FinalBurn Alpha"},
	{"MAME2003", 75, "MAME 2003"},
	{"MAME2003PLUS", 75, "MAME 2003-Plus"},
	{"MAME2010", 75, "MAME 2010"},
	{"CPS1", 6, "Capcom Play System"},
	{"CPS2", 7, "Capcom Play System II"},
	{"CPS3", 8, "Capcom Play System III"},

	// Home computers
	{"MSX", 113, "MSX"},
	{"MSX2", 116, "MSX2"},
	{"CPC", 65, "Amstrad CPC"},
	{"AMSTRAD", 65, "Amstrad CPC"},
	{"C64", 66, "Commodore 64"},
	{"C128", 66, "Commodore 128"},
	{"VIC", 73, "Commodore VIC-20"},
	{"VIC20", 73, "Commodore VIC-20"},
	{"PET", 240, "Commodore PET"},
	{"PLUS4", 99, "Commodore Plus/4"},
	{"AMIGA", 64, "Amiga"},
	{"PUAE", 64, "Amiga"},
	{"ZX", 76, "ZX Spectrum"},
	{"ZXSPECTRUM", 76, "ZX Spectrum"},
	{"SPECTRUM", 76, "ZX Spectrum"},
	{"ZXS", 76, "ZX Spectrum"},
	{"X68000", 79, "Sharp X68000"},

	// Coleco / Mattel
	{"COLECO", 48, "ColecoVision"},
	{"INTELLIVISION", 115, "Intellivision"},
	{"INTV", 115, "Intellivision"},
	{"INTELLI", 115, "Intellivision"},
	{"CV", 48, "ColecoVision"},

	// Wonderswan
	{"WS", 45, "WonderSwan"},
	{"WSC", 46, "WonderSwan Color"},
	{"WSWAN", 45, "WonderSwan"},
	{"WSWANC", 46, "WonderSwan Color"},

	// Misc
	{"ODYSSEY", 104, "Videopac / Odyssey2"},
	{"O2EM", 104, "Videopac / Odyssey2"},
	{"ODYSSEY2", 104, "Videopac / Odyssey2"},
	{"VIDEOPAC", 104, "Videopac / Odyssey2"},
	{"3DO", 29, "3DO"},
	{"MEGADUCK", 90, "Mega Duck"},
	{"GW", 52, "Game & Watch"},
	{"SUPERVISION", 207, "Watara Supervision"},
	{"SV", 207, "Watara Supervision"},
	{"VECTREX", 102, "Vectrex"},
	{"CHANNELF", 80, "Fairchild Channel F"},
	{"ARDUBOY", 263, "Arduboy"},
	{"PICO8", 234, "PICO-8"},
	{"P8", 234, "PICO-8"},
	{"PICO", 234, "PICO-8"},
	{"TIC80", 222, "TIC-80"},
	{"SCUMMVM", 123, "ScummVM"},
	{"SCUMM", 123, "ScummVM"},
	{"DOS", 135, "MS-DOS"},
	{"PRBOOM", 135, "Doom"},
	{"DOOM", 135, "Doom"},

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

// Extension of the last path component, or NULL (a dot in a directory name
// is not an extension).
static const char* romExtension(const char* rom_path) {
	const char* base = strrchr(rom_path, '/');
	base = base ? base + 1 : rom_path;
	const char* ext = strrchr(base, '.');
	return (ext && ext != base) ? ext + 1 : NULL;
}

int ScraperSystems_getIdForRom(const char* tag, const char* rom_path) {
	int id = ScraperSystems_getId(tag);
	if (!rom_path)
		return id;
	const char* ext = romExtension(rom_path);
	if (!ext)
		return id;

	// The Dreamcast folder also holds Naomi/Atomiswave MAME-style zips; those
	// are arcade sets matched by short zip name, like FBN (ratools_prefetch
	// hashes them under RA's Arcade console the same way).
	if (id == SS_ID_DREAMCAST)
		return (strcasecmp(ext, "zip") == 0 || strcasecmp(ext, "7z") == 0) ? SS_ID_ARCADE : id;
	if (id != SS_ID_MEGA_DRIVE)
		return id;

	// MD serves cartridge and Sega CD games, and the GPGX tag (Genesis Plus
	// GX) serves every Sega system under one tag. ScreenScraper searches per
	// system, so refine a Mega Drive base by extension the same way minarch's
	// RetroAchievements code (ra_do_load_game) and Genesis Plus GX itself do.
	static const char* cd_exts[] = {"cue", "chd", "ccd", "toc", "m3u", "iso", NULL};
	for (int i = 0; cd_exts[i]; i++)
		if (strcasecmp(ext, cd_exts[i]) == 0)
			return SS_ID_SEGA_CD;
	if (strcasecmp(ext, "sms") == 0)
		return SS_ID_MASTER_SYSTEM;
	if (strcasecmp(ext, "gg") == 0)
		return SS_ID_GAME_GEAR;
	if (strcasecmp(ext, "sg") == 0)
		return SS_ID_SG1000;
	return id;
}
