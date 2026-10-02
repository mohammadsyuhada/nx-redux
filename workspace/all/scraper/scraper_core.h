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

// stage is one of: "searching", "downloading", "compositing"
typedef void (*ScrapeProgressCb)(const char* stage, void* userdata);

// Variant art path: /Roms/GBA/.media/Game.png + "screenshot" ->
// /Roms/GBA/.media/screenshot/Game.png. A NULL/empty variant copies out_png.
void Scraper_variantPath(const char* out_png, const char* variant,
						 char* out, size_t out_size);

// Search ScreenScraper for `filename` (hashing `rom_path`) under `system_id`,
// download the available art and write every variant from that one download
// set: the mix composite to `out_png` (creating its .media dir), plus, when
// the source image exists, screenshot-only and box-art-only PNGs under
// .media/screenshot/ and .media/boxart/. Reports each stage via `cb` (may be
// NULL). Pure worker: no GFX, no globals — safe to call from the GUI queue
// thread or a headless process. A variant that fails to save does not fail
// the scrape; only the mix composite decides the result.
ScrapeResult scrapeOne(const char* filename, const char* rom_path, int system_id,
					   const char* out_png, ScrapeProgressCb cb, void* userdata);

#endif // SCRAPER_CORE_H
