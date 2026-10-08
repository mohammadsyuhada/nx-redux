#ifndef SCRAPER_PREFS_H
#define SCRAPER_PREFS_H

#include <stdbool.h>

// Which extra art the Artwork Manager downloads next to the 3D box art and
// screenshot. Stored as key=0|1 lines in creds_dir()/download.txt; a missing
// file, an unknown key or a value other than 1 leaves the field false.
typedef struct {
	bool box2d; // .media/boxart2d/
	bool wheel; // .media/wheel/
	bool mix;	// .media/mix/<name>.png, built from screenshot + box + wheel
} ScraperPrefs;

void ScraperPrefs_loadFrom(const char* path, ScraperPrefs* out);
bool ScraperPrefs_saveTo(const char* path, const ScraperPrefs* p);

// creds_dir()/download.txt; save creates creds_dir() first.
void ScraperPrefs_load(ScraperPrefs* out);
bool ScraperPrefs_save(const ScraperPrefs* p);

// A game counts as scraped: it has a screenshot or a box art, plus the mix when Generate mix is on.
bool ScraperPrefs_isComplete(const ScraperPrefs* p, bool has_screenshot, bool has_boxart, bool has_mix);

// The mix is built from the wheel, so it needs the wheel downloaded too.
static inline bool ScraperPrefs_wantWheel(const ScraperPrefs* p) {
	return p->wheel || p->mix;
}

#endif
