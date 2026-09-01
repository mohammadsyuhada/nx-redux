#include "scraper_fetch.h"

#include <stdio.h>
#include <string.h>

#include "api.h"		  // SDL + GFX types; SDL_Init
#include "utils.h"		  // getFile, putFile
#include "scraper_core.h" // scrapeOne, CREDS_*
#include "scraper_api.h"
#include "scraper_systems.h"

static const char* arg_after(int argc, char** argv, const char* key) {
	for (int i = 1; i < argc - 1; i++)
		if (strcmp(argv[i], key) == 0)
			return argv[i + 1];
	return NULL;
}

static void fetch_status_cb(const char* stage, void* userdata) {
	putFile((char*)userdata, (char*)stage); // userdata = status-file path
}

int run_headless_fetch(int argc, char* argv[]) {
	const char* rom = arg_after(argc, argv, "--fetch");
	const char* out = arg_after(argc, argv, "--out");
	const char* tag = arg_after(argc, argv, "--system");
	const char* status = arg_after(argc, argv, "--status");
	if (!rom || !out || !tag || !status) {
		fprintf(stderr,
				"usage: scraper.elf --fetch <rom> --out <png> --system <TAG> --status <file>\n");
		return 2;
	}

	// Headless init only — NO GFX_init/video. SDL_Init(0) is defensive; the
	// compositor uses software surfaces + SDL_image (lazy-init, like the GUI,
	// which never calls IMG_Init). See spec open-item: headless SDL minimality.
	SDL_Init(0);
	ScraperAPI_init();

	// Credentials (anonymous if the files are absent) — same source as the GUI.
	char user[64] = "", pass[128] = "";
	getFile(creds_user_path(), user, sizeof(user));
	getFile(creds_pass_path(), pass, sizeof(pass));
	char* nl;
	if ((nl = strchr(user, '\n')))
		*nl = '\0';
	if ((nl = strchr(pass, '\n')))
		*nl = '\0';
	ScraperAPI_setUserCredentials(user, pass);

	int sid = ScraperSystems_getId(tag);
	if (sid < 0) {
		putFile((char*)status, "error");
		return 1;
	}

	putFile((char*)status, "searching");
	const char* fname = strrchr(rom, '/');
	fname = fname ? fname + 1 : rom;

	ScrapeResult r = scrapeOne(fname, rom, sid, out, fetch_status_cb, (void*)status);
	if (r == SCRAPE_RESULT_OK) {
		putFile((char*)status, "done");
		return 0;
	}
	if (r == SCRAPE_RESULT_NOTFOUND) {
		putFile((char*)status, "notfound");
		return 2;
	}
	putFile((char*)status, "error");
	return 1;
}
