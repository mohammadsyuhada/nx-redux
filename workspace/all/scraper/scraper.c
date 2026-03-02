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
#include "ui_components.h"
#include "ui_list.h"
#include "utils.h"

#include "scraper_api.h"
#include "scraper_compositor.h"
#include "scraper_systems.h"
#include "ui_keyboard.h"

// ============================================
// Constants
// ============================================

#define MAX_SYSTEMS 128
#define MAX_ROMS 4096
#define MAX_QUEUE 2048
#define TMP_DIR "/tmp/scraper"
#define CREDS_DIR SHARED_USERDATA_PATH "/.scraper"
#define CREDS_USER CREDS_DIR "/ss_user.txt"
#define CREDS_PASS CREDS_DIR "/ss_pass.txt"

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
	getFile(CREDS_USER, cred_username, sizeof(cred_username));
	getFile(CREDS_PASS, cred_password, sizeof(cred_password));
	// Trim newlines
	char* nl;
	if ((nl = strchr(cred_username, '\n')))
		*nl = '\0';
	if ((nl = strchr(cred_password, '\n')))
		*nl = '\0';
	ScraperAPI_setUserCredentials(cred_username, cred_password);
}

static void saveCredentials(void) {
	mkdir_p(CREDS_DIR);
	putFile(CREDS_USER, cred_username);
	putFile(CREDS_PASS, cred_password);
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
	SCRAPE_STATUS_SKIPPED,
	SCRAPE_STATUS_CANCELLED,
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
static int system_selected = 0;
static int system_scroll = 0;

static ROMEntry roms[MAX_ROMS];
static int rom_count = 0;
static int rom_selected = 0;
static int rom_scroll = 0;

static int menu_selected = 0;

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

static bool queueAddROM(ROMEntry* rom, SystemEntry* sys) {
	if (rom->has_artwork)
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
		if (queueAddROM(&roms[i], sys))
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
			if (queueAddROM(&roms[r], &systems[s]))
				added++;
		}
	}

	// Restore ROM list for current system if we were viewing one
	if (current_screen == SCREEN_ROMS) {
		scanROMs(&systems[system_selected]);
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
		if (s == SCRAPE_STATUS_DONE || s == SCRAPE_STATUS_SKIPPED)
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
	return s == SCRAPE_STATUS_DONE || s == SCRAPE_STATUS_SKIPPED ||
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

static void scrapeOneQueueItem(ScrapeQueueItem* item) {
	// Create temp directory
	mkdir_p(TMP_DIR);

	// Search ScreenScraper API
	pthread_mutex_lock(&queue_mutex);
	item->status = SCRAPE_STATUS_SEARCHING;
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);

	ScraperGameInfo info;
	bool found = ScraperAPI_search(item->filename, item->system_id, &info);

	if (!found) {
		pthread_mutex_lock(&queue_mutex);
		item->status = SCRAPE_STATUS_NOT_FOUND;
		queue_dirty = true;
		pthread_mutex_unlock(&queue_mutex);
		return;
	}

	// Download images
	pthread_mutex_lock(&queue_mutex);
	item->status = SCRAPE_STATUS_DOWNLOADING;
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);

	char ss_path[512] = "", box_path[512] = "", wheel_path_tmp[512] = "";
	bool has_ss = false, has_box = false, has_wheel = false;

	if (info.screenshot_url[0] != '\0') {
		snprintf(ss_path, sizeof(ss_path), "%s/screenshot.png", TMP_DIR);
		has_ss = ScraperAPI_downloadFile(info.screenshot_url, ss_path);
	}
	if (info.boxart_url[0] != '\0') {
		snprintf(box_path, sizeof(box_path), "%s/boxart.png", TMP_DIR);
		has_box = ScraperAPI_downloadFile(info.boxart_url, box_path);
	}
	if (info.wheel_url[0] != '\0') {
		snprintf(wheel_path_tmp, sizeof(wheel_path_tmp), "%s/wheel.png", TMP_DIR);
		has_wheel = ScraperAPI_downloadFile(info.wheel_url, wheel_path_tmp);
	}

	if (!has_ss && !has_box && !has_wheel) {
		pthread_mutex_lock(&queue_mutex);
		item->status = SCRAPE_STATUS_NOT_FOUND;
		queue_dirty = true;
		pthread_mutex_unlock(&queue_mutex);
		return;
	}

	// Composite images
	pthread_mutex_lock(&queue_mutex);
	item->status = SCRAPE_STATUS_COMPOSITING;
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);

	SDL_Surface* artwork = Compositor_create(
		has_ss ? ss_path : NULL,
		has_box ? box_path : NULL,
		has_wheel ? wheel_path_tmp : NULL);

	// Clean up temp files
	if (has_ss)
		remove(ss_path);
	if (has_box)
		remove(box_path);
	if (has_wheel)
		remove(wheel_path_tmp);

	if (!artwork) {
		pthread_mutex_lock(&queue_mutex);
		item->status = SCRAPE_STATUS_ERROR;
		queue_dirty = true;
		pthread_mutex_unlock(&queue_mutex);
		return;
	}

	// Save to .media directory
	char* base = removeExtension(item->filename);
	char out_path[512];
	snprintf(out_path, sizeof(out_path), "%s/.media/%s.png",
			 item->system_path, base ? base : item->filename);
	if (base)
		free(base);

	// Ensure .media directory exists
	char media_dir[512];
	snprintf(media_dir, sizeof(media_dir), "%s/.media", item->system_path);
	mkdir_p(media_dir);

	bool saved = Compositor_savePNG(artwork, out_path);
	SDL_FreeSurface(artwork);

	pthread_mutex_lock(&queue_mutex);
	item->status = saved ? SCRAPE_STATUS_DONE : SCRAPE_STATUS_ERROR;
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
	case SCRAPE_STATUS_SKIPPED:
		return "Skipped";
	case SCRAPE_STATUS_CANCELLED:
		return "Cancelled";
	}
	return "";
}

static const char* romStatusLabel(ROMEntry* rom) {
	if (rom->has_artwork)
		return "Done";
	ScrapeStatus qs = getROMQueueStatus(rom->path);
	switch (qs) {
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
	default:
		break;
	}
	// Not queued and no artwork
	return NULL;
}

// ============================================
// SCREEN_MAIN_MENU
// ============================================

static void renderMainMenuBadge(SDL_Surface* dst, int index, bool selected,
								int item_y, int item_h) {
	if (index != 1)
		return; // Only badge on "Progress"

	int done, total, failed;
	queueGetStats(&done, &total, &failed);
	if (total == 0)
		return;

	char badge[32];
	snprintf(badge, sizeof(badge), "%d/%d", done, total);

	int tw = 0, th = 0;
	TTF_SizeUTF8(font.tiny, badge, &tw, &th);

	SDL_Color color = selected ? COLOR_WHITE : COLOR_GRAY;
	int x = dst->w - SCALE1(PADDING) - tw - SCALE1(PADDING);
	int y = item_y + (item_h - th) / 2;
	GFX_blitText(font.tiny, badge, 0, color, dst, &(SDL_Rect){x, y, tw, th});
}

static void renderMainMenu(void) {
	GFX_clear(screen);

	SimpleMenuConfig config = {
		.title = "Artwork Manager",
		.items = (const char*[]){"Library", "Progress", "Settings"},
		.item_count = 3,
		.btn_b_label = "EXIT",
		.render_badge = renderMainMenuBadge,
		.get_label = NULL,
		.get_icon = NULL,
		.render_text = NULL,
	};
	UI_renderSimpleMenu(screen, menu_selected, &config);
	GFX_flip(screen);
}

// ============================================
// SCREEN_SYSTEMS (pill-style, mostly unchanged)
// ============================================

static void renderSystemList(void) {
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Library");

	if (system_count == 0) {
		UI_renderEmptyState(screen, "No supported systems found",
							"Add ROMs to your SD card", NULL);
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
		GFX_flip(screen);
		return;
	}

	ListLayout layout = UI_calcListLayout(screen);
	UI_adjustListScroll(system_selected, &system_scroll, layout.items_per_page);

	for (int i = 0; i < layout.items_per_page && (system_scroll + i) < system_count; i++) {
		int idx = system_scroll + i;
		SystemEntry* sys = &systems[idx];
		bool selected = (idx == system_selected);

		char label[256];
		snprintf(label, sizeof(label), "%s", sys->name);

		char truncated[256];
		int y = layout.list_y + i * layout.item_h;

		char badge[64];
		snprintf(badge, sizeof(badge), "%d/%d", sys->scraped_count, sys->rom_count);
		int badge_tw = 0, badge_th = 0;
		TTF_SizeUTF8(font.tiny, badge, &badge_tw, &badge_th);
		int badge_prefix = badge_tw + SCALE1(PADDING);

		ListItemPos pos = UI_renderListItemPill(screen, &layout, font.large,
												label, truncated, y, selected, badge_prefix);

		SDL_Color text_color = UI_getListTextColor(selected);
		SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font.large, truncated, text_color);
		if (text_surf) {
			SDL_BlitSurface(text_surf, NULL, screen, &(SDL_Rect){pos.text_x, pos.text_y, 0, 0});
			SDL_FreeSurface(text_surf);
		}

		SDL_Color badge_color = selected ? COLOR_WHITE : COLOR_GRAY;
		int badge_x = pos.pill_width - SCALE1(PADDING) - badge_tw;
		GFX_blitText(font.tiny, badge, 0, badge_color, screen,
					 &(SDL_Rect){badge_x, pos.text_y + SCALE1(2), badge_tw, badge_th});
	}

	UI_renderScrollIndicators(screen, system_scroll, layout.items_per_page, system_count);
	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "OPEN", "Y", "QUEUE ALL", NULL});
	GFX_flip(screen);
}

// ============================================
// SCREEN_ROMS (settings-style rows)
// ============================================

static void renderROMList(void) {
	GFX_clear(screen);

	SystemEntry* sys = &systems[system_selected];
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
	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "QUEUE", "Y", "QUEUE ALL", NULL});
	GFX_flip(screen);
}

// ============================================
// SCREEN_PROGRESS
// ============================================

static void renderProgress(void) {
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Progress");

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
		if (s == SCRAPE_STATUS_DONE || s == SCRAPE_STATUS_SKIPPED)
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

	UI_renderMenuBar(screen, "Settings");

	ListLayout layout = UI_calcListLayout(screen);

	bool logged_in = cred_username[0] && cred_password[0];

	if (logged_in) {
		char quota_str[32] = "—";
		char max_str[32] = "—";
		if (user_info_fetched && cached_user_info.valid) {
			snprintf(quota_str, sizeof(quota_str), "%d", cached_user_info.requests_today);
			snprintf(max_str, sizeof(max_str), "%d", cached_user_info.max_requests_per_day);
		}

		UISettingsItem items[] = {
			{.label = "Username", .value = cred_username, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper username"},
			{.label = "Password", .value = "********", .swatch = -1, .cycleable = 0, .desc = "ScreenScraper password"},
			{.label = "Requests Today", .value = quota_str, .swatch = -1, .cycleable = 0, .desc = "API requests used today"},
			{.label = "Max Requests", .value = max_str, .swatch = -1, .cycleable = 0, .desc = "Daily request limit"},
			{.label = "Logout", .value = NULL, .swatch = -1, .cycleable = 0, .desc = "Clear saved credentials"},
		};

		int count = sizeof(items) / sizeof(items[0]);
		UI_renderSettingsPage(screen, &layout, items, count,
							  settings_selected, &settings_scroll, NULL);
	} else {
		UISettingsItem items[] = {
			{.label = "Username", .value = "Not set", .swatch = -1, .cycleable = 0, .desc = "ScreenScraper username"},
			{.label = "Password", .value = "Not set", .swatch = -1, .cycleable = 0, .desc = "ScreenScraper password"},
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
// Main
// ============================================

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

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

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();

		// Check if background thread made progress
		if (queue_dirty) {
			queue_dirty = false;
			dirty = true;
		}

		switch (current_screen) {
		case SCREEN_MAIN_MENU: {
			if (PAD_justPressed(BTN_B)) {
				app_quit = true;
				break;
			}

			if (PAD_navigateMenu(&menu_selected, 3))
				dirty = true;

			if (PAD_justPressed(BTN_A)) {
				switch (menu_selected) {
				case 0: // Library
					current_screen = SCREEN_SYSTEMS;
					system_selected = 0;
					system_scroll = 0;
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
						if (ScraperAPI_isOnline()) {
							UI_renderLoadingOverlay(screen, "Loading", "Fetching account info...");
							GFX_flip(screen);
							cached_user_info = ScraperAPI_fetchUserInfo();
							user_info_fetched = true;
						}
					}
					dirty = true;
					break;
				}
				break;
			}

			break;
		}
		case SCREEN_SYSTEMS: {
			if (PAD_justPressed(BTN_B)) {
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}

			if (PAD_navigateMenu(&system_selected, system_count))
				dirty = true;

			if (PAD_justPressed(BTN_A) && system_count > 0) {
				scanROMs(&systems[system_selected]);
				current_screen = SCREEN_ROMS;
				dirty = true;
				break;
			}

			if (PAD_justPressed(BTN_Y) && system_count > 0) {
				if (!ScraperAPI_isOnline()) {
					UI_renderLoadingOverlay(screen, "No Network",
											"Connect to WiFi first");
					GFX_flip(screen);
					SDL_Delay(1500);
					dirty = true;
					break;
				}
				int added = queueAddAllSystems();
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
				dirty = true;
				break;
			}

			break;
		}
		case SCREEN_ROMS: {
			if (PAD_justPressed(BTN_B)) {
				// Refresh system counts before going back
				systems[system_selected].scraped_count =
					countScrapedInDir(systems[system_selected].path);
				current_screen = SCREEN_SYSTEMS;
				dirty = true;
				break;
			}

			if (PAD_navigateMenu(&rom_selected, rom_count))
				dirty = true;

			if (PAD_justPressed(BTN_A) && rom_count > 0) {
				if (!ScraperAPI_isOnline()) {
					UI_renderLoadingOverlay(screen, "No Network",
											"Connect to WiFi first");
					GFX_flip(screen);
					SDL_Delay(1500);
					dirty = true;
					break;
				}
				ROMEntry* rom = &roms[rom_selected];
				if (rom->has_artwork || isROMQueued(rom->path)) {
					// Already scraped or queued
					UI_renderLoadingOverlay(screen, "Already queued",
											"ROM is already queued or scraped");
					GFX_flip(screen);
					SDL_Delay(800);
				} else {
					queueAddROM(rom, &systems[system_selected]);
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
					UI_renderLoadingOverlay(screen, "No Network",
											"Connect to WiFi first");
					GFX_flip(screen);
					SDL_Delay(1500);
					dirty = true;
					break;
				}
				int added = queueAddAllROMs(&systems[system_selected]);
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
					UIKeyboard_init();
					char* user = UIKeyboard_open("ScreenScraper Username");
					if (user) {
						snprintf(cred_username, sizeof(cred_username), "%s", user);
						free(user);
						saveCredentials();
						user_info_fetched = false;
						// Clamp selection if item count changed
						if (settings_selected >= (cred_username[0] && cred_password[0] ? 5 : 2))
							settings_selected = 0;
					}
					dirty = true;
					break;
				}
				case 1: { // Password
					UIKeyboard_init();
					char* pass = UIKeyboard_open("ScreenScraper Password");
					if (pass) {
						snprintf(cred_password, sizeof(cred_password), "%s", pass);
						free(pass);
						saveCredentials();
						user_info_fetched = false;
						if (settings_selected >= (cred_username[0] && cred_password[0] ? 5 : 2))
							settings_selected = 0;
					}
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
