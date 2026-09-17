#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "cheatdb_data.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_downloadprogress.h"
#include "ui_emptystate.h"
#include "ui_list.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "wget_fetch.h"

#define TITLE "Cheat Database"
// Truncation buffer for a rendered row's text. Matches options.c's row buffer.
#define ROW_TEXT_MAX 256

static SDL_Surface* screen = NULL;
static CheatdbPaths P;

// ---- download-with-progress (settings_updater.c pattern) ------------------
typedef struct {
	volatile int progress, speed, eta;
	volatile bool cancel, done;
	int result;
} DlCtx;
static DlCtx dl;
static void* dl_thread(void* a) {
	(void)a;
	dl.result = wget_download_file(CHEATDB_URL, P.zip, &dl.progress, &dl.cancel, &dl.speed, &dl.eta);
	dl.done = true;
	return NULL;
}
static void fmt_speed(int b, char* o, size_t n) {
	if (b >= 1024 * 1024)
		snprintf(o, n, "%.1f MB/s", b / (1024.0 * 1024.0));
	else if (b >= 1024)
		snprintf(o, n, "%.0f KB/s", b / 1024.0);
	else if (b > 0)
		snprintf(o, n, "%d B/s", b);
	else
		o[0] = '\0';
}
static void render_progress(const char* status, int pct, const char* detail, int cancelable) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, TITLE);
	UI_renderDownloadProgress(screen, &(UIDownloadProgress){
										  .status = status, .detail = detail, .progress = pct, .show_bar = (pct >= 0)});
	if (cancelable)
		UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(screen);
}
static bool enough_space(void) {
	struct statvfs v;
	if (statvfs(getenv("SDCARD_PATH") ? getenv("SDCARD_PATH") : "/mnt/SDCARD", &v) != 0)
		return true;
	long free_kb = (long)((unsigned long long)v.f_bavail * v.f_frsize / 1024);
	return free_kb >= CHEATDB_NEED_KB;
}

// Returns true on a completed install, false on cancel/failure (message shown).
static bool do_download(void) {
	if (!enough_space()) {
		UI_showMessage(screen, "Not enough space (need ~256 MB free)", 3000);
		return false;
	}
	char tmpcmd[600];
	snprintf(tmpcmd, sizeof(tmpcmd), "rm -rf '%s'; mkdir -p '%s'", P.tmpdir, P.tmpdir);
	system(tmpcmd);

	memset(&dl, 0, sizeof(dl));
	dl.progress = -1;
	pthread_t th;
	if (pthread_create(&th, NULL, dl_thread, NULL) != 0) {
		UI_showMessage(screen, "Download failed to start", 3000);
		return false;
	}

	bool dirty = true;
	IndicatorType ind = INDICATOR_NONE;
	while (!dl.done) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &ind, NULL, NULL);
		if (PAD_justPressed(BTN_B))
			dl.cancel = true;
		char sp[32];
		fmt_speed(dl.speed, sp, sizeof(sp));
		render_progress("Downloading cheat database...", dl.progress, sp, 1);
	}
	pthread_join(th, NULL);
	if (dl.cancel || dl.result < 0) {
		char rm[600];
		snprintf(rm, sizeof(rm), "rm -rf '%s'", P.tmpdir);
		system(rm);
		if (!dl.cancel)
			UI_showMessage(screen, "Download failed (check WiFi)", 3000);
		return false;
	}

	// verify archive
	char vcmd[700];
	if (strstr(P.unzip, "7zzs"))
		snprintf(vcmd, sizeof(vcmd), "'%s' t '%s' >/dev/null 2>&1", P.unzip, P.zip);
	else
		snprintf(vcmd, sizeof(vcmd), "'%s' -tq '%s' >/dev/null 2>&1", P.unzip, P.zip);
	if (system(vcmd) != 0) {
		char rm[600];
		snprintf(rm, sizeof(rm), "rm -rf '%s'", P.tmpdir);
		system(rm);
		UI_showMessage(screen, "Downloaded archive is corrupt", 3000);
		return false;
	}

	// extract each mapped system, per-system progress
	Cheatdb_truncateManifest(&P);
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++) {
		char dest[700];
		snprintf(dest, sizeof(dest), "%s/%s", P.cheats_dir, CHEATDB_MAP[i].tag);
		char status[128];
		snprintf(status, sizeof(status), "Installing %s (%d/%d)", CHEATDB_MAP[i].tag, i + 1, CHEATDB_MAP_COUNT);
		render_progress(status, (i + 1) * 100 / CHEATDB_MAP_COUNT, "", 0);
		Cheatdb_extractFolder(&P, CHEATDB_MAP[i].folder, dest);
		Cheatdb_appendManifest(&P, CHEATDB_MAP[i].folder, dest);
	}

	char lm[128];
	if (Cheatdb_remoteLastModified(lm, sizeof(lm)) <= 0)
		snprintf(lm, sizeof(lm), "installed");
	Cheatdb_writeDbVersion(&P, lm);
	char rm[600];
	snprintf(rm, sizeof(rm), "rm -rf '%s'", P.tmpdir);
	system(rm);
	UI_showMessage(screen, "Done. Open a game and see Options > Cheats.", 2500);
	return true;
}

static void do_update(void) {
	render_progress("Checking for updates...", -1, "", 0);
	char remote[128];
	int n = Cheatdb_remoteLastModified(remote, sizeof(remote));
	if (n <= 0) {
		UI_showMessage(screen, "Could not check (WiFi?)", 3000);
		return;
	}
	char cur[128] = {0};
	Cheatdb_readDbVersion(&P, cur, sizeof(cur));
	if (strcmp(remote, cur) == 0)
		UI_showMessage(screen, "Cheat database is up to date", 2500);
	else
		do_download();
}

static void do_remove(void) {
	if (!UI_confirmModal(screen, "Remove cheat database?", "Hand-made cheats are kept.", NULL, true, true))
		return;
	Cheatdb_removeAll(&P);
	UI_showMessage(screen, "Cheat database removed", 2000);
}

// ---- empty state ----------------------------------------------------------
// Returns true if the user chose Download (A), false to exit (B).
static bool empty_screen(void) {
	char* btns[] = {"A", "Download", "B", "EXIT", NULL};
	bool dirty = true;
	IndicatorType ind = INDICATOR_NONE;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &ind, NULL, NULL);
		if (UI_statusBarChanged())
			dirty = true;
		if (PAD_justPressed(BTN_A))
			return true;
		if (PAD_justPressed(BTN_B))
			return false;
		if (dirty) {
			GFX_clear(screen);
			UI_renderMenuBar(screen, TITLE);
			UI_renderEmptyStateButtons(screen, "No cheat database installed",
									   "Download it to add cheats for your games.", btns);
			GFX_flip(screen);
			dirty = false;
		} else
			GFX_sync();
	}
}

// ---- status screen (installed) --------------------------------------------
// A small action list; the status summary is drawn in the menu bar subtitle
// area via the list title. Returns when the user backs out (B).
static void status_screen(void) {
	const char* labels[] = {"Check for updates", "Remove cheat database"};
	int count = 2, selected = 0, scroll = 0;
	bool dirty = true;
	IndicatorType ind = INDICATOR_NONE;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &ind, NULL, NULL);
		if (UI_statusBarChanged())
			dirty = true;
		if (PAD_navigateMenu(&selected, count))
			dirty = true;
		else if (PAD_justPressed(BTN_B))
			return;
		else if (PAD_justPressed(BTN_A)) {
			if (selected == 0)
				do_update();
			else
				do_remove();
			if (!Cheatdb_installed(&P))
				return; // removed -> back to empty state
			dirty = true;
		}
		if (dirty) {
			GFX_clear(screen);
			char title[128];
			snprintf(title, sizeof(title), "%s  -  %d cheats", TITLE, Cheatdb_totalCount(&P));
			UI_renderMenuBar(screen, title);
			ListLayout layout = UI_calcListLayout(screen);
			UI_adjustListScroll(selected, &scroll, layout.items_per_page);
			char trunc[ROW_TEXT_MAX];
			for (int i = 0; i < count; i++) {
				bool sel = (i == selected);
				MenuItemPos pos = UI_renderMenuItemPill(screen, &layout, labels[i], trunc, i, sel, 0);
				UI_renderListItemText(screen, NULL, trunc, font.large, pos.text_x, pos.text_y, layout.max_width, sel);
			}
			UI_renderButtonHintBar(screen, (char*[]){"A", "OK", "B", "BACK", NULL});
			GFX_flip(screen);
			dirty = false;
		} else
			GFX_sync();
	}
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	PATHS_init(PLATFORM);
	Cheatdb_initPaths(&P);

	screen = GFX_init(MODE_MAIN);
	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	InitSettings();
	PAD_init();
	PWR_init();
	PWR_disableSleep();
	PWR_disableAutosleep();

	// Top-level: empty state until installed, then the status screen.
	while (1) {
		if (!Cheatdb_installed(&P)) {
			if (!empty_screen())
				break;			  // B on empty state exits the app
			if (!do_download()) { // A -> download; on cancel/failure,
				if (!Cheatdb_installed(&P))
					continue; // stay on empty state
			}
		} else {
			status_screen(); // B returns here
			if (!Cheatdb_installed(&P))
				continue; // removed -> empty state
			break;		  // B on status screen exits the app
		}
	}

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return 0;
}
