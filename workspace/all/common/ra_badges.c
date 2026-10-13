#include "ra_badges.h"
#include "http.h"
#include "defines.h"
#include "api.h"
#include "notification.h"
#include "ra_badge_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Logging macros using NextUI's LOG_* infrastructure
#define BADGE_LOG_DEBUG(fmt, ...) LOG_debug("[RA_BADGES] " fmt, ##__VA_ARGS__)
#define BADGE_LOG_INFO(fmt, ...) LOG_info("[RA_BADGES] " fmt, ##__VA_ARGS__)
#define BADGE_LOG_WARN(fmt, ...) LOG_warn("[RA_BADGES] " fmt, ##__VA_ARGS__)
#define BADGE_LOG_ERROR(fmt, ...) LOG_error("[RA_BADGES] " fmt, ##__VA_ARGS__)

/*****************************************************************************
 * Constants
 *****************************************************************************/

#define RA_BADGE_BASE_URL "https://media.retroachievements.org/Badge/"
#define MAX_BADGE_NAME 32
// Each achievement caches two variants (colored + locked), so this must be
// 2x the largest achievement set we want to fully support. 2048 covers sets
// up to 1024 achievements (e.g. Pokemon Unbound's 844); at 256 anything past
// 128 achievements was silently dropped (upstream NextUI PR #689).
#define MAX_CACHED_BADGES 2048
#define MAX_CONCURRENT_DOWNLOADS 8
// The download queue must admit everything the cache can hold, or queue
// rejections silently drop badges long before the cache is full.
#define MAX_QUEUED_DOWNLOADS MAX_CACHED_BADGES
#define NOTIFICATION_TIMEOUT_MS 15000

/*****************************************************************************
 * Badge cache entry
 *****************************************************************************/

typedef struct {
	uint32_t game_id;
	char badge_name[MAX_BADGE_NAME];
	bool locked;
	RA_BadgeState state;
	SDL_Surface* surface;
	SDL_Surface* surface_scaled; // Pre-scaled for notifications
} BadgeCacheEntry;

/*****************************************************************************
 * Download queue entry
 *****************************************************************************/

typedef struct {
	uint32_t game_id;
	char badge_name[MAX_BADGE_NAME];
	bool locked;
} QueuedDownload;

/*****************************************************************************
 * Static state
 *****************************************************************************/

static BadgeCacheEntry badge_cache[MAX_CACHED_BADGES];
static int badge_cache_count = 0;
static SDL_mutex* badge_mutex = NULL;
static int pending_downloads = 0;
static bool initialized = false;
static uint32_t notification_start_time = 0;
// RA_Badges_setRomPath(): the loaded rom, which picks the badge folder. Set
// on the main thread under badge_mutex with the prefetch worker stopped.
static char badge_rom_path[MAX_PATH] = {0};

// "Loading achievement badges..." toast. Notification_* is not thread-safe,
// so worker and HTTP threads only request changes here; RA_Badges_update()
// applies them on the main thread. DONE holds until the next prefetch so a
// toast that timed out is not shown again for the same game.
typedef enum {
	PROGRESS_IDLE,
	PROGRESS_WANT_SHOW,
	PROGRESS_SHOWN,
	PROGRESS_DONE,
} ProgressState;
static ProgressState progress_state = PROGRESS_IDLE;

// Prefetch worker: checks which badges are already on disk off the main
// thread (one stat per file in the game's badge folder, a cold card still
// costs a few ms each) and queues only the missing ones.
typedef struct {
	uint32_t game_id;
	size_t count;
	char (*names)[MAX_BADGE_NAME];
} PrefetchJob;
static SDL_Thread* prefetch_thread = NULL;
static SDL_atomic_t prefetch_cancel;
static bool prefetch_running = false; // guarded by badge_mutex

// Download queue for rate limiting
typedef struct {
	QueuedDownload items[MAX_QUEUED_DOWNLOADS];
	int head;
	int tail;
	int count;
	int active;
} DownloadQueueState;

static DownloadQueueState download_queue = {0};

/*****************************************************************************
 * Internal helpers
 *****************************************************************************/

// Find or create cache entry for a badge
static BadgeCacheEntry* find_or_create_entry(uint32_t game_id, const char* badge_name, bool locked) {
	// Search existing entries
	for (int i = 0; i < badge_cache_count; i++) {
		if (badge_cache[i].locked == locked && badge_cache[i].game_id == game_id &&
			strcmp(badge_cache[i].badge_name, badge_name) == 0) {
			return &badge_cache[i];
		}
	}

	// Create new entry if space available
	if (badge_cache_count >= MAX_CACHED_BADGES) {
		BADGE_LOG_WARN("Cache full, cannot add badge %s\n", badge_name);
		return NULL;
	}

	BadgeCacheEntry* entry = &badge_cache[badge_cache_count++];
	memset(entry, 0, sizeof(BadgeCacheEntry));
	entry->game_id = game_id;
	strncpy(entry->badge_name, badge_name, MAX_BADGE_NAME - 1);
	entry->locked = locked;
	entry->state = RA_BADGE_STATE_UNKNOWN;

	return entry;
}

// Create the fallback badge dir if it doesn't exist (game folders are made
// per game, see RA_BadgePath_ensureDir)
static void ensure_cache_dir(void) {
	mkdir(RA_USER_DIR, 0755);
	mkdir(RA_USER_DIR "/cache", 0755);
	mkdir(RA_BADGE_CACHE_DIR, 0755);
}

// Check if cache file exists
static bool cache_file_exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && st.st_size > 0;
}

// Save HTTP response data to cache file
static bool save_to_cache(const char* path, const char* data, size_t size) {
	FILE* f = fopen(path, "wb");
	if (!f) {
		// the game's folder was missing (a badge outside prefetch)
		char dir[MAX_PATH];
		snprintf(dir, sizeof(dir), "%s", path);
		char* slash = strrchr(dir, '/');
		if (slash) {
			*slash = '\0';
			if (RA_BadgePath_ensureDir(dir))
				f = fopen(path, "wb");
		}
	}
	if (!f) {
		BADGE_LOG_ERROR("Failed to open cache file for writing: %s\n", path);
		return false;
	}

	size_t written = fwrite(data, 1, size, f);
	fclose(f);

	if (written != size) {
		BADGE_LOG_ERROR("Failed to write cache file: %s\n", path);
		unlink(path);
		return false;
	}

	return true;
}

// Load badge image from cache
static SDL_Surface* load_from_cache(const char* path) {
	SDL_Surface* surface = IMG_Load(path);
	if (!surface) {
		BADGE_LOG_WARN("Failed to load badge image: %s - %s\n", path, IMG_GetError());
		return NULL;
	}
	return surface;
}

// Scale a surface to target size using SDL_BlitScaled for proper format handling
static SDL_Surface* scale_surface(SDL_Surface* src, int target_size) {
	if (!src)
		return NULL;

	// Calculate scale factor to fit in target_size x target_size
	float scale_x = (float)target_size / src->w;
	float scale_y = (float)target_size / src->h;
	float scale = (scale_x < scale_y) ? scale_x : scale_y;

	int new_w = (int)(src->w * scale);
	int new_h = (int)(src->h * scale);

	// Create scaled surface with alpha support
	SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(
		0, new_w, new_h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!scaled) {
		return NULL;
	}

	// Clear to transparent
	SDL_FillRect(scaled, NULL, 0);

	// Use SDL_BlitScaled which handles pixel format conversion properly
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_Rect dst_rect = {0, 0, new_w, new_h};
	SDL_BlitScaled(src, NULL, scaled, &dst_rect);

	return scaled;
}

/*****************************************************************************
 * Download callback
 *****************************************************************************/

typedef struct {
	uint32_t game_id;
	char badge_name[MAX_BADGE_NAME];
	bool locked;
	char cache_path[MAX_PATH];
} DownloadContext;

// Forward declarations
static void process_download_queue(void);
static void badge_download_callback(HTTP_Response* response, void* userdata);

// Queue a download for later processing (must hold mutex)
static void queue_download(uint32_t game_id, const char* badge_name, bool locked) {
	if (download_queue.count >= MAX_QUEUED_DOWNLOADS) {
		BADGE_LOG_WARN("Download queue full, dropping badge %s\n", badge_name);
		return;
	}

	QueuedDownload* item = &download_queue.items[download_queue.tail];
	item->game_id = game_id;
	strncpy(item->badge_name, badge_name, MAX_BADGE_NAME - 1);
	item->badge_name[MAX_BADGE_NAME - 1] = '\0';
	item->locked = locked;

	download_queue.tail = (download_queue.tail + 1) % MAX_QUEUED_DOWNLOADS;
	download_queue.count++;
}

// Dequeue and start a download (must hold mutex)
static bool dequeue_and_start_download(void) {
	if (download_queue.count == 0)
		return false;

	QueuedDownload* item = &download_queue.items[download_queue.head];
	download_queue.head = (download_queue.head + 1) % MAX_QUEUED_DOWNLOADS;
	download_queue.count--;

	// Get entry and check if still needs download
	BadgeCacheEntry* entry = find_or_create_entry(item->game_id, item->badge_name, item->locked);
	if (!entry)
		return false;

	// Skip if already cached (might have been cached while queued)
	if (entry->state == RA_BADGE_STATE_CACHED) {
		return false;
	}

	// Build URL and cache path. No disk check here: whoever queued the badge
	// already found it missing, and this runs on the main thread and in HTTP
	// callbacks where a disk lookup would stall.
	char url[512];
	char cache_path[MAX_PATH];
	RA_Badges_getUrl(item->badge_name, item->locked, url, sizeof(url));
	if (!RA_Badges_getCachePath(item->game_id, item->badge_name, item->locked, cache_path, sizeof(cache_path)))
		return false;

	// Start download
	DownloadContext* ctx = (DownloadContext*)malloc(sizeof(DownloadContext));
	if (!ctx)
		return false;

	ctx->game_id = item->game_id;
	strncpy(ctx->badge_name, item->badge_name, MAX_BADGE_NAME - 1);
	ctx->badge_name[MAX_BADGE_NAME - 1] = '\0';
	ctx->locked = item->locked;
	strncpy(ctx->cache_path, cache_path, MAX_PATH - 1);
	ctx->cache_path[MAX_PATH - 1] = '\0';

	entry->state = RA_BADGE_STATE_DOWNLOADING;
	download_queue.active++;
	pending_downloads++;

	HTTP_getAsync(url, badge_download_callback, ctx);
	return true;
}

// Process queued downloads up to the concurrency limit (must hold mutex)
static void process_download_queue(void) {
	while (download_queue.active < MAX_CONCURRENT_DOWNLOADS && download_queue.count > 0) {
		if (!dequeue_and_start_download()) {
			// Item was skipped (already cached), try next
			continue;
		}
	}
}

static void badge_download_callback(HTTP_Response* response, void* userdata) {
	DownloadContext* ctx = (DownloadContext*)userdata;

	bool success = false;

	// Just save to disk - don't load into memory during prefetch
	// Images will be loaded lazily when actually needed for display
	if (response && response->data && response->http_status == 200 && !response->error) {
		success = save_to_cache(ctx->cache_path, response->data, response->size);
		if (!success) {
			BADGE_LOG_WARN("Failed to save badge %s%s to cache\n",
						   ctx->badge_name, ctx->locked ? "_lock" : "");
		}
	} else {
		BADGE_LOG_WARN("Failed to download badge %s%s: %s\n",
					   ctx->badge_name, ctx->locked ? "_lock" : "",
					   response && response->error ? response->error : "HTTP error");
	}

	// Only hold mutex briefly to update state
	if (badge_mutex)
		SDL_LockMutex(badge_mutex);

	download_queue.active--;
	if (download_queue.active < 0)
		download_queue.active = 0;

	pending_downloads--;
	if (pending_downloads < 0)
		pending_downloads = 0;

	BadgeCacheEntry* entry = find_or_create_entry(ctx->game_id, ctx->badge_name, ctx->locked);
	if (entry) {
		// Mark as cached (on disk) - surfaces will be loaded lazily
		entry->state = success ? RA_BADGE_STATE_CACHED : RA_BADGE_STATE_FAILED;
	}

	// Start next queued download(s); RA_Badges_update() hides the toast
	process_download_queue();

	if (badge_mutex)
		SDL_UnlockMutex(badge_mutex);

	if (response) {
		HTTP_freeResponse(response);
	}
	free(ctx);
}

// Request a badge download - queues if at concurrency limit
static void start_download(uint32_t game_id, const char* badge_name, bool locked) {
	if (!initialized)
		return;

	BadgeCacheEntry* entry = find_or_create_entry(game_id, badge_name, locked);
	if (!entry)
		return;

	// Check if already downloading or cached
	if (entry->state == RA_BADGE_STATE_DOWNLOADING ||
		entry->state == RA_BADGE_STATE_CACHED) {
		return;
	}

	// Check if already cached on disk
	char cache_path[MAX_PATH];
	if (!RA_Badges_getCachePath(game_id, badge_name, locked, cache_path, sizeof(cache_path)))
		return;
	if (cache_file_exists(cache_path)) {
		entry->state = RA_BADGE_STATE_CACHED;
		return;
	}

	// Queue the download - state will be set when download actually starts
	queue_download(game_id, badge_name, locked);
}

/*****************************************************************************
 * Public API
 *****************************************************************************/

void RA_Badges_init(void) {
	if (initialized)
		return;

	badge_mutex = SDL_CreateMutex();
	badge_cache_count = 0;
	pending_downloads = 0;
	download_queue.head = 0;
	download_queue.tail = 0;
	download_queue.count = 0;
	download_queue.active = 0;
	memset(badge_cache, 0, sizeof(badge_cache));

	ensure_cache_dir();

	initialized = true;
}

void RA_Badges_quit(void) {
	if (!initialized)
		return;

	RA_Badges_clearMemory();

	if (badge_mutex) {
		SDL_DestroyMutex(badge_mutex);
		badge_mutex = NULL;
	}
	badge_rom_path[0] = '\0';

	initialized = false;
}

// Stop the prefetch worker, if any. Must not hold badge_mutex.
static void stop_prefetch(void) {
	if (!prefetch_thread)
		return;
	SDL_AtomicSet(&prefetch_cancel, 1);
	SDL_WaitThread(prefetch_thread, NULL);
	prefetch_thread = NULL;
	SDL_AtomicSet(&prefetch_cancel, 0);
}

void RA_Badges_clearMemory(void) {
	if (!initialized)
		return;

	stop_prefetch();

	if (badge_mutex)
		SDL_LockMutex(badge_mutex);

	for (int i = 0; i < badge_cache_count; i++) {
		if (badge_cache[i].surface) {
			SDL_FreeSurface(badge_cache[i].surface);
			badge_cache[i].surface = NULL;
		}
		if (badge_cache[i].surface_scaled) {
			SDL_FreeSurface(badge_cache[i].surface_scaled);
			badge_cache[i].surface_scaled = NULL;
		}
	}
	badge_cache_count = 0;
	if (progress_state == PROGRESS_SHOWN)
		Notification_hideProgressIndicator();
	progress_state = PROGRESS_IDLE;

	if (badge_mutex)
		SDL_UnlockMutex(badge_mutex);
}

void RA_Badges_setRomPath(const char* rom_path) {
	if (!initialized)
		return;
	if (!rom_path)
		rom_path = "";
	if (strcmp(rom_path, badge_rom_path) == 0)
		return;
	// the entries and queued downloads point into the old rom's folder
	RA_Badges_clearMemory(); // also stops the prefetch worker
	SDL_LockMutex(badge_mutex);
	download_queue.head = 0;
	download_queue.tail = 0;
	download_queue.count = 0;
	snprintf(badge_rom_path, sizeof(badge_rom_path), "%s", rom_path);
	SDL_UnlockMutex(badge_mutex);
}

// Mark a badge found on disk (must hold mutex)
static void mark_cached(uint32_t game_id, const char* badge_name, bool locked) {
	BadgeCacheEntry* entry = find_or_create_entry(game_id, badge_name, locked);
	if (entry && entry->state != RA_BADGE_STATE_DOWNLOADING)
		entry->state = RA_BADGE_STATE_CACHED;
}

static int prefetch_worker(void* data) {
	PrefetchJob* job = (PrefetchJob*)data;
	// the game's folder: downloads land there
	char game_dir[MAX_PATH];
	if (RA_BadgePath_dir(RA_BADGE_CACHE_DIR, badge_rom_path, job->game_id, game_dir, sizeof(game_dir)))
		RA_BadgePath_ensureDir(game_dir);

	int queued = 0;
	for (size_t i = 0; i < job->count * 2; i++) {
		if (SDL_AtomicGet(&prefetch_cancel))
			break;
		const char* name = job->names[i / 2];
		bool locked = (i % 2) == 1;
		char cache_path[MAX_PATH];
		if (!RA_BadgePath_inDir(game_dir, name, locked, cache_path, sizeof(cache_path)))
			continue;
		bool exists = cache_file_exists(cache_path); // slow part, no lock held

		SDL_LockMutex(badge_mutex);
		if (exists) {
			mark_cached(job->game_id, name, locked);
		} else {
			BadgeCacheEntry* entry = find_or_create_entry(job->game_id, name, locked);
			if (entry && (entry->state == RA_BADGE_STATE_UNKNOWN ||
						  entry->state == RA_BADGE_STATE_FAILED)) {
				queue_download(job->game_id, name, locked);
				process_download_queue();
				queued++;
				if (progress_state == PROGRESS_IDLE)
					progress_state = PROGRESS_WANT_SHOW;
			}
		}
		SDL_UnlockMutex(badge_mutex);
	}
	BADGE_LOG_DEBUG("Badge check for game %u done: %d to download\n", job->game_id, queued);

	SDL_LockMutex(badge_mutex);
	prefetch_running = false;
	SDL_UnlockMutex(badge_mutex);

	free(job->names);
	free(job);
	return 0;
}

void RA_Badges_prefetch(uint32_t game_id, const char** badge_names, size_t count) {
	if (!initialized || game_id == 0 || !badge_names || count == 0)
		return;

	stop_prefetch();

	PrefetchJob* job = (PrefetchJob*)calloc(1, sizeof(PrefetchJob));
	if (!job)
		return;
	job->names = calloc(count, sizeof(*job->names));
	if (!job->names) {
		free(job);
		return;
	}
	job->game_id = game_id;
	for (size_t i = 0; i < count; i++) {
		if (badge_names[i] && badge_names[i][0]) {
			strncpy(job->names[job->count], badge_names[i], MAX_BADGE_NAME - 1);
			job->count++;
		}
	}

	SDL_LockMutex(badge_mutex);
	progress_state = PROGRESS_IDLE;
	prefetch_running = true;
	SDL_UnlockMutex(badge_mutex);

	prefetch_thread = SDL_CreateThread(prefetch_worker, "RABadgePrefetch", job);
	if (!prefetch_thread) {
		BADGE_LOG_WARN("Failed to start badge prefetch thread\n");
		SDL_LockMutex(badge_mutex);
		prefetch_running = false;
		SDL_UnlockMutex(badge_mutex);
		free(job->names);
		free(job);
	}
}

void RA_Badges_update(void) {
	if (!initialized)
		return;

	SDL_LockMutex(badge_mutex);
	if (progress_state == PROGRESS_WANT_SHOW) {
		Notification_setProgressIndicatorPersistent(true);
		Notification_showProgressIndicator("Loading achievement badges...", "", NULL);
		notification_start_time = SDL_GetTicks();
		progress_state = PROGRESS_SHOWN;
	} else if (progress_state == PROGRESS_SHOWN) {
		bool done = !prefetch_running && pending_downloads == 0 && download_queue.count == 0;
		// Force hide after the timeout, even if downloads aren't complete
		if (done || SDL_GetTicks() - notification_start_time >= NOTIFICATION_TIMEOUT_MS) {
			Notification_hideProgressIndicator();
			progress_state = PROGRESS_DONE;
		}
	}
	SDL_UnlockMutex(badge_mutex);
}

// A badge marked cached failed to load (file deleted or damaged): fetch it
// again. Must hold mutex.
static void handle_missing_cached(BadgeCacheEntry* entry) {
	entry->state = RA_BADGE_STATE_UNKNOWN;
	queue_download(entry->game_id, entry->badge_name, entry->locked);
	process_download_queue();
}


SDL_Surface* RA_Badges_get(uint32_t game_id, const char* badge_name, bool locked) {
	if (!initialized || game_id == 0 || !badge_name || !badge_name[0])
		return NULL;

	if (badge_mutex)
		SDL_LockMutex(badge_mutex);

	BadgeCacheEntry* entry = find_or_create_entry(game_id, badge_name, locked);
	SDL_Surface* result = NULL;

	if (entry) {
		if (entry->state == RA_BADGE_STATE_CACHED) {
			// Lazy load from disk if not in memory
			if (!entry->surface) {
				char cache_path[MAX_PATH];
				RA_Badges_getCachePath(game_id, badge_name, locked, cache_path, sizeof(cache_path));
				entry->surface = load_from_cache(cache_path);
				if (entry->surface) {
					entry->surface_scaled = scale_surface(entry->surface, RA_BADGE_NOTIFY_SIZE);
				} else {
					handle_missing_cached(entry);
				}
			}
			result = entry->surface;
		} else if (entry->state == RA_BADGE_STATE_UNKNOWN) {
			// Trigger download
			start_download(game_id, badge_name, locked);
		}
	}

	if (badge_mutex)
		SDL_UnlockMutex(badge_mutex);

	return result;
}

SDL_Surface* RA_Badges_getNotificationSize(uint32_t game_id, const char* badge_name, bool locked) {
	if (!initialized || game_id == 0 || !badge_name || !badge_name[0])
		return NULL;

	if (badge_mutex)
		SDL_LockMutex(badge_mutex);

	BadgeCacheEntry* entry = find_or_create_entry(game_id, badge_name, locked);
	SDL_Surface* result = NULL;

	if (entry) {
		if (entry->state == RA_BADGE_STATE_CACHED) {
			// Lazy load from disk if not in memory
			if (!entry->surface_scaled) {
				char cache_path[MAX_PATH];
				RA_Badges_getCachePath(game_id, badge_name, locked, cache_path, sizeof(cache_path));
				entry->surface = load_from_cache(cache_path);
				if (entry->surface) {
					entry->surface_scaled = scale_surface(entry->surface, RA_BADGE_NOTIFY_SIZE);
				} else {
					handle_missing_cached(entry);
				}
			}
			result = entry->surface_scaled;
		} else if (entry->state == RA_BADGE_STATE_UNKNOWN) {
			// Trigger download
			start_download(game_id, badge_name, locked);
		}
	}

	if (badge_mutex)
		SDL_UnlockMutex(badge_mutex);

	return result;
}


bool RA_Badges_getCachePath(uint32_t game_id, const char* badge_name, bool locked, char* buffer,
							size_t buffer_size) {
	return RA_BadgePath_file(RA_BADGE_CACHE_DIR, badge_rom_path, game_id, badge_name, locked, buffer,
							 buffer_size);
}

void RA_Badges_getUrl(const char* badge_name, bool locked, char* buffer, size_t buffer_size) {
	if (locked) {
		snprintf(buffer, buffer_size, "%s%s_lock.png", RA_BADGE_BASE_URL, badge_name);
	} else {
		snprintf(buffer, buffer_size, "%s%s.png", RA_BADGE_BASE_URL, badge_name);
	}
}
