#ifndef SCRAPER_CORE_H
#define SCRAPER_CORE_H

#include <stdio.h>

#include "defines.h" // SHARED_USERDATA_PATH, MAX_PATH

#define TMP_DIR "/tmp/scraper"

// SHARED_USERDATA_PATH is a runtime array (not a string literal) on desktop
// builds (see paths.h), so it can no longer be adjacent-string-literal
// concatenated at compile time like the old CREDS_DIR/CREDS_USER/CREDS_PASS
// macros did; build the same paths with snprintf instead (byte-identical to
// the old macros on device, where SHARED_USERDATA_PATH is still a
// compile-time literal). static inline so each TU that doesn't call one
// doesn't warn about an unused static function.
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

// Search ScreenScraper for `filename` (hashing `rom_path`) under `system_id`,
// download available art, composite, and write `out_png` (creating its .media
// dir). Reports each stage via `cb` (may be NULL). Pure worker: no GFX, no
// globals — safe to call from the GUI queue thread or a headless process.
ScrapeResult scrapeOne(const char* filename, const char* rom_path, int system_id,
					   const char* out_png, ScrapeProgressCb cb, void* userdata);

#endif // SCRAPER_CORE_H
