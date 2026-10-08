#ifndef __RA_CONSOLES_H__
#define __RA_CONSOLES_H__

/**
 * RetroAchievements Console ID Mapping for NextUI
 * 
 * Maps NextUI EMU_TAGs to rcheevos RC_CONSOLE_* constants.
 * This is used to identify games when loading them for achievement tracking.
 */

#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <rcheevos/rc_consoles.h>

/**
 * Mapping entry from EMU tag to RetroAchievements console ID
 */
typedef struct {
	const char* emu_tag;
	int console_id;
} RA_ConsoleMapping;

/**
 * Lookup table mapping NextUI EMU tags to RC_CONSOLE_* constants.
 * Sorted alphabetically by emu_tag for readability/maintainability.
 */
static const RA_ConsoleMapping ra_console_table[] = {
	// Atari
	{"A2600", RC_CONSOLE_ATARI_2600},
	{"A5200", RC_CONSOLE_ATARI_5200},
	{"A7800", RC_CONSOLE_ATARI_7800},
	// Sega 32X
	{"32X", RC_CONSOLE_SEGA_32X},
	// Commodore
	{"C128", RC_CONSOLE_COMMODORE_64}, // Uses C64 for RA
	{"C64", RC_CONSOLE_COMMODORE_64},
	// ColecoVision
	{"COLECO", RC_CONSOLE_COLECOVISION},
	// Amstrad
	{"CPC", RC_CONSOLE_AMSTRAD_PC},
	// Dreamcast (standalone flycast pak). The folder also carries NAOMI/
	// Atomiswave .zip sets, which prefetch refines to the "Arcade" console.
	{"DC", RC_CONSOLE_DREAMCAST},
	// Dreamcast Lite (DCX.pak: libretro's 2022 flycast fork). Same games as DC.
	{"DCX", RC_CONSOLE_DREAMCAST},
	// Nintendo
	{"FC", RC_CONSOLE_NINTENDO},
	// FinalBurn Neo
	{"FBN", RC_CONSOLE_ARCADE},
	// Famicom Disk System
	{"FDS", RC_CONSOLE_FAMICOM_DISK_SYSTEM},
	// Game Boy
	{"GB", RC_CONSOLE_GAMEBOY},
	// Game Boy Advance
	{"GBA", RC_CONSOLE_GAMEBOY_ADVANCE},
	// Game Boy Color
	{"GBC", RC_CONSOLE_GAMEBOY_COLOR},
	// Game Gear
	{"GG", RC_CONSOLE_GAME_GEAR},
	// Genesis Plus GX. Base is Mega Drive; ra_do_load_game refines it by ROM
	// extension so this one tag also covers Master System/Game Gear/SG-1000/CD.
	{"GPGX", RC_CONSOLE_MEGA_DRIVE},
	// Atari Lynx
	{"LYNX", RC_CONSOLE_ATARI_LYNX},
	// Mega Drive/Genesis
	{"MD", RC_CONSOLE_MEGA_DRIVE},
	// GBA (mGBA)
	{"MGBA", RC_CONSOLE_GAMEBOY_ADVANCE},
	// MSX
	{"MSX", RC_CONSOLE_MSX},
	// Neo Geo Pocket
	{"NGP", RC_CONSOLE_NEOGEO_POCKET},
	// Neo Geo Pocket Color
	{"NGPC", RC_CONSOLE_NEOGEO_POCKET},
	// PICO-8
	{"P8", RC_CONSOLE_PICO},
	// PC Engine
	{"PCE", RC_CONSOLE_PC_ENGINE},
	// Not supported (no RA)
	{"PET", RC_CONSOLE_UNKNOWN},
	// Pokemon Mini
	{"PKM", RC_CONSOLE_POKEMON_MINI},
	// Not supported (no RA)
	{"PLUS4", RC_CONSOLE_UNKNOWN},
	// PrBoom (no RA)
	{"PRBOOM", RC_CONSOLE_UNKNOWN},
	// PlayStation
	{"PS", RC_CONSOLE_PLAYSTATION},
	// PlayStation (SwanStation)
	{"PSX", RC_CONSOLE_PLAYSTATION},
	// PlayStation Portable (PPSSPP libretro core)
	{"PSP", RC_CONSOLE_PSP},
	// Amiga
	{"PUAE", RC_CONSOLE_AMIGA},
	// Sega CD
	{"SEGACD", RC_CONSOLE_SEGA_CD},
	// Super Famicom/SNES
	{"SFC", RC_CONSOLE_SUPER_NINTENDO},
	// SG-1000
	{"SG1000", RC_CONSOLE_SG1000},
	// Super Game Boy
	{"SGB", RC_CONSOLE_GAMEBOY},
	// Master System
	{"SMS", RC_CONSOLE_MASTER_SYSTEM},
	// Super Famicom (Supafaust)
	{"SUPA", RC_CONSOLE_SUPER_NINTENDO},
	// Virtual Boy
	{"VB", RC_CONSOLE_VIRTUAL_BOY},
	// VIC-20
	{"VIC", RC_CONSOLE_VIC20},
	// WonderSwan / WonderSwan Color (no bundled core; e.g. the Pak Store
	// WSC.pak runs mednafen_wswan in minarch). RA uses one console for both.
	{"WS", RC_CONSOLE_WONDERSWAN},
	{"WSC", RC_CONSOLE_WONDERSWAN},
};

#define RA_CONSOLE_TABLE_SIZE (sizeof(ra_console_table) / sizeof(ra_console_table[0]))

/**
 * Get the RetroAchievements console ID for a given EMU tag.
 * @param emu_tag The NextUI emulator tag (e.g., "GB", "SFC", "PS")
 * @return The RC_CONSOLE_* constant, or RC_CONSOLE_UNKNOWN if not supported
 */
static inline int RA_getConsoleId(const char* emu_tag) {
	if (!emu_tag || !*emu_tag) {
		return RC_CONSOLE_UNKNOWN;
	}

	for (size_t i = 0; i < RA_CONSOLE_TABLE_SIZE; i++) {
		if (strcmp(emu_tag, ra_console_table[i].emu_tag) == 0) {
			return ra_console_table[i].console_id;
		}
	}

	return RC_CONSOLE_UNKNOWN;
}

/*****************************************************************************
 * Console detection
 *
 * The tag alone can't tell which system a game is for: a custom pak can use
 * any tag, and one core can run several systems (Genesis Plus GX, mGBA,
 * PicoDrive, Flycast with Naomi sets). RA needs the right console up front:
 * it picks the hash method and the memory map, and the memory regions must
 * exist before rcheevos validates achievement addresses during the load.
 *
 * RA_detectConsole resolves it locally, before anything is fetched, from:
 *   1. the libretro core actually running (what it can emulate),
 *   2. the ROM extension (which of those systems this file is),
 *   3. the folder tag (the default when the extension doesn't decide),
 *   4. the extension alone, for a custom pak with an unknown core and tag.
 * rcheevos' own hash iterator is no substitute: its candidate list names
 * hash methods ("Mega Drive" stands in for every full-file console), not
 * systems.
 *****************************************************************************/

#define RA_FAMILY_MAX 6

// libretro core (file name minus "_libretro.so") -> systems it runs; the first
// entry is its main system. Entries ending in '*' match by prefix (versioned
// builds: stella2014, puae2021, vice_x64...).
typedef struct {
	const char* core;
	int consoles[RA_FAMILY_MAX];
} RA_CoreFamily;

static const RA_CoreFamily ra_core_families[] = {
	{"a5200", {RC_CONSOLE_ATARI_5200}},
	{"beetle_psx*", {RC_CONSOLE_PLAYSTATION}},
	{"bluemsx", {RC_CONSOLE_MSX, RC_CONSOLE_COLECOVISION, RC_CONSOLE_SG1000}},
	{"bsnes*", {RC_CONSOLE_SUPER_NINTENDO}},
	{"cap32", {RC_CONSOLE_AMSTRAD_PC}},
	{"fake08", {RC_CONSOLE_PICO}},
	{"fbalpha*", {RC_CONSOLE_ARCADE}},
	{"fbneo", {RC_CONSOLE_ARCADE}},
	{"fceumm", {RC_CONSOLE_NINTENDO, RC_CONSOLE_FAMICOM_DISK_SYSTEM}},
	{"flycast", {RC_CONSOLE_DREAMCAST, RC_CONSOLE_ARCADE}},
	{"flycast_legacy", {RC_CONSOLE_DREAMCAST, RC_CONSOLE_ARCADE}},
	{"fmsx", {RC_CONSOLE_MSX}},
	{"gambatte", {RC_CONSOLE_GAMEBOY, RC_CONSOLE_GAMEBOY_COLOR}},
	{"gearboy", {RC_CONSOLE_GAMEBOY, RC_CONSOLE_GAMEBOY_COLOR}},
	{"gearcoleco", {RC_CONSOLE_COLECOVISION}},
	{"gearsystem", {RC_CONSOLE_MASTER_SYSTEM, RC_CONSOLE_GAME_GEAR, RC_CONSOLE_SG1000}},
	{"genesis_plus_gx*", {RC_CONSOLE_MEGA_DRIVE, RC_CONSOLE_MASTER_SYSTEM, RC_CONSOLE_GAME_GEAR, RC_CONSOLE_SG1000, RC_CONSOLE_SEGA_CD}},
	{"gpsp", {RC_CONSOLE_GAMEBOY_ADVANCE}},
	{"handy", {RC_CONSOLE_ATARI_LYNX}},
	{"mame*", {RC_CONSOLE_ARCADE}},
	{"mednafen_gba", {RC_CONSOLE_GAMEBOY_ADVANCE}},
	{"mednafen_lynx", {RC_CONSOLE_ATARI_LYNX}},
	{"mednafen_ngp", {RC_CONSOLE_NEOGEO_POCKET}},
	{"mednafen_pce*", {RC_CONSOLE_PC_ENGINE, RC_CONSOLE_PC_ENGINE_CD}},
	{"mednafen_pcfx", {RC_CONSOLE_PCFX}},
	{"mednafen_psx*", {RC_CONSOLE_PLAYSTATION}},
	{"mednafen_saturn", {RC_CONSOLE_SATURN}},
	{"mednafen_snes", {RC_CONSOLE_SUPER_NINTENDO}},
	{"mednafen_supafaust", {RC_CONSOLE_SUPER_NINTENDO}},
	{"mednafen_supergrafx", {RC_CONSOLE_PC_ENGINE, RC_CONSOLE_PC_ENGINE_CD}},
	{"mednafen_vb", {RC_CONSOLE_VIRTUAL_BOY}},
	{"mednafen_wswan", {RC_CONSOLE_WONDERSWAN}},
	{"melonds*", {RC_CONSOLE_NINTENDO_DS}},
	{"mesen", {RC_CONSOLE_NINTENDO, RC_CONSOLE_FAMICOM_DISK_SYSTEM}},
	{"mgba", {RC_CONSOLE_GAMEBOY_ADVANCE, RC_CONSOLE_GAMEBOY, RC_CONSOLE_GAMEBOY_COLOR}},
	{"mupen64plus*", {RC_CONSOLE_NINTENDO_64}},
	{"nestopia", {RC_CONSOLE_NINTENDO, RC_CONSOLE_FAMICOM_DISK_SYSTEM}},
	{"o2em", {RC_CONSOLE_MAGNAVOX_ODYSSEY2}},
	{"opera", {RC_CONSOLE_3DO}},
	{"parallel_n64", {RC_CONSOLE_NINTENDO_64}},
	{"pcsx_rearmed", {RC_CONSOLE_PLAYSTATION}},
	{"picodrive", {RC_CONSOLE_MEGA_DRIVE, RC_CONSOLE_MASTER_SYSTEM, RC_CONSOLE_GAME_GEAR, RC_CONSOLE_SG1000, RC_CONSOLE_SEGA_CD, RC_CONSOLE_SEGA_32X}},
	{"pokemini", {RC_CONSOLE_POKEMON_MINI}},
	{"ppsspp", {RC_CONSOLE_PSP}},
	{"prosystem", {RC_CONSOLE_ATARI_7800}},
	{"puae*", {RC_CONSOLE_AMIGA}},
	{"race", {RC_CONSOLE_NEOGEO_POCKET}},
	{"sameboy", {RC_CONSOLE_GAMEBOY, RC_CONSOLE_GAMEBOY_COLOR}},
	{"snes9x*", {RC_CONSOLE_SUPER_NINTENDO}},
	{"stella*", {RC_CONSOLE_ATARI_2600}},
	{"swanstation", {RC_CONSOLE_PLAYSTATION}},
	{"tgbdual", {RC_CONSOLE_GAMEBOY, RC_CONSOLE_GAMEBOY_COLOR}},
	{"vba_next", {RC_CONSOLE_GAMEBOY_ADVANCE}},
	{"vbam", {RC_CONSOLE_GAMEBOY_ADVANCE, RC_CONSOLE_GAMEBOY, RC_CONSOLE_GAMEBOY_COLOR}},
	{"vecx", {RC_CONSOLE_VECTREX}},
	{"vice_x128", {RC_CONSOLE_COMMODORE_64}}, // RA tracks C128 software as C64
	{"vice_x64*", {RC_CONSOLE_COMMODORE_64}},
	{"vice_xvic", {RC_CONSOLE_VIC20}},
	{"virtualjaguar", {RC_CONSOLE_ATARI_JAGUAR}},
	{"yabasanshiro", {RC_CONSOLE_SATURN}},
	{"yabause", {RC_CONSOLE_SATURN}},
};

// Folder tags that cover several systems when the core is unknown (a stock
// tag with a replaced core keeps today's behaviour).
typedef struct {
	const char* emu_tag;
	int consoles[RA_FAMILY_MAX];
} RA_TagFamily;

static const RA_TagFamily ra_tag_families[] = {
	{"DC", {RC_CONSOLE_DREAMCAST, RC_CONSOLE_ARCADE}},
	{"DCX", {RC_CONSOLE_DREAMCAST, RC_CONSOLE_ARCADE}},
	{"GPGX", {RC_CONSOLE_MEGA_DRIVE, RC_CONSOLE_MASTER_SYSTEM, RC_CONSOLE_GAME_GEAR, RC_CONSOLE_SG1000, RC_CONSOLE_SEGA_CD}},
	{"MD", {RC_CONSOLE_MEGA_DRIVE, RC_CONSOLE_MASTER_SYSTEM, RC_CONSOLE_GAME_GEAR, RC_CONSOLE_SG1000, RC_CONSOLE_SEGA_CD}},
	{"PCE", {RC_CONSOLE_PC_ENGINE, RC_CONSOLE_PC_ENGINE_CD}},
};

// ROM extensions that name exactly one RA console
typedef struct {
	const char* ext;
	int console_id;
} RA_ExtConsole;

static const RA_ExtConsole ra_ext_consoles[] = {
	{"32x", RC_CONSOLE_SEGA_32X},
	{"a26", RC_CONSOLE_ATARI_2600},
	{"a52", RC_CONSOLE_ATARI_5200},
	{"a78", RC_CONSOLE_ATARI_7800},
	{"col", RC_CONSOLE_COLECOVISION},
	{"cso", RC_CONSOLE_PSP},
	{"fds", RC_CONSOLE_FAMICOM_DISK_SYSTEM},
	{"gb", RC_CONSOLE_GAMEBOY},
	{"gba", RC_CONSOLE_GAMEBOY_ADVANCE},
	{"gbc", RC_CONSOLE_GAMEBOY_COLOR},
	{"gdi", RC_CONSOLE_DREAMCAST},
	{"gen", RC_CONSOLE_MEGA_DRIVE},
	{"gg", RC_CONSOLE_GAME_GEAR},
	{"j64", RC_CONSOLE_ATARI_JAGUAR},
	{"lnx", RC_CONSOLE_ATARI_LYNX},
	{"md", RC_CONSOLE_MEGA_DRIVE},
	{"min", RC_CONSOLE_POKEMON_MINI},
	{"n64", RC_CONSOLE_NINTENDO_64},
	{"nds", RC_CONSOLE_NINTENDO_DS},
	{"nes", RC_CONSOLE_NINTENDO},
	{"ngc", RC_CONSOLE_NEOGEO_POCKET},
	{"ngp", RC_CONSOLE_NEOGEO_POCKET},
	{"p8", RC_CONSOLE_PICO},
	{"pbp", RC_CONSOLE_PSP},
	{"pce", RC_CONSOLE_PC_ENGINE},
	{"sfc", RC_CONSOLE_SUPER_NINTENDO},
	{"sg", RC_CONSOLE_SG1000},
	{"sgx", RC_CONSOLE_PC_ENGINE},
	{"smc", RC_CONSOLE_SUPER_NINTENDO},
	{"smd", RC_CONSOLE_MEGA_DRIVE},
	{"sms", RC_CONSOLE_MASTER_SYSTEM},
	{"v64", RC_CONSOLE_NINTENDO_64},
	{"vb", RC_CONSOLE_VIRTUAL_BOY},
	{"ws", RC_CONSOLE_WONDERSWAN},
	{"wsc", RC_CONSOLE_WONDERSWAN},
	{"z64", RC_CONSOLE_NINTENDO_64},
};

static inline bool ra_console_is_disc(int console_id) {
	switch (console_id) {
	case RC_CONSOLE_PC_ENGINE_CD:
	case RC_CONSOLE_SEGA_CD:
	case RC_CONSOLE_PLAYSTATION:
	case RC_CONSOLE_PLAYSTATION_2:
	case RC_CONSOLE_PSP:
	case RC_CONSOLE_DREAMCAST:
	case RC_CONSOLE_SATURN:
	case RC_CONSOLE_3DO:
	case RC_CONSOLE_NEO_GEO_CD:
	case RC_CONSOLE_PCFX:
	case RC_CONSOLE_ATARI_JAGUAR_CD:
		return true;
	default:
		return false;
	}
}

// Extension (no dot, any case) of a disc image or playlist
static inline bool ra_ext_is_disc(const char* ext) {
	static const char* disc[] = {"chd", "cue", "ccd", "toc", "iso", "m3u", "cdi", "gdi", "img"};
	for (size_t i = 0; i < sizeof(disc) / sizeof(disc[0]); i++)
		if (strcasecmp(ext, disc[i]) == 0)
			return true;
	return false;
}

static inline const int* ra_core_family(const char* core_name) {
	if (!core_name || !*core_name)
		return NULL;
	for (size_t i = 0; i < sizeof(ra_core_families) / sizeof(ra_core_families[0]); i++) {
		const char* c = ra_core_families[i].core;
		size_t n = strlen(c);
		if (n && c[n - 1] == '*' ? strncasecmp(core_name, c, n - 1) == 0
								 : strcasecmp(core_name, c) == 0)
			return ra_core_families[i].consoles;
	}
	return NULL;
}

static inline bool ra_family_has(const int* family, int console_id) {
	for (int i = 0; i < RA_FAMILY_MAX && family[i]; i++)
		if (family[i] == console_id)
			return true;
	return false;
}

/**
 * The RA console for a game. emu_tag and core_name may be NULL/empty
 * (core_name is the libretro core file name minus "_libretro.so", as
 * minarch's core.name and a pak's EMU_EXE). Returns RC_CONSOLE_UNKNOWN when
 * the game can't be placed (or the tag is one RA doesn't support).
 */
static inline int RA_detectConsole(const char* emu_tag, const char* core_name, const char* rom_path) {
	int tag_console = RA_getConsoleId(emu_tag);
	if (tag_console == RC_CONSOLE_UNKNOWN && emu_tag && *emu_tag) {
		// listed with no console (PET, PLUS4, PRBOOM): RA doesn't support it
		for (size_t i = 0; i < RA_CONSOLE_TABLE_SIZE; i++)
			if (strcmp(emu_tag, ra_console_table[i].emu_tag) == 0)
				return RC_CONSOLE_UNKNOWN;
	}

	const char* ext = rom_path ? strrchr(rom_path, '.') : NULL;
	ext = (ext && !strchr(ext, '/')) ? ext + 1 : "";
	int ext_console = RC_CONSOLE_UNKNOWN;
	for (size_t i = 0; i < sizeof(ra_ext_consoles) / sizeof(ra_ext_consoles[0]); i++) {
		if (strcasecmp(ext, ra_ext_consoles[i].ext) == 0) {
			ext_console = ra_ext_consoles[i].console_id;
			break;
		}
	}

	// the systems this game can be: the core's, else the tag's
	int tag_single[RA_FAMILY_MAX] = {tag_console};
	const int* family = ra_core_family(core_name);
	if (!family && tag_console != RC_CONSOLE_UNKNOWN) {
		family = tag_single;
		for (size_t i = 0; i < sizeof(ra_tag_families) / sizeof(ra_tag_families[0]); i++) {
			if (strcmp(emu_tag, ra_tag_families[i].emu_tag) == 0) {
				family = ra_tag_families[i].consoles;
				break;
			}
		}
	}
	if (!family) // unknown core and tag: only an unambiguous extension places it
		return ext_console;

	if (ext_console != RC_CONSOLE_UNKNOWN && ra_family_has(family, ext_console))
		return ext_console;
	if (ra_ext_is_disc(ext)) {
		if (ra_console_is_disc(tag_console) && ra_family_has(family, tag_console))
			return tag_console;
		for (int i = 0; i < RA_FAMILY_MAX && family[i]; i++)
			if (ra_console_is_disc(family[i]))
				return family[i];
	}
	if ((strcasecmp(ext, "zip") == 0 || strcasecmp(ext, "7z") == 0) &&
		ra_family_has(family, RC_CONSOLE_ARCADE))
		return RC_CONSOLE_ARCADE;
	if (tag_console != RC_CONSOLE_UNKNOWN && ra_family_has(family, tag_console))
		return tag_console;
	return family[0];
}

#endif // __RA_CONSOLES_H__
