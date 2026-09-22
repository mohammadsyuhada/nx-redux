#ifndef SCRAPER_SYSTEMS_H
#define SCRAPER_SYSTEMS_H

// Get ScreenScraper system ID for a NxRedux tag (e.g. "GBA", "SNES", "N64")
// Returns -1 if no mapping found
int ScraperSystems_getId(const char* tag);

// Same, for one ROM: a Mega Drive tag (MD, or GPGX which covers every Sega
// system under one tag) is refined by the ROM's extension — .sms Master
// System, .gg Game Gear, .sg SG-1000, CD images / .cue / .m3u Sega CD.
// A Dreamcast tag with a .zip/.7z ROM (Naomi / Atomiswave MAME set) becomes
// Arcade. Other tags return ScraperSystems_getId(tag) unchanged.
int ScraperSystems_getIdForRom(const char* tag, const char* rom_path);

#endif // SCRAPER_SYSTEMS_H
