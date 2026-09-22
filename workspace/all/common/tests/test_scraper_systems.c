// Host test for scraper_systems.c: the Artwork Manager maps a Roms folder
// tag to a ScreenScraper system. The GPGX tag (Genesis Plus GX) serves every
// Sega system under one tag, so the per-ROM lookup must refine a Mega Drive
// tag by the ROM's extension, exactly as RetroAchievements does in minarch.
#include <assert.h>
#include <stdio.h>

#include "../../scraper/scraper_systems.h"

// ScreenScraper ids used by the table.
#define SS_MEGA_DRIVE 1
#define SS_MASTER_SYSTEM 2
#define SS_SEGA_CD 20
#define SS_GAME_GEAR 21
#define SS_SG1000 109
#define SS_GBA 12
#define SS_DREAMCAST 23
#define SS_ARCADE 75

int main(void) {
	// Plain tag lookup: GPGX is a Mega Drive tag, case-insensitive.
	assert(ScraperSystems_getId("GPGX") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getId("gpgx") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getId("MD") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getId("NOPE") == -1);
	assert(ScraperSystems_getId(NULL) == -1);

	// Per-ROM refinement of a Mega Drive tag by extension.
	assert(ScraperSystems_getIdForRom("GPGX", "/mnt/SDCARD/Roms/Sega Genesis (GPGX)/Sonic.md") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Genesis (GPGX)/Sonic.bin") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Genesis (GPGX)/Sonic.gen") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Master System (GPGX)/Alex Kidd.sms") == SS_MASTER_SYSTEM);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Master System (GPGX)/Alex Kidd.SMS") == SS_MASTER_SYSTEM);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Game Gear (GPGX)/Shinobi.gg") == SS_GAME_GEAR);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega SG-1000 (GPGX)/Flicky.sg") == SS_SG1000);
	// CD images and folder games (the walk hands over the folder's .cue/.m3u).
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega CD (GPGX)/Sonic CD.chd") == SS_SEGA_CD);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega CD (GPGX)/Sonic CD/Sonic CD.cue") == SS_SEGA_CD);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega CD (GPGX)/Lunar/Lunar.m3u") == SS_SEGA_CD);
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega CD (GPGX)/Snatcher.iso") == SS_SEGA_CD);
	// The plain MD tag gets the same refinement (an .sms dropped in a Genesis folder).
	assert(ScraperSystems_getIdForRom("MD", "/Roms/Sega Genesis (MD)/Alex Kidd.sms") == SS_MASTER_SYSTEM);
	assert(ScraperSystems_getIdForRom("MD", "/Roms/Sega Genesis (MD)/Sonic CD.chd") == SS_SEGA_CD);
	// A dot inside a directory name is not an extension.
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Genesis (GPGX)/v1.0.sms/rom") == SS_MEGA_DRIVE);
	// Non-Sega tags are untouched, unknown tags stay unknown, NULL path is safe.
	assert(ScraperSystems_getIdForRom("GBA", "/Roms/GBA/game.sms") == SS_GBA);
	assert(ScraperSystems_getIdForRom("NOPE", "/Roms/x/game.sms") == -1);
	assert(ScraperSystems_getIdForRom("GPGX", NULL) == SS_MEGA_DRIVE);

	// Naomi / Atomiswave MAME-style zips sit in the Dreamcast folder and are
	// arcade sets, matched by short zip name like FBN (RA prefetch does the same).
	assert(ScraperSystems_getIdForRom("DC", "/Roms/DreamCast (DC)/Shenmue.chd") == SS_DREAMCAST);
	assert(ScraperSystems_getIdForRom("DC", "/Roms/DreamCast (DC)/Sonic Adventure.gdi") == SS_DREAMCAST);
	assert(ScraperSystems_getIdForRom("DC", "/Roms/DreamCast (DC)/mslug6.zip") == SS_ARCADE);
	assert(ScraperSystems_getIdForRom("DC", "/Roms/DreamCast (DC)/mslug6.ZIP") == SS_ARCADE);
	assert(ScraperSystems_getIdForRom("DC", "/Roms/DreamCast (DC)/dolphin.7z") == SS_ARCADE);
	assert(ScraperSystems_getIdForRom("DC", "/Roms/DreamCast (DC)/mslug6.zip/x") == SS_DREAMCAST);
	// Zips under any other tag stay on that tag's system.
	assert(ScraperSystems_getIdForRom("GPGX", "/Roms/Sega Genesis (GPGX)/Sonic.zip") == SS_MEGA_DRIVE);
	assert(ScraperSystems_getIdForRom("GBA", "/Roms/GBA/game.zip") == SS_GBA);

	printf("test_scraper_systems: OK\n");
	return 0;
}
