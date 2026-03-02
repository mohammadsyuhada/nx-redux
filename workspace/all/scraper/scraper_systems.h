#ifndef SCRAPER_SYSTEMS_H
#define SCRAPER_SYSTEMS_H

// Get ScreenScraper system ID for a NextUI tag (e.g. "GBA", "SNES", "N64")
// Returns -1 if no mapping found
int ScraperSystems_getId(const char* tag);

// Get display name for a NextUI tag (e.g. "GBA" -> "Game Boy Advance")
// Returns the tag itself if no display name found
const char* ScraperSystems_getName(const char* tag);

#endif // SCRAPER_SYSTEMS_H
