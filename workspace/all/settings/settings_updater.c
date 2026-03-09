/*
 * settings_updater.c - System updater for NextUI Settings
 *
 * Checks GitHub for the latest release, compares against the installed
 * version, and allows the user to download + install updates.
 *
 * Single "Updater" item in the About page:
 *   - "Fetching update.."           while checking in background
 *   - "Install Update"  (desc=tag)  when update available, A to install
 *   - "You already have latest version"  when up to date
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>

#include "settings_updater.h"
#include "settings_menu.h"
#include "defines.h"
#include "api.h"
#include "ui_components.h"
#include "wget_fetch.h"

// ============================================
// Configuration
// ============================================

#define UPDATER_REPO_OWNER "mohammadsyuhada"
#define UPDATER_REPO_NAME "nx-redux"
#define VERSION_FILE_PATH "/mnt/SDCARD/.system/version.txt"
#define DOWNLOAD_PATH "/mnt/SDCARD/.tmp_update.zip"
#define EXTRACT_DEST "/mnt/SDCARD/"

// ============================================
// Device name mapping
// ============================================

static const char* get_device_name(void) {
	if (strcmp(PLATFORM, "tg5050") == 0)
		return "smartpros";
	char* device = getenv("DEVICE");
	if (device && strcmp(device, "brick") == 0)
		return "brick";
	return "smartpro";
}

// ============================================
// Release info
// ============================================

typedef struct {
	char tag_name[128];
	char commit_sha[64];
	char download_url[512];
	char release_notes[2048];
} ReleaseInfo;

// ============================================
// State
// ============================================

typedef enum {
	UPDATE_IDLE,
	UPDATE_CHECKING,
	UPDATE_UP_TO_DATE,
	UPDATE_AVAILABLE,
	UPDATE_ERROR,
} UpdateCheckState;

static UpdateCheckState auto_state = UPDATE_IDLE;
static ReleaseInfo cached_release = {0};
static char item_label[160] = "Updater";
static char item_desc[256] = "";
static char current_sha_cache[64] = "";
static char current_tag_cache[128] = "";

// Background check thread
static pthread_t auto_tid;
static volatile int auto_done = 0;
static volatile int auto_success = 0;
static char* auto_response_data = NULL;
static char auto_error[256] = "";

// ============================================
// JSON helpers
// ============================================

static const char* find_json_string(const char* json, const char* key, char* out, size_t out_size) {
	if (!json || !key || !out || out_size == 0)
		return NULL;

	char search[128];
	snprintf(search, sizeof(search), "\"%s\":\"", key);

	const char* start = strstr(json, search);
	if (!start) {
		snprintf(search, sizeof(search), "\"%s\": \"", key);
		start = strstr(json, search);
		if (!start)
			return NULL;
	}

	start += strlen(search);
	const char* end = strchr(start, '"');
	if (!end)
		return NULL;

	size_t len = end - start;
	if (len >= out_size)
		len = out_size - 1;

	strncpy(out, start, len);
	out[len] = '\0';

	return out;
}

static const char* find_zip_asset_url(const char* json, const char* platform, char* out, size_t out_size) {
	if (!json || !platform || !out || out_size == 0)
		return NULL;

	char suffix[64];
	snprintf(suffix, sizeof(suffix), "-%s.zip", platform);

	const char* assets = strstr(json, "\"assets\"");
	if (!assets)
		return NULL;

	const char* pos = assets;
	while ((pos = strstr(pos, "\"browser_download_url\"")) != NULL) {
		pos = strchr(pos + 21, '"');
		if (!pos)
			break;

		if (*(pos - 1) == ':') {
			// pos points to opening quote of value
		} else {
			pos++;
			while (*pos == ' ' || *pos == ':')
				pos++;
			if (*pos != '"')
				continue;
		}

		pos++;
		const char* end = strchr(pos, '"');
		if (!end)
			break;

		size_t len = end - pos;
		size_t suf_len = strlen(suffix);
		if (len > suf_len && strncmp(end - suf_len, suffix, suf_len) == 0) {
			if (len >= out_size)
				len = out_size - 1;
			strncpy(out, pos, len);
			out[len] = '\0';
			return out;
		}

		pos = end + 1;
	}

	return NULL;
}

static void extract_first_paragraph(const char* body, char* out, size_t out_size) {
	if (!body || !out || out_size == 0)
		return;

	out[0] = '\0';
	size_t j = 0;

	for (size_t i = 0; body[i] && j < out_size - 1; i++) {
		if (body[i] == '\\' && body[i + 1] == 'n') {
			if (body[i + 2] == '\\' && body[i + 3] == 'n')
				break;
			out[j++] = ' ';
			i++;
			continue;
		}
		if (body[i] == '\\' && body[i + 1] == 'r') {
			i++;
			continue;
		}
		if (body[i] == '#') {
			while (body[i] == '#')
				i++;
			if (body[i] == ' ')
				i++;
			i--;
			continue;
		}
		out[j++] = body[i];
	}

	out[j] = '\0';
	while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\n' || out[j - 1] == '\r'))
		out[--j] = '\0';
}

// ============================================
// Version reading
// ============================================

static void read_current_version(char* version, size_t ver_size,
								 char* sha, size_t sha_size,
								 char* tag, size_t tag_size) {
	version[0] = '\0';
	sha[0] = '\0';
	tag[0] = '\0';

	FILE* f = fopen(VERSION_FILE_PATH, "r");
	if (!f) {
		snprintf(version, ver_size, "Unknown");
		return;
	}

	if (!fgets(version, ver_size, f)) {
		snprintf(version, ver_size, "Unknown");
		fclose(f);
		return;
	}
	version[strcspn(version, "\r\n")] = '\0';

	if (!fgets(sha, sha_size, f))
		sha[0] = '\0';
	sha[strcspn(sha, "\r\n")] = '\0';

	if (!fgets(tag, tag_size, f))
		tag[0] = '\0';
	tag[strcspn(tag, "\r\n")] = '\0';

	fclose(f);
}

// ============================================
// Command execution (no shell, avoids injection)
// ============================================

static int run_command(char* const argv[]) {
	pid_t pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0) {
		freopen("/dev/null", "w", stderr);
		execvp(argv[0], argv);
		_exit(127);
	}

	int status;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

// ============================================
// Background auto-check thread
// ============================================

// Buffer size for GitHub API response (releases/latest JSON)
#define API_RESPONSE_SIZE (64 * 1024)

static void* auto_check_thread(void* arg) {
	(void)arg;

	char url[256];
	snprintf(url, sizeof(url),
			 "https://api.github.com/repos/%s/%s/releases/latest",
			 UPDATER_REPO_OWNER, UPDATER_REPO_NAME);

	uint8_t* buf = malloc(API_RESPONSE_SIZE);
	if (!buf) {
		snprintf(auto_error, sizeof(auto_error), "Memory allocation failed");
		auto_success = 0;
		__sync_synchronize();
		auto_done = 1;
		return NULL;
	}

	int ret = wget_fetch(url, buf, API_RESPONSE_SIZE);
	if (ret < 0) {
		snprintf(auto_error, sizeof(auto_error), "Failed to check for updates");
		free(buf);
		auto_success = 0;
		__sync_synchronize();
		auto_done = 1;
		return NULL;
	}

	buf[ret] = '\0';
	auto_response_data = (char*)buf;
	auto_success = 1;
	__sync_synchronize();
	auto_done = 1;
	return NULL;
}

// Parse the response and update state. Called from main thread.
static void process_auto_check_result(void) {
	pthread_join(auto_tid, NULL);

	if (!auto_success) {
		free(auto_response_data);
		auto_response_data = NULL;
		auto_state = UPDATE_ERROR;
		snprintf(item_label, sizeof(item_label), "Updater");
		snprintf(item_desc, sizeof(item_desc), "%s", auto_error);
		return;
	}

	char* data = auto_response_data;
	auto_response_data = NULL;
	ReleaseInfo release = {0};

	if (!find_json_string(data, "tag_name", release.tag_name,
						  sizeof(release.tag_name)) ||
		!find_json_string(data, "target_commitish", release.commit_sha,
						  sizeof(release.commit_sha)) ||
		!find_zip_asset_url(data, get_device_name(), release.download_url,
							sizeof(release.download_url))) {
		free(data);
		auto_state = UPDATE_ERROR;
		snprintf(item_label, sizeof(item_label), "Updater");
		snprintf(item_desc, sizeof(item_desc), "Could not parse release info");
		return;
	}

	char body[4096] = "";
	find_json_string(data, "body", body, sizeof(body));
	extract_first_paragraph(body, release.release_notes, sizeof(release.release_notes));
	free(data);

	// Compare tag names
	int is_same = 0;
	if (current_tag_cache[0] && release.tag_name[0]) {
		if (strcmp(current_tag_cache, release.tag_name) == 0)
			is_same = 1;
	}

	if (is_same) {
		auto_state = UPDATE_UP_TO_DATE;
		snprintf(item_label, sizeof(item_label), "You already have latest version");
		item_desc[0] = '\0';
	} else {
		cached_release = release;
		auto_state = UPDATE_AVAILABLE;
		snprintf(item_label, sizeof(item_label), "Install Update");
		snprintf(item_desc, sizeof(item_desc), "%s", release.tag_name);
	}
}

// ============================================
// Extract context (for 7z thread)
// ============================================

typedef struct {
	volatile int done;
	volatile int success;
	char error[256];
} ExtractContext;

static void* extract_thread(void* arg) {
	ExtractContext* ctx = (ExtractContext*)arg;

	char out_arg[256];
	snprintf(out_arg, sizeof(out_arg), "-o%s", EXTRACT_DEST);
	char* argv[] = {"/mnt/SDCARD/.system/shared/bin/7zzs.aarch64",
					"x", DOWNLOAD_PATH, out_arg, "-y", NULL};

	int ret = run_command(argv);
	if (ret != 0) {
		snprintf(ctx->error, sizeof(ctx->error), "Extraction failed");
		ctx->success = 0;
		__sync_synchronize();
		ctx->done = 1;
		return NULL;
	}

	unlink(DOWNLOAD_PATH);

	ctx->success = 1;
	__sync_synchronize();
	ctx->done = 1;
	return NULL;
}

// ============================================
// Full-screen update page helpers
// ============================================

static void format_speed(int speed_bps, char* out, size_t out_size) {
	if (speed_bps >= 1024 * 1024)
		snprintf(out, out_size, "%.1f MB/s", speed_bps / (1024.0 * 1024.0));
	else if (speed_bps >= 1024)
		snprintf(out, out_size, "%.0f KB/s", speed_bps / 1024.0);
	else if (speed_bps > 0)
		snprintf(out, out_size, "%d B/s", speed_bps);
	else
		out[0] = '\0';
}

static void format_eta(int eta_sec, char* out, size_t out_size) {
	if (eta_sec <= 0) {
		out[0] = '\0';
		return;
	}
	if (eta_sec >= 3600)
		snprintf(out, out_size, "%d:%02d:%02d", eta_sec / 3600, (eta_sec % 3600) / 60, eta_sec % 60);
	else if (eta_sec >= 60)
		snprintf(out, out_size, "%d:%02d", eta_sec / 60, eta_sec % 60);
	else
		snprintf(out, out_size, "%ds", eta_sec);
}

static void render_update_page(SDL_Surface* screen, const char* title,
							   const char* status, int progress_pct,
							   const char* speed_str, const char* eta_str,
							   int show_cancel) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, title);

	if (show_cancel)
		UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});

	int hw = screen->w;
	int cy = screen->h / 2;

	// Status text above progress bar
	if (status && status[0]) {
		SDL_Surface* status_surf = TTF_RenderUTF8_Blended(font.large, status, COLOR_WHITE);
		if (status_surf) {
			SDL_BlitSurface(status_surf, NULL, screen,
							&(SDL_Rect){(hw - status_surf->w) / 2, cy - status_surf->h - SCALE1(PADDING)});
			SDL_FreeSurface(status_surf);
		}
	}

	// Progress bar
	int below_bar_y = cy + SCALE1(PADDING);
	if (progress_pct >= 0) {
		int bar_w = hw - SCALE1(PADDING * 8);
		int bar_h = SCALE1(12);
		int bar_x = SCALE1(PADDING * 4);
		int bar_y = cy;

		// Background
		SDL_FillRect(screen, &(SDL_Rect){bar_x, bar_y, bar_w, bar_h},
					 SDL_MapRGB(screen->format, 64, 64, 64));

		// Fill
		if (progress_pct > 0) {
			int prog_w = (bar_w * progress_pct) / 100;
			SDL_FillRect(screen, &(SDL_Rect){bar_x, bar_y, prog_w, bar_h},
						 SDL_MapRGB(screen->format, 100, 200, 100));
		}

		// Percentage text inside bar
		char pct_str[16];
		snprintf(pct_str, sizeof(pct_str), "%d%%", progress_pct);
		SDL_Surface* pct_text = TTF_RenderUTF8_Blended(font.tiny, pct_str, COLOR_WHITE);
		if (pct_text) {
			SDL_BlitSurface(pct_text, NULL, screen,
							&(SDL_Rect){bar_x + (bar_w - pct_text->w) / 2,
										bar_y + (bar_h - pct_text->h) / 2});
			SDL_FreeSurface(pct_text);
		}

		below_bar_y = bar_y + bar_h + SCALE1(PADDING);
	}

	// Speed and ETA below progress bar
	if ((speed_str && speed_str[0]) || (eta_str && eta_str[0])) {
		char info[128] = "";
		if (speed_str && speed_str[0] && eta_str && eta_str[0])
			snprintf(info, sizeof(info), "%s - %s remaining", speed_str, eta_str);
		else if (speed_str && speed_str[0])
			snprintf(info, sizeof(info), "%s", speed_str);
		else if (eta_str && eta_str[0])
			snprintf(info, sizeof(info), "%s remaining", eta_str);

		if (info[0]) {
			SDL_Surface* info_surf = TTF_RenderUTF8_Blended(font.small, info, COLOR_GRAY);
			if (info_surf) {
				SDL_BlitSurface(info_surf, NULL, screen,
								&(SDL_Rect){(hw - info_surf->w) / 2, below_bar_y});
				SDL_FreeSurface(info_surf);
			}
		}
	}

	GFX_flip(screen);
}

static void show_message_page(SDL_Surface* screen, const char* title, const char* message) {
	unsigned long start = SDL_GetTicks();
	while (SDL_GetTicks() - start < 2000) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			break;
		render_update_page(screen, title, message, -1, NULL, NULL, 0);
	}
}

// Show update info (version + release notes) with A to confirm, B to cancel.
static int show_update_info(SDL_Surface* screen, ReleaseInfo* release) {
	while (1) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B))
			return 0;
		if (PAD_justPressed(BTN_A))
			return 1;

		GFX_clear(screen);
		UI_renderMenuBar(screen, "Update Available");
		UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", "A", "INSTALL", NULL});

		int bar_h = SCALE1(BUTTON_SIZE) + SCALE1(BUTTON_MARGIN * 2);
		int y = bar_h + SCALE1(PADDING * 2);

		SDL_Surface* tag_surf = TTF_RenderUTF8_Blended(font.large, release->tag_name, COLOR_WHITE);
		if (tag_surf) {
			SDL_BlitSurface(tag_surf, NULL, screen,
							&(SDL_Rect){(screen->w - tag_surf->w) / 2, y});
			y += tag_surf->h + SCALE1(PADDING);
			SDL_FreeSurface(tag_surf);
		}

		if (release->release_notes[0]) {
			int max_w = screen->w - SCALE1(PADDING * 4);
			char notes_copy[2048];
			strncpy(notes_copy, release->release_notes, sizeof(notes_copy) - 1);
			notes_copy[sizeof(notes_copy) - 1] = '\0';

			int max_lines = 8;
			GFX_wrapText(font.small, notes_copy, max_w, max_lines);
			GFX_blitWrappedText(font.small, notes_copy, max_w, max_lines,
								COLOR_GRAY, screen, y);
		}

		GFX_flip(screen);
	}
}

// Run download + extract + reboot with full-screen progress pages.
static void do_install(SDL_Surface* screen, ReleaseInfo* release) {
	pthread_t tid;

	// Prevent screen off and disable WiFi power save during download
	PWR_disableAutosleep();
	system("iw dev wlan0 set power_save off 2>/dev/null");

	// --- Download phase ---
	// Remove stale files
	char done_marker[256], headers_file[256];
	snprintf(done_marker, sizeof(done_marker), "%s.done", DOWNLOAD_PATH);
	snprintf(headers_file, sizeof(headers_file), "%s.headers", DOWNLOAD_PATH);
	unlink(DOWNLOAD_PATH);
	unlink(done_marker);
	unlink(headers_file);

	// Start wget in background with -S to capture Content-Length via stderr
	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
			 "(" SHARED_BIN_PATH "/wget --no-check-certificate -S -T 30 -t 2"
			 " -O '%s' '%s' 2>'%s'; echo $? > '%s') &",
			 DOWNLOAD_PATH, release->download_url, headers_file, done_marker);
	system(cmd);

	// Poll download progress from main thread
	long total_size = 0;
	int headers_parsed = 0;
	long prev_size = 0;
	int speed_bps = 0;
	int eta_sec = 0;
	struct timespec prev_time;
	clock_gettime(CLOCK_MONOTONIC, &prev_time);
	struct timespec stall_start = prev_time;
	long stall_size = 0;
	int cancelled = 0;
	int dl_success = 0;
	char dl_error[256] = "";

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Cancel
		if (PAD_justPressed(BTN_B)) {
			system("kill $(pgrep -f 'wget.*tmp_update') 2>/dev/null");
			unlink(DOWNLOAD_PATH);
			unlink(done_marker);
			unlink(headers_file);
			cancelled = 1;
			break;
		}

		// Check if wget finished
		int wget_done = (access(done_marker, F_OK) == 0);

		// Parse Content-Length from headers file (take last one after redirects)
		if (!headers_parsed) {
			FILE* hf = fopen(headers_file, "r");
			if (hf) {
				char line[256];
				long last_cl = 0;
				while (fgets(line, sizeof(line), hf)) {
					const char* p = line;
					while (*p == ' ' || *p == '\t')
						p++;
					if (strncasecmp(p, "Content-Length:", 15) == 0) {
						long val = atol(p + 15);
						if (val > 0)
							last_cl = val;
					}
				}
				fclose(hf);
				if (last_cl > 1024) {
					total_size = last_cl;
					headers_parsed = 1;
				}
			}
		}

		// Poll file size
		struct stat st;
		long curr_size = 0;
		if (stat(DOWNLOAD_PATH, &st) == 0)
			curr_size = st.st_size;

		// Progress percentage
		int progress_pct = 0;
		if (total_size > 0) {
			progress_pct = (int)((curr_size * 100) / total_size);
			if (progress_pct > 99 && !wget_done)
				progress_pct = 99;
		}

		// Speed and ETA (every ~1 second)
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - prev_time.tv_sec) +
						 (now.tv_nsec - prev_time.tv_nsec) / 1e9;
		if (elapsed >= 1.0) {
			long bytes_delta = curr_size - prev_size;
			speed_bps = (int)(bytes_delta / elapsed);
			if (speed_bps < 0)
				speed_bps = 0;
			if (speed_bps > 0 && total_size > 0)
				eta_sec = (int)((total_size - curr_size) / speed_bps);
			else
				eta_sec = 0;
			prev_size = curr_size;
			prev_time = now;
		}

		// Stall detection (only after data started flowing)
		if (curr_size != stall_size) {
			stall_size = curr_size;
			stall_start = now;
		} else if (curr_size > 0) {
			double stall_elapsed = (now.tv_sec - stall_start.tv_sec) +
								   (now.tv_nsec - stall_start.tv_nsec) / 1e9;
			if (stall_elapsed >= 60.0) {
				system("kill $(pgrep -f 'wget.*tmp_update') 2>/dev/null");
				snprintf(dl_error, sizeof(dl_error), "Download stalled");
				unlink(done_marker);
				unlink(headers_file);
				break;
			}
		}

		// Build status text: "v1.0.2 - 9.1 MB (3%)"
		char dl_status[256];
		if (curr_size > 0) {
			char size_str[64];
			if (curr_size >= 1024 * 1024 && total_size >= 1024 * 1024)
				snprintf(size_str, sizeof(size_str), "%.1f / %.0f MB",
						 curr_size / (1024.0 * 1024.0), total_size / (1024.0 * 1024.0));
			else if (curr_size >= 1024 * 1024)
				snprintf(size_str, sizeof(size_str), "%.1f MB",
						 curr_size / (1024.0 * 1024.0));
			else
				snprintf(size_str, sizeof(size_str), "%.0f KB", curr_size / 1024.0);

			if (progress_pct > 0)
				snprintf(dl_status, sizeof(dl_status), "%s - %s (%d%%)",
						 release->tag_name, size_str, progress_pct);
			else
				snprintf(dl_status, sizeof(dl_status), "%s - %s",
						 release->tag_name, size_str);
		} else {
			snprintf(dl_status, sizeof(dl_status), "%s - Connecting...",
					 release->tag_name);
		}

		char speed_str[32] = "";
		char eta_str[32] = "";
		format_speed(speed_bps, speed_str, sizeof(speed_str));
		format_eta(eta_sec, eta_str, sizeof(eta_str));

		render_update_page(screen, "Downloading Update",
						   dl_status, progress_pct, speed_str, eta_str, 1);

		// Check completion after render
		if (wget_done) {
			// Verify wget exit code
			int wget_exit = -1;
			FILE* mf = fopen(done_marker, "r");
			if (mf) {
				fscanf(mf, "%d", &wget_exit);
				fclose(mf);
			}
			unlink(done_marker);
			unlink(headers_file);

			if (wget_exit == 0 && curr_size > 1024) {
				dl_success = 1;
			} else {
				snprintf(dl_error, sizeof(dl_error), "Download failed (error %d)", wget_exit);
			}
			break;
		}
	}

	// Re-enable autosleep and WiFi power save after download
	PWR_enableAutosleep();
	system("iw dev wlan0 set power_save on 2>/dev/null");

	if (cancelled)
		return;

	if (!dl_success) {
		show_message_page(screen, "Update Error", dl_error);
		return;
	}

	// --- Extract phase ---
	ExtractContext ex = {0};

	if (pthread_create(&tid, NULL, extract_thread, &ex) != 0) {
		show_message_page(screen, "Update Error", "Failed to start installation");
		return;
	}

	// Render installing page until done
	while (!ex.done) {
		GFX_startFrame();
		PAD_poll();
		render_update_page(screen, "Installing Update",
						   "Please do not power off...", -1, NULL, NULL, 0);
	}
	__sync_synchronize();
	pthread_join(tid, NULL);

	if (!ex.success) {
		show_message_page(screen, "Update Error", ex.error);
		return;
	}

	// --- Reboot ---
	render_update_page(screen, "Update Complete",
					   "Rebooting...", -1, NULL, NULL, 0);
	sleep(2);
	system("reboot");
}

// ============================================
// Find the updater item in the page
// ============================================

static SettingItem* find_updater_item(SettingsPage* page) {
	for (int i = 0; i < page->item_count; i++) {
		if (page->items[i].on_press == updater_check_for_updates)
			return &page->items[i];
	}
	return NULL;
}

// ============================================
// Public API
// ============================================

void updater_about_on_show(SettingsPage* page) {
	(void)page;

	char version[128];
	read_current_version(version, sizeof(version),
						 current_sha_cache, sizeof(current_sha_cache),
						 current_tag_cache, sizeof(current_tag_cache));

	// Only auto-check once per session (retry allowed on error)
	if (auto_state == UPDATE_CHECKING || auto_state == UPDATE_UP_TO_DATE ||
		auto_state == UPDATE_AVAILABLE)
		return;

	// Check WiFi before attempting update check
	if (!WIFI_connected()) {
		auto_state = UPDATE_ERROR;
		snprintf(item_label, sizeof(item_label), "Updater");
		snprintf(item_desc, sizeof(item_desc), "No internet connection");
		SettingItem* item = find_updater_item(page);
		if (item) {
			item->name = item_label;
			item->desc = item_desc;
		}
		return;
	}

	auto_done = 0;
	auto_success = 0;
	auto_response_data = NULL;
	auto_error[0] = '\0';
	auto_state = UPDATE_CHECKING;
	snprintf(item_label, sizeof(item_label), "Checking for update..");
	item_desc[0] = '\0';

	SettingItem* item = find_updater_item(page);
	if (item) {
		item->name = item_label;
		item->desc = item_desc;
		item->type = ITEM_STATIC;
	}

	if (pthread_create(&auto_tid, NULL, auto_check_thread, NULL) != 0) {
		auto_state = UPDATE_ERROR;
		snprintf(item_label, sizeof(item_label), "Updater");
		snprintf(item_desc, sizeof(item_desc), "Failed to start update check");
		if (item) {
			item->name = item_label;
			item->desc = item_desc;
			item->type = ITEM_BUTTON;
		}
	}
}

void updater_about_on_tick(SettingsPage* page) {
	if (auto_state != UPDATE_CHECKING || !auto_done)
		return;

	__sync_synchronize();
	process_auto_check_result();

	SettingItem* item = find_updater_item(page);
	if (item) {
		item->name = item_label;
		item->desc = item_desc;
		// Only make clickable if there's an update or error (retry)
		item->type = (auto_state == UPDATE_AVAILABLE || auto_state == UPDATE_ERROR)
						 ? ITEM_BUTTON
						 : ITEM_STATIC;
		page->needs_layout = 1;
	}
}

const char* updater_get_status(void) {
	return item_label;
}

void updater_check_for_updates(void) {
	// Item is ITEM_STATIC during checking, so this shouldn't be called,
	// but guard anyway
	if (auto_state == UPDATE_CHECKING)
		return;

	// Do nothing if already up to date
	if (auto_state == UPDATE_UP_TO_DATE)
		return;

	// If update available, show info and install
	if (auto_state == UPDATE_AVAILABLE) {
		SettingsPage* page = settings_menu_current();
		if (!page || !page->screen)
			return;

		SDL_Surface* screen = page->screen;

		if (!show_update_info(screen, &cached_release))
			return;

		do_install(screen, &cached_release);
		return;
	}

	// Error or idle: retry the check (non-blocking, will update via on_tick)
	auto_state = UPDATE_IDLE;
	SettingsPage* page = settings_menu_current();
	if (page)
		updater_about_on_show(page);

	SettingItem* item = find_updater_item(page);
	if (item) {
		item->name = item_label;
		item->desc = item_desc;
	}
}
