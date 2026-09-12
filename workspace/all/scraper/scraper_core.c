#include "scraper_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h" // LOG_warn
#include "utils.h"
#include "scraper_api.h"
#include "scraper_compositor.h"

// Composite `image_path` on its own and save it under .media/<variant>/.
// Best effort: a missing source or a failed save is not an error for the
// scrape as a whole (only the mix composite decides that).
static void saveVariant(const char* image_path, const char* out_png, const char* variant) {
	if (!image_path)
		return;

	char variant_path[MAX_PATH];
	Scraper_variantPath(out_png, variant, variant_path, sizeof(variant_path));

	SDL_Surface* single = Compositor_createSingle(image_path);
	if (!single) {
		LOG_warn("Scraper: could not build %s variant for %s\n", variant, out_png);
		return;
	}
	// Compositor_savePNG creates the parent directory.
	if (!Compositor_savePNG(single, variant_path))
		LOG_warn("Scraper: could not save %s\n", variant_path);
	SDL_FreeSurface(single);
}

ScrapeResult scrapeOne(const char* filename, const char* rom_path, int system_id,
					   const char* out_png, ScrapeProgressCb cb, void* userdata) {
	mkdir_p(TMP_DIR);

	if (cb)
		cb("searching", userdata);
	ScraperGameInfo info;
	if (!ScraperAPI_search(filename, rom_path, system_id, &info))
		return SCRAPE_RESULT_NOTFOUND;

	if (cb)
		cb("downloading", userdata);
	char ss_path[512] = "", box_path[512] = "", wheel_path[512] = "";
	bool has_ss = false, has_box = false, has_wheel = false;
	if (info.screenshot_url[0]) {
		snprintf(ss_path, sizeof(ss_path), "%s/screenshot.png", TMP_DIR);
		has_ss = ScraperAPI_downloadFile(info.screenshot_url, ss_path);
	}
	if (info.boxart_url[0]) {
		snprintf(box_path, sizeof(box_path), "%s/boxart.png", TMP_DIR);
		has_box = ScraperAPI_downloadFile(info.boxart_url, box_path);
	}
	if (info.wheel_url[0]) {
		snprintf(wheel_path, sizeof(wheel_path), "%s/wheel.png", TMP_DIR);
		has_wheel = ScraperAPI_downloadFile(info.wheel_url, wheel_path);
	}
	if (!has_ss && !has_box && !has_wheel)
		return SCRAPE_RESULT_NOTFOUND;

	if (cb)
		cb("compositing", userdata);
	SDL_Surface* artwork = Compositor_create(
		has_ss ? ss_path : NULL, has_box ? box_path : NULL, has_wheel ? wheel_path : NULL);

	// ensure the .media directory of out_png exists
	char media_dir[512];
	snprintf(media_dir, sizeof(media_dir), "%s", out_png);
	char* slash = strrchr(media_dir, '/');
	if (slash) {
		*slash = '\0';
		mkdir_p(media_dir);
	}

	bool saved = false;
	if (artwork) {
		saved = Compositor_savePNG(artwork, out_png);
		SDL_FreeSurface(artwork);
	}

	// Single-image variants from the same downloads: no extra API requests,
	// so the launcher can switch between them without re-scraping.
	saveVariant(has_ss ? ss_path : NULL, out_png, "screenshot");
	saveVariant(has_box ? box_path : NULL, out_png, "boxart");

	if (has_ss)
		remove(ss_path);
	if (has_box)
		remove(box_path);
	if (has_wheel)
		remove(wheel_path);

	return saved ? SCRAPE_RESULT_OK : SCRAPE_RESULT_ERROR;
}
