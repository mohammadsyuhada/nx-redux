#ifndef SCRAPER_CORE_H
#define SCRAPER_CORE_H

#include <stdio.h>

#include "defines.h" // SHARED_USERDATA_PATH, MAX_PATH

#define TMP_DIR "/tmp/scraper"

// Credential paths under SHARED_USERDATA_PATH, built with snprintf (the
// values match the old CREDS_DIR/CREDS_USER/CREDS_PASS macros). static
// inline so each TU that doesn't call one doesn't warn about an unused static
// function.
static inline char* creds_dir(void) {
	static char buf[MAX_PATH];
	snprintf(buf, sizeof(buf), "%s/.scraper", SHARED_USERDATA_PATH);
	return buf;
}
static inline char* creds_user_path(void) {
	static char buf[MAX_PATH];
	snprintf(buf, sizeof(buf), "%s/.scraper/ss_user.txt", SHARED_USERDATA_PATH);
	return buf;
}
static inline char* creds_pass_path(void) {
	static char buf[MAX_PATH];
	snprintf(buf, sizeof(buf), "%s/.scraper/ss_pass.txt", SHARED_USERDATA_PATH);
	return buf;
}

typedef enum {
	SCRAPE_RESULT_OK = 0,
	SCRAPE_RESULT_ERROR = 1,
	SCRAPE_RESULT_NOTFOUND = 2,
} ScrapeResult;

// stage is one of: "searching", "downloading", "saving", "compositing"
typedef void (*ScrapeProgressCb)(const char* stage, void* userdata);

// Variant art path: /Roms/GBA/.media/Game.png + "screenshot" ->
// /Roms/GBA/.media/screenshot/Game.png. A NULL/empty variant copies out_png.
void Scraper_variantPath(const char* out_png, const char* variant,
						 char* out, size_t out_size);

// Reset artwork for one .media directory: delete the PNGs in each scraper variant folder (screenshot/, boxart/,
// boxart2d/, wheel/, mix/) and remove each folder once it is empty. The root .media/*.png pictures are the user's own
// art (Ports, hand-made pictures) and the folder backgrounds: never touched. Returns the number of files deleted.
int Scraper_deleteMediaArtwork(const char* media_path);

// Reset artwork across `roms_root`: Scraper_deleteMediaArtwork for every folder's .media, nested game folders too
// (e.g. Roms/GB/Hacks/.media), skipping hidden folders. Returns the number of files deleted.
int Scraper_deleteAllArtwork(const char* roms_root);

// Search ScreenScraper for `filename` (hashing `rom_path`) under `system_id`, download its screenshot and box art and
// save each that exists as a PNG under .media/screenshot/ and .media/boxart/ next to `out_png` (the .media/<name>.png
// base path). The download preferences (scraper_prefs.h) add .media/boxart2d/ and .media/wheel/ (full colour), and the
// mix composite at .media/mix/ (full colour); `out_png` itself is not written. Reports each stage via `cb` (may be NULL). Pure worker: no GFX, no globals — safe to call from
// the GUI queue thread or a headless process. OK when the screenshot or the box art saved.
ScrapeResult scrapeOne(const char* filename, const char* rom_path, int system_id,
					   const char* out_png, ScrapeProgressCb cb, void* userdata);

#endif // SCRAPER_CORE_H
