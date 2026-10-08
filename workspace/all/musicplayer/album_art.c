#define _GNU_SOURCE
#include "album_art.h"
#include "wget_fetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include "api.h"
#include "utils.h"
#include "tz_country.h"
#include "parson/parson.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// Album art cache directory path on SD card
#define ALBUMART_CACHE_DIR SDCARD_PATH "/.cache/albumart"
#define CACHE_PARENT_DIR SDCARD_PATH "/.cache"

// Album art module state
typedef struct {
	SDL_Surface* album_art;
	char last_art_artist[256];
	char last_art_title[256];
	char loaded_art_artist[256];
	char loaded_art_title[256];
	unsigned int revision;
	bool art_fetch_in_progress;

	// Thread state
	pthread_t fetch_thread;
	bool thread_active;
	char req_artist[256];
	char req_title[256];

	// Thread result (written by thread, read by main)
	SDL_Surface* pending_art;
	bool result_ready;
} AlbumArtContext;

static AlbumArtContext art_ctx = {0};

// album_art_fetch() is called from the radio streaming thread (ICY/HLS metadata),
// while album_art_get()/_is_fetching()/_clear() run on the UI thread. Without
// serialization they can pthread_join() the same handle concurrently, create a
// new fetch thread over one being joined, and double-free pending_art. This lock
// makes those four entry points mutually exclusive. The worker thread
// (fetch_thread_func) deliberately does NOT take this lock, so holding it across
// pthread_join() cannot deadlock — the worker finishes on its own.
static pthread_mutex_t art_lock = PTHREAD_MUTEX_INITIALIZER;

// Get album art cache directory path (on SD card)
static void get_cache_dir(char* path, int path_size) {
	snprintf(path, path_size, "%s", ALBUMART_CACHE_DIR);
}

// Ensure cache directory exists
static void ensure_cache_dir(void) {
	// Create .cache directory on SD card
	mkdir(CACHE_PARENT_DIR, 0755);
	// Create albumart cache directory
	mkdir(ALBUMART_CACHE_DIR, 0755);
}

// Get cache file path for artist+title
static void get_cache_filepath(const char* artist, const char* title, char* path, int path_size) {
	char cache_dir[512];
	get_cache_dir(cache_dir, sizeof(cache_dir));

	// Create hash from artist+title
	char combined[512];
	snprintf(combined, sizeof(combined), "%s_%s", artist ? artist : "", title ? title : "");
	unsigned int hash = hashString(combined);

	snprintf(path, path_size, "%s/%08x.jpg", cache_dir, hash);
}

// Load album art from cache file
static SDL_Surface* load_cached_album_art(const char* cache_path) {
	FILE* f = fopen(cache_path, "rb");
	if (!f)
		return NULL;

	// Get file size
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 2 * 1024 * 1024) { // Max 2MB
		fclose(f);
		return NULL;
	}

	uint8_t* data = (uint8_t*)malloc(size);
	if (!data) {
		fclose(f);
		return NULL;
	}

	if (fread(data, 1, size, f) != (size_t)size) {
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);

	SDL_RWops* rw = SDL_RWFromConstMem(data, size);
	SDL_Surface* art = NULL;
	if (rw) {
		art = IMG_Load_RW(rw, 1);
	}
	free(data);

	return art;
}

// Save album art to cache file
static void save_album_art_to_cache(const char* cache_path, const uint8_t* data, int size) {
	FILE* f = fopen(cache_path, "wb");
	if (!f)
		return;

	fwrite(data, 1, size, f);
	fclose(f);
}

// URL encode a string for use in query parameters
// url_encode moved to common/utils.c as urlEncode()

#define SEARCH_RESPONSE_MAX (64 * 1024)

// Fetch url and parse it as JSON. Returns NULL on any failure.
static JSON_Value* fetch_json(const char* url) {
	uint8_t* buf = (uint8_t*)malloc(SEARCH_RESPONSE_MAX);
	if (!buf)
		return NULL;
	int bytes = wget_fetch(url, buf, SEARCH_RESPONSE_MAX - 1);
	if (bytes <= 0) {
		LOG_error("Album art search failed: %s\n", url);
		free(buf);
		return NULL;
	}
	buf[bytes] = '\0';
	JSON_Value* root = json_parse_string((const char*)buf);
	free(buf);
	if (!root)
		LOG_error("Album art search returned invalid JSON: %s\n", url);
	return root;
}

// Pick the search result to take art from: the first whose track title equals
// title (ignoring case), so a cover version or remix ranked higher doesn't win,
// else the first result. Returns NULL when there are no results.
static JSON_Object* pick_result(JSON_Array* results, const char* title_key, const char* title) {
	size_t count = json_array_get_count(results);
	for (size_t i = 0; title[0] && i < count; i++) {
		const char* t = json_object_get_string(json_array_get_object(results, i), title_key);
		if (t && strcasecmp(t, title) == 0)
			return json_array_get_object(results, i);
	}
	return json_array_get_object(results, 0);
}

// Search one iTunes storefront (two-letter country code) for term (already
// URL-encoded). On a hit, writes a 300x300 artwork URL into out.
static bool search_itunes(const char* term, const char* title, const char* country, char* out, int out_sz) {
	char url[1300];
	snprintf(url, sizeof(url), "https://itunes.apple.com/search?term=%s&media=music&limit=10&country=%s", term,
			 country);
	JSON_Value* root = fetch_json(url);
	if (!root)
		return false;
	JSON_Array* results = json_object_get_array(json_value_get_object(root), "results");
	const char* artwork_url = json_object_get_string(pick_result(results, "trackName", title), "artworkUrl100");
	if (!artwork_url) {
		json_value_free(root);
		return false;
	}

	// Convert HTTPS to HTTP for better compatibility
	if (strncmp(artwork_url, "https://", 8) == 0) {
		const char* after_https = artwork_url + 8;
		const char* ssl_pos = strstr(after_https, "-ssl.");
		if (ssl_pos)
			snprintf(out, out_sz, "http://%.*s%s", (int)(ssl_pos - after_https), after_https, ssl_pos + 4);
		else
			snprintf(out, out_sz, "http://%s", after_https);
	} else {
		snprintf(out, out_sz, "%s", artwork_url);
	}
	json_value_free(root);

	// Replace 100x100 with 300x300 for larger image
	char* size_str = strstr(out, "100x100");
	if (size_str)
		memcpy(size_str, "300x300", 7);
	return true;
}

// Search Deezer (one catalogue worldwide, no API key) for term. On a hit,
// writes the album's 500x500 cover URL into out.
static bool search_deezer(const char* term, const char* title, char* out, int out_sz) {
	char url[1300];
	snprintf(url, sizeof(url), "https://api.deezer.com/search?q=%s&limit=10", term);
	JSON_Value* root = fetch_json(url);
	if (!root)
		return false;
	JSON_Array* results = json_object_get_array(json_value_get_object(root), "data");
	const char* cover = json_object_dotget_string(pick_result(results, "title", title), "album.cover_big");
	if (cover)
		snprintf(out, out_sz, "%s", cover);
	json_value_free(root);
	return cover != NULL;
}

// Background thread: fetch album art (iTunes, then Deezer)
static void* fetch_thread_func(void* arg) {
	(void)arg;
	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	const char* artist = art_ctx.req_artist;
	const char* title = art_ctx.req_title;

	ensure_cache_dir();

	// Check disk cache first
	char cache_path[768];
	get_cache_filepath(artist, title, cache_path, sizeof(cache_path));

	SDL_Surface* cached_art = load_cached_album_art(cache_path);
	if (cached_art) {
		art_ctx.pending_art = cached_art;
		art_ctx.result_ready = true;
		return NULL;
	}

	char encoded_artist[512];
	char encoded_title[512];
	urlEncode(artist, encoded_artist, sizeof(encoded_artist));
	urlEncode(title, encoded_title, sizeof(encoded_title));

	char term[1100];
	if (artist[0] && title[0])
		snprintf(term, sizeof(term), "%s+%s", encoded_artist, encoded_title);
	else
		snprintf(term, sizeof(term), "%s", artist[0] ? encoded_artist : encoded_title);

	// iTunes in the device's storefront (from its timezone), then the US one,
	// and only then Deezer: storefronts carry different catalogues, and a lot
	// of regional music is missing from the US one.
	char large_artwork_url[512];
	char country[8];
	bool found = false;
	if (TZ_currentCountryCode(country, sizeof(country)) && strcasecmp(country, "US") != 0)
		found = search_itunes(term, title, country, large_artwork_url, sizeof(large_artwork_url));
	if (!found)
		found = search_itunes(term, title, "US", large_artwork_url, sizeof(large_artwork_url));
	if (!found)
		found = search_deezer(term, title, large_artwork_url, sizeof(large_artwork_url));
	if (!found) {
		LOG_info("No album art found for \"%s\" - \"%s\"\n", artist, title);
		art_ctx.result_ready = true;
		return NULL;
	}
	LOG_info("Album art for \"%s\" - \"%s\": %s\n", artist, title, large_artwork_url);

	// Download the image
	uint8_t* image_buf = (uint8_t*)malloc(1024 * 1024);
	if (!image_buf) {
		art_ctx.result_ready = true;
		return NULL;
	}

	int image_bytes = wget_fetch(large_artwork_url, image_buf, 1024 * 1024);
	if (image_bytes <= 0) {
		LOG_error("Failed to download album art image (bytes=%d)\n", image_bytes);
		free(image_buf);
		art_ctx.result_ready = true;
		return NULL;
	}

	// Load image into SDL_Surface
	SDL_RWops* rw = SDL_RWFromConstMem(image_buf, image_bytes);
	if (rw) {
		SDL_Surface* art = IMG_Load_RW(rw, 1);
		if (art) {
			// Save to disk cache for future use
			save_album_art_to_cache(cache_path, image_buf, image_bytes);
			art_ctx.pending_art = art;
		} else {
			LOG_error("Failed to load album art image: %s\n", IMG_GetError());
		}
	}

	free(image_buf);
	art_ctx.result_ready = true;
	return NULL;
}

void album_art_init(void) {
	memset(&art_ctx, 0, sizeof(AlbumArtContext));
}

void album_art_cleanup(void) {
	pthread_mutex_lock(&art_lock);
	// Wait for any active thread to finish
	if (art_ctx.thread_active) {
		pthread_join(art_ctx.fetch_thread, NULL);
		art_ctx.thread_active = false;
	}
	if (art_ctx.pending_art) {
		SDL_FreeSurface(art_ctx.pending_art);
		art_ctx.pending_art = NULL;
	}
	if (art_ctx.album_art) {
		SDL_FreeSurface(art_ctx.album_art);
		art_ctx.album_art = NULL;
	}
	art_ctx.last_art_artist[0] = '\0';
	art_ctx.last_art_title[0] = '\0';
	art_ctx.art_fetch_in_progress = false;
	pthread_mutex_unlock(&art_lock);
}

void album_art_load_path(const char* path) {
	if (!path || !path[0])
		return;
	pthread_mutex_lock(&art_lock);
	if (art_ctx.thread_active) {
		pthread_join(art_ctx.fetch_thread, NULL);
		art_ctx.thread_active = false;
		art_ctx.art_fetch_in_progress = false;
	}
	if (art_ctx.pending_art) {
		SDL_FreeSurface(art_ctx.pending_art);
		art_ctx.pending_art = NULL;
	}
	SDL_Surface* art = IMG_Load(path);
	if (art) {
		if (art_ctx.album_art)
			SDL_FreeSurface(art_ctx.album_art);
		art_ctx.album_art = art;
		art_ctx.revision++;
		art_ctx.result_ready = false;
		art_ctx.pending_art = NULL;
	}
	pthread_mutex_unlock(&art_lock);
}

void album_art_clear(void) {
	pthread_mutex_lock(&art_lock);
	// Wait for any active thread to finish before clearing
	if (art_ctx.thread_active) {
		pthread_join(art_ctx.fetch_thread, NULL);
		art_ctx.thread_active = false;
	}
	if (art_ctx.pending_art) {
		SDL_FreeSurface(art_ctx.pending_art);
		art_ctx.pending_art = NULL;
	}
	if (art_ctx.album_art) {
		SDL_FreeSurface(art_ctx.album_art);
		art_ctx.album_art = NULL;
	}
	art_ctx.last_art_artist[0] = '\0';
	art_ctx.last_art_title[0] = '\0';
	art_ctx.revision++;
	art_ctx.art_fetch_in_progress = false;
	art_ctx.result_ready = false;
	pthread_mutex_unlock(&art_lock);
}

unsigned int album_art_revision(void) {
	pthread_mutex_lock(&art_lock);
	unsigned int revision = art_ctx.revision;
	pthread_mutex_unlock(&art_lock);
	return revision;
}

bool album_art_matches(const char* artist, const char* title) {
	bool matches;
	pthread_mutex_lock(&art_lock);
	matches = art_ctx.album_art && strcmp(art_ctx.loaded_art_artist, artist ? artist : "") == 0 &&
			  strcmp(art_ctx.loaded_art_title, title ? title : "") == 0;
	pthread_mutex_unlock(&art_lock);
	return matches;
}

SDL_Surface* album_art_get(void) {
	pthread_mutex_lock(&art_lock);
	// Check if background thread has delivered a result
	if (art_ctx.result_ready && art_ctx.thread_active) {
		pthread_join(art_ctx.fetch_thread, NULL);
		art_ctx.thread_active = false;
		art_ctx.art_fetch_in_progress = false;
		art_ctx.result_ready = false;

		if (art_ctx.pending_art) {
			if (art_ctx.album_art) {
				SDL_FreeSurface(art_ctx.album_art);
			}
			art_ctx.album_art = art_ctx.pending_art;
			art_ctx.pending_art = NULL;
			strncpy(art_ctx.loaded_art_artist, art_ctx.req_artist, sizeof(art_ctx.loaded_art_artist) - 1);
			strncpy(art_ctx.loaded_art_title, art_ctx.req_title, sizeof(art_ctx.loaded_art_title) - 1);
			art_ctx.revision++;
		}
	}
	SDL_Surface* result = art_ctx.album_art;
	pthread_mutex_unlock(&art_lock);
	return result;
}

bool album_art_is_fetching(void) {
	// "Fetching" must end once the background thread posts its result —
	// consumers gate rendering (and their dirty flags) on this, and the flag
	// itself is only fully cleared inside album_art_get(). Reporting
	// in-progress until consumption deadlocks the radio screen: render waits
	// for !fetching, while !fetching waits for a render to call get().
	pthread_mutex_lock(&art_lock);
	bool fetching = art_ctx.art_fetch_in_progress && !art_ctx.result_ready;
	pthread_mutex_unlock(&art_lock);
	return fetching;
}

// Fetch album art from iTunes Search API (truly async, non-blocking)
void album_art_fetch(const char* artist, const char* title) {
	if (!artist || !title || (artist[0] == '\0' && title[0] == '\0')) {
		return;
	}

	pthread_mutex_lock(&art_lock);

	// Check if we already fetched art for this track
	if (strcmp(art_ctx.last_art_artist, artist) == 0 &&
		strcmp(art_ctx.last_art_title, title) == 0) {
		pthread_mutex_unlock(&art_lock);
		return; // Already fetched
	}

	// Wait for any previous thread to finish
	if (art_ctx.thread_active) {
		pthread_join(art_ctx.fetch_thread, NULL);
		art_ctx.thread_active = false;
		// Discard any pending result from previous fetch
		if (art_ctx.pending_art) {
			SDL_FreeSurface(art_ctx.pending_art);
			art_ctx.pending_art = NULL;
		}
	}

	// Save current track info
	art_ctx.art_fetch_in_progress = true;
	art_ctx.result_ready = false;
	art_ctx.pending_art = NULL;
	strncpy(art_ctx.last_art_artist, artist, sizeof(art_ctx.last_art_artist) - 1);
	strncpy(art_ctx.last_art_title, title, sizeof(art_ctx.last_art_title) - 1);
	strncpy(art_ctx.req_artist, artist, sizeof(art_ctx.req_artist) - 1);
	strncpy(art_ctx.req_title, title, sizeof(art_ctx.req_title) - 1);

	// Launch background thread
	if (pthread_create(&art_ctx.fetch_thread, NULL, fetch_thread_func, NULL) == 0) {
		art_ctx.thread_active = true;
	} else {
		// Thread creation failed, fall through
		art_ctx.art_fetch_in_progress = false;
	}
	pthread_mutex_unlock(&art_lock);
}

// Get the total size of the album art disk cache in bytes
long album_art_get_cache_size(void) {
	char cache_dir[512];
	get_cache_dir(cache_dir, sizeof(cache_dir));

	DIR* dir = opendir(cache_dir);
	if (!dir)
		return 0;

	long total_size = 0;
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char filepath[768];
		snprintf(filepath, sizeof(filepath), "%s/%s", cache_dir, ent->d_name);

		struct stat st;
		if (stat(filepath, &st) == 0) {
			total_size += st.st_size;
		}
	}
	closedir(dir);

	return total_size;
}

// Clear all cached album art from disk
void album_art_clear_disk_cache(void) {
	char cache_dir[512];
	get_cache_dir(cache_dir, sizeof(cache_dir));

	DIR* dir = opendir(cache_dir);
	if (!dir)
		return;

	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char filepath[768];
		snprintf(filepath, sizeof(filepath), "%s/%s", cache_dir, ent->d_name);
		unlink(filepath);
	}
	closedir(dir);

	// Also clear the in-memory album art since cached files are gone
	album_art_clear();
}
