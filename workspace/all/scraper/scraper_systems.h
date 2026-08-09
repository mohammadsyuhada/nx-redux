#ifndef SCRAPER_SYSTEMS_H
#define SCRAPER_SYSTEMS_H

// Get ScreenScraper system ID for a NxRedux tag (e.g. "GBA", "SNES", "N64")
// Returns -1 if no mapping found
int ScraperSystems_getId(const char* tag);

#endif // SCRAPER_SYSTEMS_H
