#include "scraper_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h" // LOG_warn
#include "utils.h"
#include "scraper_api.h"
#include "scraper_compositor.h"

// Scale `image_path` on its own and save it under .media/<variant>/. True when saved.
static bool saveVariant(const char* image_path, const char* out_png, const char* variant) {
	if (!image_path)
		return false;

	char variant_path[MAX_PATH];
	Scraper_variantPath(out_png, variant, variant_path, sizeof(variant_path));

	SDL_Surface* single = Compositor_createSingle(image_path);
	if (!single) {
		LOG_warn("Scraper: could not build %s variant for %s\n", variant, out_png);
		return false;
	}
	// Compositor_savePNG creates the parent directory.
	bool saved = Compositor_savePNG(single, variant_path);
	if (!saved)
		LOG_warn("Scraper: could not save %s\n", variant_path);
	SDL_FreeSurface(single);
	return saved;
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
	// the screenshot and the box art: the menus use them on their own (the mix composite and the wheel logo it
	// needed are no longer made)
	char ss_path[512] = "", box_path[512] = "";
	bool has_ss = false, has_box = false;
	if (info.screenshot_url[0]) {
		snprintf(ss_path, sizeof(ss_path), "%s/screenshot.png", TMP_DIR);
		has_ss = ScraperAPI_downloadFile(info.screenshot_url, ss_path);
	}
	if (info.boxart_url[0]) {
		snprintf(box_path, sizeof(box_path), "%s/boxart.png", TMP_DIR);
		has_box = ScraperAPI_downloadFile(info.boxart_url, box_path);
	}
	if (!has_ss && !has_box)
		return SCRAPE_RESULT_NOTFOUND;

	if (cb)
		cb("saving", userdata);
	bool saved_ss = saveVariant(has_ss ? ss_path : NULL, out_png, "screenshot");
	bool saved_box = saveVariant(has_box ? box_path : NULL, out_png, "boxart");

	if (has_ss)
		remove(ss_path);
	if (has_box)
		remove(box_path);

	return saved_ss || saved_box ? SCRAPE_RESULT_OK : SCRAPE_RESULT_ERROR;
}
