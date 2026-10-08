#include "scraper_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h" // LOG_warn
#include "utils.h"
#include "scraper_api.h"
#include "scraper_compositor.h"
#include "scraper_prefs.h"

// Up to this size, full colour: the 2D box art and the wheel, big enough for the Backdrop item slot and the List box
#define FULL_COLOUR_MAX_W COMPOSITOR_LIST_ART_MAX_W
#define FULL_COLOUR_MAX_H COMPOSITOR_LIST_ART_MAX_H

// Scale `image_path` on its own and save it under .media/<variant>/: fitted to 640x480 as a 256-colour PNG, or with
// `full_colour` shrunk to fit 480x576 (never grown) as a 32-bit RGBA PNG. True when saved.
static bool saveVariant(const char* image_path, const char* out_png, const char* variant, bool full_colour) {
	if (!image_path)
		return false;

	char variant_path[MAX_PATH];
	Scraper_variantPath(out_png, variant, variant_path, sizeof(variant_path));

	SDL_Surface* single = full_colour ? Compositor_createUpTo(image_path, FULL_COLOUR_MAX_W, FULL_COLOUR_MAX_H)
									  : Compositor_createSingle(image_path);
	if (!single) {
		LOG_warn("Scraper: could not build %s variant for %s\n", variant, out_png);
		return false;
	}
	// Both savers create the parent directory.
	bool saved = full_colour ? Compositor_savePNGRGBA(single, variant_path) : Compositor_savePNG(single, variant_path);
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

	ScraperPrefs prefs;
	ScraperPrefs_load(&prefs);

	if (cb)
		cb("downloading", userdata);
	// the screenshot and the box art (3D, else 2D) always; the 2D box art and the wheel when asked for (the mix needs
	// the wheel too)
	char ss_path[512] = "", box_path[512] = "", box2d_path[512] = "", wheel_path[512] = "";
	bool has_ss = false, has_box = false, has_box2d = false, has_wheel = false;
	if (info.screenshot_url[0]) {
		snprintf(ss_path, sizeof(ss_path), "%s/screenshot.png", TMP_DIR);
		has_ss = ScraperAPI_downloadFile(info.screenshot_url, ss_path);
	}
	if (info.boxart_url[0]) {
		snprintf(box_path, sizeof(box_path), "%s/boxart.png", TMP_DIR);
		has_box = ScraperAPI_downloadFile(info.boxart_url, box_path);
	}
	if (prefs.box2d && info.boxart2d_url[0]) {
		snprintf(box2d_path, sizeof(box2d_path), "%s/boxart2d.png", TMP_DIR);
		has_box2d = ScraperAPI_downloadFile(info.boxart2d_url, box2d_path);
	}
	if (ScraperPrefs_wantWheel(&prefs) && info.wheel_url[0]) {
		snprintf(wheel_path, sizeof(wheel_path), "%s/wheel.png", TMP_DIR);
		has_wheel = ScraperAPI_downloadFile(info.wheel_url, wheel_path);
	}

	bool saved_ss = false, saved_box = false;
	if (has_ss || has_box) {
		if (cb)
			cb("saving", userdata);
		saved_ss = saveVariant(has_ss ? ss_path : NULL, out_png, "screenshot", false);
		saved_box = saveVariant(has_box ? box_path : NULL, out_png, "boxart", false);
		if (has_box2d)
			saveVariant(box2d_path, out_png, "boxart2d", true);
		if (has_wheel)
			saveVariant(wheel_path, out_png, "wheel", true);

		// The mix goes to .media/mix/, full colour; the root out_png is not written.
		if (prefs.mix) {
			if (cb)
				cb("compositing", userdata);
			if (!Compositor_saveMix(has_ss ? ss_path : NULL, has_box ? box_path : NULL,
									has_wheel ? wheel_path : NULL, out_png))
				LOG_warn("Scraper: could not save the mix for %s\n", out_png);
		}
	}

	if (has_ss)
		remove(ss_path);
	if (has_box)
		remove(box_path);
	if (has_box2d)
		remove(box2d_path);
	if (has_wheel)
		remove(wheel_path);

	if (!has_ss && !has_box)
		return SCRAPE_RESULT_NOTFOUND;
	return saved_ss || saved_box ? SCRAPE_RESULT_OK : SCRAPE_RESULT_ERROR;
}
