#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_emptystate.h"
#include "ui_loadingoverlay.h"
#include "ui_menubar.h"
#include "ui_splash.h"
#include "ui_quitrequest.h"
#include "ui_list.h"
#include "ui_listview.h"
#include "utils.h"

#include "scraper_api.h"
#include "scraper_systems.h"
#include "scraper_core.h"
#include "scraper_fetch.h"
#include "ui_keyboard.h"
#include "display_helper.h"

// ============================================
// Constants
// ============================================

#define MAX_SYSTEMS 128
#define MAX_ROMS 4096
#define MAX_QUEUE 2048

// ROM file extensions to consider
static const char* rom_extensions[] = {
	".zip", ".7z", ".bin", ".cue", ".iso", ".img", ".pbp",
	".nes", ".sfc", ".smc", ".gba", ".gbc", ".gb", ".nds",
	".n64", ".z64", ".v64", ".gen", ".md", ".sms", ".gg",
	".pce", ".ngp", ".ngc", ".ws", ".wsc", ".lnx",
	".a26", ".a52", ".a78", ".col", ".rom", ".mx1", ".mx2",
	".cso", ".chd", ".fds", ".dsk", ".tap", ".tzx",
	".d64", ".t64", ".prg",
	NULL};

// ============================================
// User Credentials
// ============================================

static char cred_username[64] = "";
static char cred_password[128] = "";

static void loadCredentials(void) {
	cred_username[0] = '\0';
	cred_password[0] = '\0';
	getFile(creds_user_path(), cred_username, sizeof(cred_username));
	getFile(creds_pass_path(), cred_password, sizeof(cred_password));
	// Trim newlines
	char* nl;
	if ((nl = strchr(cred_username, '\n')))
		*nl = '\0';
	if ((nl = strchr(cred_password, '\n')))
		*nl = '\0';
	ScraperAPI_setUserCredentials(cred_username, cred_password);
}

static void saveCredentials(void) {
	mkdir_p(creds_dir());
	putFile(creds_user_path(), cred_username);
	putFile(creds_pass_path(), cred_password);
	ScraperAPI_setUserCredentials(cred_username, cred_password);
}

// ============================================
// Data Structures
// ============================================

typedef struct {
	char name[256];	   // Display name (e.g. "Game Boy Advance")
	char tag[64];	   // Tag extracted from folder (e.g. "GBA")
	char path[512];	   // Full path to ROM directory
	int system_id;	   // ScreenScraper system ID
	int rom_count;	   // Total ROMs
	int scraped_count; // ROMs with existing artwork
} SystemEntry;

typedef struct {
	char filename[256]; // ROM filename
	char path[512];		// Full path to ROM file
	bool has_artwork;	// Whether .media/<name>.png exists
} ROMEntry;

typedef enum {
	SCREEN_MAIN_MENU,
	SCREEN_SYSTEMS,
	SCREEN_ROMS,
	SCREEN_PROGRESS,
	SCREEN_SETTINGS,
} ScreenState;

typedef enum {
	SCRAPE_STATUS_IDLE,
	SCRAPE_STATUS_SEARCHING,
	SCRAPE_STATUS_DOWNLOADING,
	SCRAPE_STATUS_COMPOSITING,
	SCRAPE_STATUS_DONE,
	SCRAPE_STATUS_NOT_FOUND,
	SCRAPE_STATUS_ERROR,
} ScrapeStatus;

// ============================================
// Scrape Queue
// ============================================

typedef struct {
	char filename[256];
	char rom_path[512];
	char system_path[512];
	char system_name[128];
	int system_id;
	volatile ScrapeStatus status;
} ScrapeQueueItem;

static ScrapeQueueItem scrape_queue[MAX_QUEUE];
static volatile int queue_count = 0;
static volatile int queue_current = -1;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t scraper_thread;
static volatile bool scraper_running = false;
static volatile bool scraper_thread_started = false;
static volatile bool queue_dirty = false;

// ============================================
// Global State
// ============================================

static SDL_Surface* screen;
static ScreenState current_screen = SCREEN_MAIN_MENU;

static SystemEntry systems[MAX_SYSTEMS];
static int system_count = 0;
static ListView systems_view;
static char systems_badge_buf[64];

static ROMEntry roms[MAX_ROMS];
static int rom_count = 0;
static int rom_selected = 0;
static int rom_scroll = 0;

static int progress_selected = 0;
static int progress_scroll = 0;

static int settings_selected = 0;
static int settings_scroll = 0;

// Settings screen user info cache
static ScraperUserInfo cached_user_info = {0};
static bool user_info_fetched = false;

// ============================================
// ROM Extension Check
// ============================================

static bool isRomFile(const char* filename) {
	if (!filename)
		return false;
	if (filename[0] == '.')
		return false;

	const char* ext = strrchr(filename, '.');
	if (!ext)
		return false;

	for (int i = 0; rom_extensions[i] != NULL; i++) {
		if (strcasecmp(ext, rom_extensions[i]) == 0)
			return true;
	}
	return false;
}

// ============================================
// System Scanner
// ============================================

static void extractTag(const char* dirname, char* tag_out, int tag_size) {
	tag_out[0] = '\0';
	const char* open = strrchr(dirname, '(');
	const char* close = strrchr(dirname, ')');
	if (open && close && close > open) {
		int len = (int)(close - open - 1);
		if (len > 0 && len < tag_size) {
			strncpy(tag_out, open + 1, len);
			tag_out[len] = '\0';
		}
	}
}

static void extractDisplayName(const char* dirname, char* name_out, int name_size) {
	const char* open = strrchr(dirname, '(');
	if (open && open > dirname) {
		int len = (int)(open - dirname);
		while (len > 0 && dirname[len - 1] == ' ')
			len--;
		if (len > 0 && len < name_size) {
			strncpy(name_out, dirname, len);
			name_out[len] = '\0';
			return;
		}
	}
	snprintf(name_out, name_size, "%s", dirname);
}

static int countRomsInDir(const char* dirpath) {
	int count = 0;
	DIR* dir = opendir(dirpath);
	if (!dir)
		return 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (isRomFile(entry->d_name))
			count++;
	}
	closedir(dir);
	return count;
}

static int countScrapedInDir(const char* dirpath) {
	int count = 0;
	char media_path[512];
	snprintf(media_path, sizeof(media_path), "%s/.media", dirpath);

	DIR* dir = opendir(dirpath);
	if (!dir)
		return 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (!isRomFile(entry->d_name))
			continue;

		char* base = removeExtension(entry->d_name);
		if (base) {
			char png_path[512];
			snprintf(png_path, sizeof(png_path), "%s/%s.png", media_path, base);
			if (exists(png_path))
				count++;
			free(base);
		}
	}
	closedir(dir);
	return count;
}

static int systemCompare(const void* a, const void* b) {
	return strcasecmp(((const SystemEntry*)a)->name, ((const SystemEntry*)b)->name);
}

static void scanSystems(void) {
	system_count = 0;
	DIR* dir = opendir(ROMS_PATH);
	if (!dir)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL && system_count < MAX_SYSTEMS) {
		if (entry->d_name[0] == '.')
			continue;
		if (entry->d_type != DT_DIR)
			continue;

		char tag[64];
		extractTag(entry->d_name, tag, sizeof(tag));
		if (tag[0] == '\0')
			continue;

		int sid = ScraperSystems_getId(tag);
		if (sid < 0)
			continue;

		SystemEntry* sys = &systems[system_count];
		extractDisplayName(entry->d_name, sys->name, sizeof(sys->name));
		snprintf(sys->tag, sizeof(sys->tag), "%s", tag);
		snprintf(sys->path, sizeof(sys->path), "%s/%s", ROMS_PATH, entry->d_name);
		sys->system_id = sid;
		sys->rom_count = countRomsInDir(sys->path);
		sys->scraped_count = countScrapedInDir(sys->path);

		if (sys->rom_count > 0)
			system_count++;
	}
	closedir(dir);

	qsort(systems, system_count, sizeof(SystemEntry), systemCompare);
}

// ============================================
// ROM Scanner
// ============================================

static int romCompare(const void* a, const void* b) {
	return strcasecmp(((const ROMEntry*)a)->filename, ((const ROMEntry*)b)->filename);
}

static void scanROMs(SystemEntry* sys) {
	rom_count = 0;
	rom_selected = 0;
	rom_scroll = 0;

	DIR* dir = opendir(sys->path);
	if (!dir)
		return;

	char media_path[512];
	snprintf(media_path, sizeof(media_path), "%s/.media", sys->path);

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL && rom_count < MAX_ROMS) {
		if (!isRomFile(entry->d_name))
			continue;

		ROMEntry* rom = &roms[rom_count];
		snprintf(rom->filename, sizeof(rom->filename), "%s", entry->d_name);
		snprintf(rom->path, sizeof(rom->path), "%s/%s", sys->path, entry->d_name);

		char* base = removeExtension(entry->d_name);
		if (base) {
			char png_path[512];
			snprintf(png_path, sizeof(png_path), "%s/%s.png", media_path, base);
			rom->has_artwork = exists(png_path);
			free(base);
		} else {
			rom->has_artwork = false;
		}

		rom_count++;
	}
	closedir(dir);

	qsort(roms, rom_count, sizeof(ROMEntry), romCompare);
}

// ============================================
// Queue Operations
// ============================================

static bool isROMQueued(const char* rom_path) {
	pthread_mutex_lock(&queue_mutex);
	for (int i = 0; i < queue_count; i++) {
		if (strcmp(scrape_queue[i].rom_path, rom_path) == 0) {
			pthread_mutex_unlock(&queue_mutex);
			return true;
		}
	}
	pthread_mutex_unlock(&queue_mutex);
	return false;
}

static ScrapeStatus getROMQueueStatus(const char* rom_path) {
	pthread_mutex_lock(&queue_mutex);
	for (int i = 0; i < queue_count; i++) {
		if (strcmp(scrape_queue[i].rom_path, rom_path) == 0) {
			ScrapeStatus s = scrape_queue[i].status;
			pthread_mutex_unlock(&queue_mutex);
			return s;
		}
	}
	pthread_mutex_unlock(&queue_mutex);
	return SCRAPE_STATUS_IDLE;
}

static void* scraper_thread_func(void* arg);

static void ensureThreadStarted(void) {
	if (!scraper_thread_started) {
		scraper_running = true;
		scraper_thread_started = true;
		pthread_create(&scraper_thread, NULL, scraper_thread_func, NULL);
	}
}

static bool queueAddROM(ROMEntry* rom, SystemEntry* sys, bool force) {
	if (!force && rom->has_artwork)
		return false;
	if (isROMQueued(rom->path))
		return false;

	pthread_mutex_lock(&queue_mutex);
	if (queue_count >= MAX_QUEUE) {
		pthread_mutex_unlock(&queue_mutex);
		return false;
	}

	ScrapeQueueItem* item = &scrape_queue[queue_count];
	snprintf(item->filename, sizeof(item->filename), "%s", rom->filename);
	snprintf(item->rom_path, sizeof(item->rom_path), "%s", rom->path);
	snprintf(item->system_path, sizeof(item->system_path), "%s", sys->path);
	snprintf(item->system_name, sizeof(item->system_name), "%s", sys->name);
	item->system_id = sys->system_id;
	item->status = SCRAPE_STATUS_IDLE;
	queue_count++;
	pthread_mutex_unlock(&queue_mutex);

	ensureThreadStarted();
	return true;
}

static int queueAddAllROMs(SystemEntry* sys) {
	int added = 0;
	for (int i = 0; i < rom_count; i++) {
		if (queueAddROM(&roms[i], sys, false))
			added++;
	}
	return added;
}

static int queueAddAllSystems(void) {
	int added = 0;
	// Save/restore current ROM list since scanROMs overwrites globals
	int saved_rom_count = rom_count;
	int saved_rom_selected = rom_selected;
	int saved_rom_scroll = rom_scroll;

	for (int s = 0; s < system_count; s++) {
		scanROMs(&systems[s]);
		for (int r = 0; r < rom_count; r++) {
			if (queueAddROM(&roms[r], &systems[s], false))
				added++;
		}
	}

	// Restore ROM list for current system if we were viewing one
	if (current_screen == SCREEN_ROMS) {
		scanROMs(&systems[systems_view.selected]);
		rom_selected = saved_rom_selected;
		rom_scroll = saved_rom_scroll;
	} else {
		rom_count = saved_rom_count;
		rom_selected = saved_rom_selected;
		rom_scroll = saved_rom_scroll;
	}

	return added;
}

static void queueGetStats(int* done, int* total, int* failed) {
	int d = 0, f = 0;
	pthread_mutex_lock(&queue_mutex);
	for (int i = 0; i < queue_count; i++) {
		ScrapeStatus s = scrape_queue[i].status;
		if (s == SCRAPE_STATUS_DONE)
			d++;
		else if (s == SCRAPE_STATUS_NOT_FOUND || s == SCRAPE_STATUS_ERROR)
			f++;
	}
	if (done)
		*done = d;
	if (total)
		*total = queue_count;
	if (failed)
		*failed = f;
	pthread_mutex_unlock(&queue_mutex);
}

static bool isTerminalStatus(ScrapeStatus s) {
	return s == SCRAPE_STATUS_DONE ||
		   s == SCRAPE_STATUS_NOT_FOUND || s == SCRAPE_STATUS_ERROR;
}

static void queueClearDone(void) {
	pthread_mutex_lock(&queue_mutex);
	int cur = queue_current;
	int write_idx = 0;
	int new_current = -1;
	for (int i = 0; i < queue_count; i++) {
		ScrapeStatus s = scrape_queue[i].status;
		// Keep items that are in-progress or idle, skip terminal items
		// Also always keep the currently-processing item to avoid pointer invalidation
		if (isTerminalStatus(s) && i != cur) {
			continue;
		}
		if (i == cur)
			new_current = write_idx;
		if (write_idx != i)
			scrape_queue[write_idx] = scrape_queue[i];
		write_idx++;
	}
	queue_count = write_idx;
	queue_current = new_current;
	// Clamp progress selection and scroll
	if (progress_selected >= queue_count)
		progress_selected = queue_count > 0 ? queue_count - 1 : 0;
	if (progress_scroll >= queue_count)
		progress_scroll = queue_count > 0 ? queue_count - 1 : 0;
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
}

// ============================================
// Background Scraper Thread
// ============================================

static void scrape_status_cb(const char* stage, void* userdata) {
	ScrapeQueueItem* item = (ScrapeQueueItem*)userdata;
	ScrapeStatus s = SCRAPE_STATUS_SEARCHING;
	if (strcmp(stage, "downloading") == 0)
		s = SCRAPE_STATUS_DOWNLOADING;
	else if (strcmp(stage, "compositing") == 0)
		s = SCRAPE_STATUS_COMPOSITING;
	pthread_mutex_lock(&queue_mutex);
	item->status = s;
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
}

static void scrapeOneQueueItem(ScrapeQueueItem* item) {
	char rom_abs[MAX_PATH];
	snprintf(rom_abs, sizeof(rom_abs), "%s/%s", item->system_path, item->filename);
	char out_path[512];
	ROM_mediaArtPath(rom_abs, out_path, sizeof(out_path));

	ScrapeResult r = scrapeOne(item->filename, item->rom_path, item->system_id,
							   out_path, scrape_status_cb, item);

	pthread_mutex_lock(&queue_mutex);
	item->status = (r == SCRAPE_RESULT_OK)		   ? SCRAPE_STATUS_DONE
				   : (r == SCRAPE_RESULT_NOTFOUND) ? SCRAPE_STATUS_NOT_FOUND
												   : SCRAPE_STATUS_ERROR;
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
}

static void* scraper_thread_func(void* arg) {
	(void)arg;
	while (scraper_running) {
		// Find next IDLE item in queue
		pthread_mutex_lock(&queue_mutex);
		int next = -1;
		for (int i = 0; i < queue_count; i++) {
			if (scrape_queue[i].status == SCRAPE_STATUS_IDLE) {
				next = i;
				queue_current = i;
				break;
			}
		}
		if (next < 0)
			queue_current = -1;
		pthread_mutex_unlock(&queue_mutex);

		if (next < 0) {
			usleep(200000); // 200ms idle polling
			continue;
		}

		scrapeOneQueueItem(&scrape_queue[next]);
	}
	return NULL;
}

// ============================================
// Status Text Helpers
// ============================================

static const char* scrapeStatusText(ScrapeStatus status) {
	switch (status) {
	case SCRAPE_STATUS_IDLE:
		return "Queued";
	case SCRAPE_STATUS_SEARCHING:
		return "Searching...";
	case SCRAPE_STATUS_DOWNLOADING:
		return "Downloading...";
	case SCRAPE_STATUS_COMPOSITING:
		return "Compositing...";
	case SCRAPE_STATUS_DONE:
		return "Done";
	case SCRAPE_STATUS_NOT_FOUND:
		return "Not Found";
	case SCRAPE_STATUS_ERROR:
		return "Error";
	}
	return "";
}

static const char* romStatusLabel(ROMEntry* rom) {
	if (rom->has_artwork)
		return "Done";
	if (!isROMQueued(rom->path))
		return NULL;
	// A queued ROM always has one of the ScrapeStatus values that
	// scrapeStatusText() maps to a label, so reuse that mapping.
	return scrapeStatusText(getROMQueueStatus(rom->path));
}

// ============================================
// SCREEN_MAIN_MENU
// ============================================

static ListView main_menu_view;
static const char* main_menu_items[] = {"Library", "Progress", "Settings"};
static char progress_badge_buf[32];

static void main_menu_get_row(void* ctx, int i, bool selected,
							  ListViewRow* out) {
	(void)ctx;
	(void)selected;
	out->label = main_menu_items[i];
	if (i == 1) {
		int done, total, failed;
		queueGetStats(&done, &total, &failed);
		if (total > 0) {
			snprintf(progress_badge_buf, sizeof(progress_badge_buf), "%d/%d",
					 done, total);
			out->annotation = progress_badge_buf;
		}
	}
}

static void renderMainMenu(void) {
	GFX_clear(screen);
	ListView* v = &main_menu_view;
	v->title = "Artwork Manager";
	v->font = font.large;
	v->count = 3;
	v->get_row = main_menu_get_row;
	v->ctx = NULL;
	v->list_id = (const void*)main_menu_items;
	v->hint_pairs = (char*[]){"B", "EXIT", "A", "OPEN", NULL};
	UI_listViewRender(v, screen);
	GFX_flip(screen);
}

// ============================================
// SCREEN_SYSTEMS (full-mode ListView; in-pill %d/%d badge)
// ============================================

static void systems_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	(void)ctx;
	(void)selected;
	SystemEntry* sys = &systems[i];
	out->label = sys->name;
	snprintf(systems_badge_buf, sizeof(systems_badge_buf), "%d/%d",
			 sys->scraped_count, sys->rom_count);
	out->badge = systems_badge_buf;
}

static void renderSystemList(void) {
	GFX_clear(screen);
	// Function-scope arrays: hint_pairs must outlive the UI_listViewRender
	// call below, so a branch-scoped compound literal would be UB (see
	// ui_listview.h).
	char* hints_full[] = {"B", "BACK", "Y", "QUEUE ALL", "A", "OPEN", NULL};
	char* hints_empty[] = {"B", "BACK", NULL};
	ListView* v = &systems_view;
	v->title = "Artwork Manager | Library";
	v->font = font.large;
	v->count = system_count;
	v->get_row = systems_get_row;
	v->ctx = NULL;
	v->list_id = (const void*)systems;
	v->empty_title = "No supported systems found";
	v->empty_subtitle = "Add ROMs to your SD card";
	if (system_count > 0)
		v->hint_pairs = hints_full;
	else
		v->hint_pairs = hints_empty;
	UI_listViewRender(v, screen);
	GFX_flip(screen);
}

// ============================================
// SCREEN_ROMS (settings-style rows)
// ============================================

static void renderROMList(void) {
	GFX_clear(screen);

	SystemEntry* sys = &systems[systems_view.selected];
	UI_renderMenuBar(screen, sys->name);

	if (rom_count == 0) {
		UI_renderEmptyState(screen, "No ROMs found", NULL, NULL);
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
		GFX_flip(screen);
		return;
	}

	ListLayout layout = UI_calcListLayout(screen);

	// Use static arrays to avoid VLA stack overflow with large ROM lists
	static UISettingsItem items[MAX_ROMS];
	static char clean_names[MAX_ROMS][256];

	for (int i = 0; i < rom_count; i++) {
		ROMEntry* rom = &roms[i];

		char* base = removeExtension(rom->filename);
		snprintf(clean_names[i], sizeof(clean_names[i]), "%s", base ? base : rom->filename);
		if (base)
			free(base);

		items[i] = (UISettingsItem){
			.label = clean_names[i],
			.value = romStatusLabel(rom),
			.swatch = -1,
			.cycleable = 0,
			.desc = NULL,
		};
	}

	UI_renderSettingsPage(screen, &layout, items, rom_count, rom_selected, &rom_scroll, NULL);
	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "Y", "QUEUE ALL", "A", "QUEUE", NULL});
	GFX_flip(screen);
}

// ============================================
// SCREEN_PROGRESS
// ============================================

static void renderProgress(void) {
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Artwork Manager | Progress");

	pthread_mutex_lock(&queue_mutex);
	int count = queue_count;

	if (count == 0) {
		pthread_mutex_unlock(&queue_mutex);
		UI_renderEmptyState(screen, "Queue is empty",
							"Add ROMs from Library", NULL);
		GFX_flip(screen);
		return;
	}

	ListLayout layout = UI_calcListLayout(screen);

	// Use static arrays to avoid VLA stack overflow with large queues
	static UISettingsItem items[MAX_QUEUE];
	static char filenames[MAX_QUEUE][256];
	static char sys_names[MAX_QUEUE][128];
	static const char* status_texts[MAX_QUEUE];

	for (int i = 0; i < count; i++) {
		char* base = removeExtension(scrape_queue[i].filename);
		snprintf(filenames[i], sizeof(filenames[i]), "%s",
				 base ? base : scrape_queue[i].filename);
		if (base)
			free(base);

		snprintf(sys_names[i], sizeof(sys_names[i]), "%s", scrape_queue[i].system_name);
		status_texts[i] = scrapeStatusText(scrape_queue[i].status);

		items[i] = (UISettingsItem){
			.label = filenames[i],
			.value = status_texts[i],
			.swatch = -1,
			.cycleable = 0,
			.desc = sys_names[i],
		};
	}

	// Build status message
	int done = 0, failed = 0;
	for (int i = 0; i < count; i++) {
		ScrapeStatus s = scrape_queue[i].status;
		if (s == SCRAPE_STATUS_DONE)
			done++;
		else if (s == SCRAPE_STATUS_NOT_FOUND || s == SCRAPE_STATUS_ERROR)
			failed++;
	}
	pthread_mutex_unlock(&queue_mutex);

	char status_msg[128];
	if (failed > 0)
		snprintf(status_msg, sizeof(status_msg), "%d / %d complete, %d failed", done, count, failed);
	else
		snprintf(status_msg, sizeof(status_msg), "%d / %d complete", done, count);

	UI_renderSettingsPage(screen, &layout, items, count,
						  progress_selected, &progress_scroll, status_msg);
	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "X", "CLEAR DONE", NULL});
	GFX_flip(screen);
}

// ============================================
// SCREEN_SETTINGS
// ============================================

static void renderSettings(void) {
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Artwork Manager | Settings");

	ListLayout layout = UI_calcListLayout(screen);

	bool logged_in = cred_username[0] && cred_password[0];

	const char* user_display = cred_username[0] ? cred_username : "Not set";
	const char* pass_display = cred_password[0] ? "********" : "Not set";

	if (logged_in) {
		char quota_str[32] = "—";
		char max_str[32] = "—";
		if (user_info_fetched && cached_user_info.valid) {
			snprintf(quota_str, sizeof(quota_str), "%d", cached_user_info.requests_today);
			snprintf(max_str, sizeof(max_str), "%d", cached_user_info.max_requests_per_day);
		}

		UISettingsItem items[] = {
			{.label = "Username", .value = user_display, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper username"},
			{.label = "Password", .value = pass_display, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper password"},
			{.label = "Requests Today", .value = quota_str, .swatch = -1, .cycleable = 0, .desc = "API requests used today"},
			{.label = "Max Requests", .value = max_str, .swatch = -1, .cycleable = 0, .desc = "Daily request limit"},
			{.label = "Logout", .value = NULL, .swatch = -1, .cycleable = 0, .desc = "Clear saved credentials"},
		};

		int count = sizeof(items) / sizeof(items[0]);
		UI_renderSettingsPage(screen, &layout, items, count,
							  settings_selected, &settings_scroll, NULL);
	} else {
		UISettingsItem items[] = {
			{.label = "Username", .value = user_display, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper username"},
			{.label = "Password", .value = pass_display, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper password"},
		};

		int count = sizeof(items) / sizeof(items[0]);
		UI_renderSettingsPage(screen, &layout, items, count,
							  settings_selected, &settings_scroll,
							  "Log in for higher rate limits");
	}

	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "SELECT", NULL});
	GFX_flip(screen);
}

// ============================================
// Action Helpers
// ============================================

// Fetch and cache ScreenScraper account info when online.
static void fetchUserInfoIfOnline(void) {
	if (ScraperAPI_isOnline()) {
		UI_renderLoadingOverlay(screen, "Loading", "Fetching account info...");
		GFX_flip(screen);
		cached_user_info = ScraperAPI_fetchUserInfo();
		user_info_fetched = true;
	}
}

// Prompt for a credential field via the on-screen keyboard, persist it,
// and refresh account info if both credentials are now set.
static void editCredentialField(const char* prompt, char* field, size_t field_size) {
	UIKeyboard_init();
	char* input = UIKeyboard_open(prompt);
	PAD_poll();
	PAD_reset();
	if (input) {
		snprintf(field, field_size, "%s", input);
		free(input);
		saveCredentials();
		user_info_fetched = false;
	}
	// Auto-fetch user info if both credentials are now set
	if (cred_username[0] && cred_password[0] && !user_info_fetched) {
		fetchUserInfoIfOnline();
	}
}

// Show the "no network" overlay for a fixed dwell.
static void showNoNetworkOverlay(void) {
	UI_renderLoadingOverlay(screen, "No Network", "Connect to WiFi first");
	GFX_flip(screen);
	SDL_Delay(1500);
}

// Report how many ROMs were queued (or that nothing needed queuing).
static void reportQueued(int added) {
	if (added > 0) {
		char msg[64];
		snprintf(msg, sizeof(msg), "Queued %d ROMs", added);
		UI_renderLoadingOverlay(screen, "Queued", msg);
	} else {
		UI_renderLoadingOverlay(screen, "Nothing to queue",
								"All ROMs already queued or scraped");
	}
	GFX_flip(screen);
	SDL_Delay(1000);
}

// ============================================
// Main
// ============================================

int main(int argc, char* argv[]) {
	PATHS_init(PLATFORM);

	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], "--fetch") == 0)
			return run_headless_fetch(argc, argv);

	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, "Artwork Manager");

	InitSettings();
	PAD_init();
	PWR_init();
	setup_signal_handlers();
	ScraperAPI_init();
	loadCredentials();

	mkdir_p(TMP_DIR);
	scanSystems();

	// Cold-start the main-menu ListView; selection persists across screen
	// changes thereafter (static view), matching the old module-static index.
	UI_listViewReset(&main_menu_view, 3, main_menu_items);

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();
		// MENU + SELECT exits (same combo as in-game and the other pak tools).
		bool req_quit = false;
		UI_handleQuitRequest(screen, &req_quit, &dirty, "Exit Artwork Manager?", NULL);
		if (req_quit)
			app_quit = true;

		// Check if background thread made progress
		if (queue_dirty) {
			queue_dirty = false;
			dirty = true;
		}

		switch (current_screen) {
		case SCREEN_MAIN_MENU: {
			ListViewAction act = UI_listViewHandleInput(&main_menu_view);
			if (act.type == LISTVIEW_BACK) {
				app_quit = true;
				break;
			}
			if (act.type == LISTVIEW_ACTIVATED) {
				switch (act.index) {
				case 0: // Library
					current_screen = SCREEN_SYSTEMS;
					UI_listViewReset(&systems_view, system_count, systems);
					dirty = true;
					break;
				case 1: // Progress
					current_screen = SCREEN_PROGRESS;
					progress_selected = 0;
					progress_scroll = 0;
					dirty = true;
					break;
				case 2: // Settings
					current_screen = SCREEN_SETTINGS;
					settings_selected = 0;
					settings_scroll = 0;
					// Fetch user info if credentials are set
					if (ScraperAPI_hasUserCredentials() && !user_info_fetched) {
						fetchUserInfoIfOnline();
					}
					dirty = true;
					break;
				}
				break;
			}

			break;
		}
		case SCREEN_SYSTEMS: {
			ListViewAction act = UI_listViewHandleInput(&systems_view);
			if (act.type == LISTVIEW_BACK) {
				// Long system names marquee on LAYER_SCROLLTEXT; the band
				// persists across screens unless cleared on the way out.
				GFX_clearLayers(LAYER_SCROLLTEXT);
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}
			if (act.type == LISTVIEW_ACTIVATED) {
				GFX_clearLayers(LAYER_SCROLLTEXT);
				scanROMs(&systems[act.index]);
				current_screen = SCREEN_ROMS;
				dirty = true;
				break;
			}
			if (act.type == LISTVIEW_BUTTON && act.btn == BTN_Y &&
				system_count > 0 && act.index >= 0) {
				if (!ScraperAPI_isOnline()) {
					showNoNetworkOverlay();
					dirty = true;
					break;
				}
				reportQueued(queueAddAllSystems());
				dirty = true;
				break;
			}

			break;
		}
		case SCREEN_ROMS: {
			if (PAD_justPressed(BTN_B)) {
				// Refresh system counts before going back
				systems[systems_view.selected].scraped_count =
					countScrapedInDir(systems[systems_view.selected].path);
				current_screen = SCREEN_SYSTEMS;
				dirty = true;
				break;
			}

			if (PAD_navigateMenu(&rom_selected, rom_count))
				dirty = true;

			if (PAD_justPressed(BTN_A) && rom_count > 0) {
				if (!ScraperAPI_isOnline()) {
					showNoNetworkOverlay();
					dirty = true;
					break;
				}
				ROMEntry* rom = &roms[rom_selected];
				if (isROMQueued(rom->path)) {
					UI_renderLoadingOverlay(screen, "Already queued",
											"ROM is already in the queue");
					GFX_flip(screen);
					SDL_Delay(800);
				} else {
					queueAddROM(rom, &systems[systems_view.selected], true);
					UI_renderLoadingOverlay(screen, "Queued",
											rom->filename);
					GFX_flip(screen);
					SDL_Delay(500);
				}
				dirty = true;
				break;
			}

			if (PAD_justPressed(BTN_Y) && rom_count > 0) {
				if (!ScraperAPI_isOnline()) {
					showNoNetworkOverlay();
					dirty = true;
					break;
				}
				reportQueued(queueAddAllROMs(&systems[systems_view.selected]));
				dirty = true;
				break;
			}

			break;
		}
		case SCREEN_PROGRESS: {
			if (PAD_justPressed(BTN_B)) {
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}

			int count = queue_count;
			if (count > 0 && PAD_navigateMenu(&progress_selected, count))
				dirty = true;

			if (PAD_justPressed(BTN_X) && count > 0) {
				queueClearDone();
				dirty = true;
				break;
			}

			break;
		}
		case SCREEN_SETTINGS: {
			bool logged_in = cred_username[0] && cred_password[0];
			int settings_count = logged_in ? 5 : 2;

			if (PAD_justPressed(BTN_B)) {
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}

			if (PAD_navigateMenu(&settings_selected, settings_count))
				dirty = true;

			if (PAD_justPressed(BTN_A)) {
				switch (settings_selected) {
				case 0: { // Username
					editCredentialField("ScreenScraper Username",
										cred_username, sizeof(cred_username));
					dirty = true;
					break;
				}
				case 1: { // Password
					editCredentialField("ScreenScraper Password",
										cred_password, sizeof(cred_password));
					dirty = true;
					break;
				}
				case 4: { // Logout (only reachable when logged in)
					cred_username[0] = '\0';
					cred_password[0] = '\0';
					saveCredentials();
					user_info_fetched = false;
					cached_user_info = (ScraperUserInfo){0};
					settings_selected = 0;
					settings_scroll = 0;
					dirty = true;
					break;
				}
				}
				break;
			}

			break;
		}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (UI_statusBarChanged())
			dirty = true;
		// Selection-pill glides advance frame-by-frame, so keep redrawing
		// while the active screen's ListView pill is still travelling.
		if ((current_screen == SCREEN_MAIN_MENU &&
			 UI_listViewBusy(&main_menu_view)) ||
			(current_screen == SCREEN_SYSTEMS &&
			 UI_listViewBusy(&systems_view)))
			dirty = true;

		if (dirty) {
			switch (current_screen) {
			case SCREEN_MAIN_MENU:
				renderMainMenu();
				break;
			case SCREEN_SYSTEMS:
				renderSystemList();
				break;
			case SCREEN_ROMS:
				renderROMList();
				break;
			case SCREEN_PROGRESS:
				renderProgress();
				break;
			case SCREEN_SETTINGS:
				renderSettings();
				break;
			}
			dirty = false;
		} else {
			if (current_screen == SCREEN_MAIN_MENU)
				UI_listViewTickIdle(&main_menu_view);
			else if (current_screen == SCREEN_SYSTEMS)
				UI_listViewTickIdle(&systems_view);
			GFX_sync();
		}
	}

	// Cleanup: stop background thread
	if (scraper_thread_started) {
		scraper_running = false;
		pthread_join(scraper_thread, NULL);
	}

	// Remove temp directory
	system("rm -rf " TMP_DIR);

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
