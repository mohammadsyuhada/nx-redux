#include "ratools_prefetch.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <rcheevos/rc_api_runtime.h>
#include <rcheevos/rc_api_user.h> // rc_api_start_session_request_t
#include <rcheevos/rc_hash.h>
#include <rcheevos/rc_consoles.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "http.h"
#include "ra_badges.h" // RA_BADGE_CACHE_DIR
#include "ra_consoles.h"
#include "ra_hash_cdreader.h"
#include "ra_offline.h"
#include "ui_buttonhintbar.h"
#include "utils.h"
#include "ui_downloadprogress.h"
#include "ui_menubar.h"

#define RAT_MAX_ROMS 4096

typedef struct {
	char path[512];
	int console_id;
} RAT_RomFile;

// Only prefetch systems whose emulator pak is actually installed, mirroring
// nextui's launchability rule (SD Emus/<platform>/<TAG>.pak first, then the
// system paks) - there is no point caching achievements for roms the device
// cannot launch.
static bool rat_has_emu(const char* tag) {
	char pak_path[512];
	getEmuPath((char*)tag, pak_path);
	return exists(pak_path);
}

// "Game Boy (GB)" -> "GB"
static bool rat_dir_tag(const char* dirname, char* tag, size_t n) {
	const char* o = strrchr(dirname, '(');
	if (!o)
		return false;
	const char* c = strchr(o, ')');
	if (!c || c <= o + 1)
		return false;
	size_t len = (size_t)(c - o - 1);
	if (len >= n)
		len = n - 1;
	memcpy(tag, o + 1, len);
	tag[len] = '\0';
	return true;
}

static bool rat_skip_extension(const char* name) {
	const char* ext = strrchr(name, '.');
	if (!ext)
		return true;
	ext++;
	static const char* deny[] = {"txt", "dat", "png", "jpg", "jpeg", "bmp", "xml",
								 "db", "sav", "srm", "st", "cfg", "log", "bak", NULL};
	for (int i = 0; deny[i]; i++)
		if (strcasecmp(ext, deny[i]) == 0)
			return true;
	return false;
}

static int rat_adjust_cd_console(int console_id, const char* path) {
	const char* ext = strrchr(path, '.');
	bool is_cd = ext && (!strcasecmp(ext, ".chd") || !strcasecmp(ext, ".cue") ||
						 !strcasecmp(ext, ".ccd") || !strcasecmp(ext, ".toc") ||
						 !strcasecmp(ext, ".m3u"));
	if (console_id == RC_CONSOLE_PC_ENGINE && is_cd)
		return RC_CONSOLE_PC_ENGINE_CD;
	if (console_id == RC_CONSOLE_MEGA_DRIVE && is_cd)
		return RC_CONSOLE_SEGA_CD;
	return console_id;
}

static void rat_scan_dir(const char* dir, int console_id, int depth,
						 RAT_RomFile* roms, int* count) {
	if (depth > 2 || *count >= RAT_MAX_ROMS)
		return;
	DIR* d = opendir(dir);
	if (!d)
		return;
	struct dirent* ent;
	while ((ent = readdir(d)) && *count < RAT_MAX_ROMS) {
		if (ent->d_name[0] == '.')
			continue;
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			rat_scan_dir(path, console_id, depth + 1, roms, count);
		} else if (!rat_skip_extension(ent->d_name)) {
			RAT_RomFile* r = &roms[(*count)++];
			snprintf(r->path, sizeof(r->path), "%s", path);
			r->console_id = rat_adjust_cd_console(console_id, path);
		}
	}
	closedir(d);
}

static int rat_collect_roms(RAT_RomFile** out) {
	RAT_RomFile* roms = (RAT_RomFile*)malloc(RAT_MAX_ROMS * sizeof(RAT_RomFile));
	*out = NULL;
	if (!roms)
		return 0;
	int count = 0;

	DIR* d = opendir(ROMS_PATH);
	if (!d) {
		free(roms);
		return 0;
	}
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		char tag[16];
		if (!rat_dir_tag(ent->d_name, tag, sizeof(tag)))
			continue;
		int console_id = RA_getConsoleId(tag);
		if (console_id == RC_CONSOLE_UNKNOWN)
			continue;
		if (!rat_has_emu(tag))
			continue; // no emulator installed for this system
		char dir[512];
		snprintf(dir, sizeof(dir), "%s/%s", ROMS_PATH, ent->d_name);
		rat_scan_dir(dir, console_id, 0, roms, &count);
	}
	closedir(d);

	if (count == 0) {
		free(roms);
		return 0;
	}
	*out = roms;
	return count;
}

// ---------------- network helpers (sync, cache-through) ----------------

// POST an rc_api request; on HTTP 200 mirror it into the offline cache and
// return the body (caller frees). NULL on failure.
static char* rat_post_and_cache(rc_api_request_t* request, size_t* out_len) {
	HTTP_Response* resp = HTTP_post(request->url, request->post_data, request->content_type);
	char* body = NULL;
	if (resp && resp->data && !resp->error && resp->http_status == 200) {
		RA_Offline_cacheResponse(request->post_data, resp->data, resp->size);
		body = (char*)malloc(resp->size + 1);
		if (body) {
			memcpy(body, resp->data, resp->size);
			body[resp->size] = '\0';
			if (out_len)
				*out_len = resp->size;
		}
	}
	if (resp)
		HTTP_freeResponse(resp);
	return body;
}

static void rat_download_badge(const char* url, const char* badge_name, bool locked) {
	if (!url || !*url || !badge_name || !*badge_name)
		return;
	char path[512];
	if (locked)
		snprintf(path, sizeof(path), RA_BADGE_CACHE_DIR "/%s_lock.png", badge_name);
	else
		snprintf(path, sizeof(path), RA_BADGE_CACHE_DIR "/%s.png", badge_name);
	struct stat st;
	if (stat(path, &st) == 0 && st.st_size > 0)
		return; // already cached
	HTTP_Response* resp = HTTP_get(url);
	if (resp && resp->data && !resp->error && resp->http_status == 200 && resp->size > 0) {
		FILE* f = fopen(path, "wb");
		if (f) {
			size_t wr = fwrite(resp->data, 1, resp->size, f);
			bool close_ok = fclose(f) == 0;
			if (wr != resp->size || !close_ok)
				remove(path); // truncated write must not satisfy the size>0 skip forever
		}
	}
	if (resp)
		HTTP_freeResponse(resp);
}

// ---------------- UI ----------------

static void rat_pf_render(SDL_Surface* screen, const char* line1, const char* line2,
						  int done, int total) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, "Download game data");

	char detail[192];
	snprintf(detail, sizeof(detail), "%s (%d/%d)", line2 ? line2 : "", done, total);

	UIDownloadProgress info = {
		.title = NULL, // menu bar drawn above
		.status = line1,
		.detail = detail,
		.progress = total > 0 ? (done * 100) / total : 0,
		.show_bar = true,
	};
	UI_renderDownloadProgress(screen, &info);

	UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(screen);
}

static void rat_pf_message(SDL_Surface* screen, const char* line1, const char* line2) {
	bool quit = false, dirty = true;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			quit = true;
		if (dirty) {
			GFX_clear(screen);
			SDL_Color white = {255, 255, 255, 255};
			SDL_Surface* s = TTF_RenderUTF8_Blended(font.medium, line1, white);
			if (s) {
				SDL_BlitSurface(s, NULL, screen,
								&(SDL_Rect){(screen->w - s->w) / 2, screen->h / 2 - SCALE1(30), s->w, s->h});
				SDL_FreeSurface(s);
			}
			if (line2) {
				s = TTF_RenderUTF8_Blended(font.small, line2, (SDL_Color){180, 180, 180, 255});
				if (s) {
					SDL_BlitSurface(s, NULL, screen,
									&(SDL_Rect){(screen->w - s->w) / 2, screen->h / 2, s->w, s->h});
					SDL_FreeSurface(s);
				}
			}
			UI_renderButtonHintBar(screen, (char*[]){"A", "OK", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

// Download any badge files still missing for a parsed sets response
// (rat_download_badge skips files already on disk). Returns false if the
// user cancelled with B mid-way.
static bool rat_download_set_badges(SDL_Surface* screen,
									const rc_api_fetch_game_sets_response_t* sets,
									const char* label, int done, int total) {
	int badge_total = 0;
	for (uint32_t s = 0; s < sets->num_sets; s++)
		badge_total += (int)sets->sets[s].num_achievements * 2; // colored + locked

	int badge_done = 0;
	char sub[192];
	snprintf(sub, sizeof(sub), "%s - badge 0/%d", label, badge_total);
	rat_pf_render(screen, "Downloading badges", sub, done, total);

	for (uint32_t s = 0; s < sets->num_sets; s++) {
		for (uint32_t a = 0; a < sets->sets[s].num_achievements; a++) {
			PAD_poll();
			if (PAD_justPressed(BTN_B))
				return false;
			const rc_api_achievement_definition_t* def = &sets->sets[s].achievements[a];
			rat_download_badge(def->badge_url, def->badge_name, false);
			rat_download_badge(def->badge_locked_url, def->badge_name, true);
			badge_done += 2;
			// refresh every few files so large sets visibly progress
			// (cheap vs the ~0.5s per actual download)
			if ((badge_done & 7) == 0 || badge_done == badge_total) {
				snprintf(sub, sizeof(sub), "%s - badge %d/%d", label, badge_done, badge_total);
				rat_pf_render(screen, "Downloading badges", sub, done, total);
			}
		}
	}
	return true;
}

void RATPrefetch_run(SDL_Surface* screen) {
	if (!CFG_getRAAuthenticated() || strlen(CFG_getRAToken()) == 0) {
		rat_pf_message(screen, "Not authenticated",
					   "Set credentials in Settings and authenticate first.");
		return;
	}
	if (!PLAT_wifiConnected()) {
		rat_pf_message(screen, "No network connection", "Connect to WiFi and try again.");
		return;
	}

	// badges directory may not exist on a fresh card (RA_offline's ".ra"
	// parent was already created by RA_Offline_init() in main())
	mkdir(RA_BADGE_CACHE_DIR, 0755);

	// CHD-aware hashing for disc images
	rc_hash_cdreader_t cdreader;
	RA_HashCdreader_get(&cdreader);
	rc_hash_init_custom_cdreader(&cdreader);

	RAT_RomFile* roms = NULL;
	int total = rat_collect_roms(&roms);
	if (total == 0) {
		rat_pf_message(screen, "No roms found", "Nothing to download.");
		return;
	}

	const char* username = CFG_getRAUsername();
	const char* token = CFG_getRAToken();
	int fetched = 0, cached = 0, unknown = 0, failed = 0;
	bool cancelled = false;

	for (int i = 0; i < total && !cancelled; i++) {
		PAD_poll();
		if (PAD_justPressed(BTN_B)) {
			cancelled = true;
			break;
		}

		const char* name = strrchr(roms[i].path, '/');
		name = name ? name + 1 : roms[i].path;
		rat_pf_render(screen, "Hashing", name, i + 1, total);

		char hash[33];
		if (!rc_hash_generate_from_file(hash, (uint32_t)roms[i].console_id, roms[i].path)) {
			failed++;
			continue;
		}

		// skip refetching games whose data is already cached — but still
		// back-fill the rom path and repair any badges a previous run (or
		// minarch's async in-game prefetch, cut off at quit) left missing;
		// rat_download_badge skips files already on disk, so a complete
		// game costs only stat() calls here
		char rel[192];
		snprintf(rel, sizeof(rel), "cache/games/%s/sets.json", hash);
		char* probe = NULL;
		size_t probe_len = 0;
		if (RA_Offline_readCacheFile(rel, &probe, &probe_len)) {
			RA_Offline_setGameRomPath(hash, roms[i].path);

			rc_api_server_response_t sr;
			memset(&sr, 0, sizeof(sr));
			sr.body = probe;
			sr.body_length = probe_len;
			sr.http_status_code = 200;
			rc_api_fetch_game_sets_response_t cached_sets;
			if (rc_api_process_fetch_game_sets_server_response(&cached_sets, &sr) == RC_OK &&
				cached_sets.response.succeeded) {
				// cancel mid-repair keeps sets.json: the data is complete and
				// the missing badges are retried on the next run anyway
				if (!rat_download_set_badges(screen, &cached_sets,
											 cached_sets.title ? cached_sets.title : name,
											 i + 1, total))
					cancelled = true;
			}
			rc_api_destroy_fetch_game_sets_response(&cached_sets);
			free(probe);
			cached++;
			continue;
		}

		rat_pf_render(screen, "Fetching achievement data", name, i + 1, total);

		// 1) hash -> game id
		rc_api_resolve_hash_request_t hreq;
		memset(&hreq, 0, sizeof(hreq));
		hreq.game_hash = hash;
		rc_api_request_t request;
		if (rc_api_init_resolve_hash_request(&request, &hreq) != RC_OK) {
			rc_api_destroy_request(&request);
			failed++;
			continue;
		}
		size_t blen = 0;
		char* body = rat_post_and_cache(&request, &blen);
		rc_api_destroy_request(&request);
		if (!body) {
			failed++;
			continue;
		}
		rc_api_resolve_hash_response_t hresp;
		rc_api_server_response_t sr = {.body = body, .body_length = blen, .http_status_code = 200};
		int hrc = rc_api_process_resolve_hash_server_response(&hresp, &sr);
		bool resolved = (hrc == RC_OK && hresp.response.succeeded);
		bool known = resolved && hresp.game_id != 0;
		rc_api_destroy_resolve_hash_response(&hresp);
		free(body);
		if (!resolved) {
			failed++; // server error / corrupt response — NOT "not in RA"
			continue;
		}
		if (!known) {
			unknown++; // authoritatively resolved: hash not in RA database
			continue;
		}

		// 2) achievement definitions (by hash)
		rc_api_fetch_game_sets_request_t sreq;
		memset(&sreq, 0, sizeof(sreq));
		sreq.username = username;
		sreq.api_token = token;
		sreq.game_hash = hash;
		if (rc_api_init_fetch_game_sets_request(&request, &sreq) != RC_OK) {
			rc_api_destroy_request(&request);
			failed++;
			continue;
		}
		body = rat_post_and_cache(&request, &blen);
		rc_api_destroy_request(&request);
		if (!body) {
			failed++;
			continue;
		}
		rc_api_fetch_game_sets_response_t sets;
		sr = (rc_api_server_response_t){.body = body, .body_length = blen, .http_status_code = 200};
		bool sets_ok = rc_api_process_fetch_game_sets_server_response(&sets, &sr) == RC_OK &&
					   sets.response.succeeded;
		free(body);
		if (!sets_ok) {
			rc_api_destroy_fetch_game_sets_response(&sets);
			failed++;
			continue;
		}

		// 3) user unlock state
		rc_api_start_session_request_t ssreq;
		memset(&ssreq, 0, sizeof(ssreq));
		ssreq.username = username;
		ssreq.api_token = token;
		ssreq.game_id = sets.session_game_id;
		if (rc_api_init_start_session_request(&request, &ssreq) == RC_OK) {
			body = rat_post_and_cache(&request, &blen);
			rc_api_destroy_request(&request);
			free(body); // cached as a side effect; content not needed here
		} else {
			// session cache is non-critical: skip the post but keep going
			rc_api_destroy_request(&request);
		}

		// 4) badges (both variants), with cancel checks between downloads
		if (!rat_download_set_badges(screen, &sets,
									 sets.title ? sets.title : name, i + 1, total))
			cancelled = true;
		if (cancelled) {
			// partial badge set: drop the cached sets.json so the next
			// prefetch run refetches this game instead of skipping it
			char sets_path[512];
			snprintf(sets_path, sizeof(sets_path),
					 SHARED_USERDATA_PATH "/.ra/cache/games/%s/sets.json", hash);
			remove(sets_path);
		}
		rc_api_destroy_fetch_game_sets_response(&sets);
		if (!cancelled) {
			RA_Offline_setGameRomPath(hash, roms[i].path);
			fetched++;
		}
	}

	char line1[64], line2[128];
	snprintf(line1, sizeof(line1), cancelled ? "Cancelled" : "Done");
	snprintf(line2, sizeof(line2), "%d fetched, %d already cached, %d not in RA, %d failed",
			 fetched, cached, unknown, failed);
	rat_pf_message(screen, line1, line2);
	free(roms);
}
