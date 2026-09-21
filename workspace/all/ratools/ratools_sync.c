#include "ratools_sync.h"

#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ra_offline.h"
#include "ra_offline_net.h"
#include "ratools_data.h"
#include "wifi.h"
#include "ui_buttonhintbar.h"
#include "ui_downloadprogress.h"
#include "ui_menubar.h"
#include "ui_emptystate.h"

typedef struct {
	SDL_Surface* screen;
	bool cancelled; // B pressed during either phase
} RATSyncUI;

// Same progress-bar screen "Download all game data" uses (menu bar, status
// line, bar with percentage, detail line, B CANCEL hint). Blocking flows:
// each HTTP call is one curl with a 30s cap, so the screen only updates
// between requests.
static void rat_sync_render(SDL_Surface* screen, const char* status, const char* detail,
							int done, int total) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, "Sync now");

	char detail_buf[192];
	if (total > 0)
		snprintf(detail_buf, sizeof(detail_buf), "%s (%d/%d)", detail ? detail : "", done, total);
	else
		snprintf(detail_buf, sizeof(detail_buf), "%s", detail ? detail : "");

	UIDownloadProgress info = {
		.title = NULL, // menu bar drawn above
		.status = status,
		.detail = detail_buf,
		.progress = total > 0 ? (done * 100) / total : 0,
		.show_bar = true,
	};
	UI_renderDownloadProgress(screen, &info);

	UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(screen);
}

// phase 1 progress: journal entries submitted so far
static void rat_sync_progress(int done, int total, void* userdata) {
	RATSyncUI* ui = (RATSyncUI*)userdata;
	rat_sync_render(ui->screen, "Syncing offline unlocks", "Submitting", done, total);
}

// phase 2 progress: cloud state refreshed game-by-game
static void rat_sync_refresh_progress(int done, int total, const char* label,
									  void* userdata) {
	RATSyncUI* ui = (RATSyncUI*)userdata;
	rat_sync_render(ui->screen, "Refreshing cloud status", label, done, total);
}

// B cancels between requests: unsent journal entries stay for the next sync,
// already-refreshed games are kept.
static int rat_sync_cancel(void* userdata) {
	RATSyncUI* ui = (RATSyncUI*)userdata;
	PAD_poll();
	if (PAD_justPressed(BTN_B))
		ui->cancelled = true;
	return ui->cancelled ? 1 : 0;
}

// message modal using the shared empty-state component (icon + message +
// subtitle + centered B/BACK button, same as the music player library)
static void rat_sync_message_screen(SDL_Surface* screen, const char* line1, const char* line2) {
	bool quit = false, dirty = true;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))
			quit = true;
		if (dirty) {
			GFX_clear(screen);
			UI_renderEmptyState(screen, line1, line2, NULL);
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

void RATSync_run(SDL_Surface* screen) {
	// Preconditions (same messages as before). Note: no early "Nothing to
	// sync" return — even with an empty journal we still pull fresh cloud
	// state so unlocks earned in standalone emulators (DC.pak flycast) and
	// the header's points total stop going stale.
	if (!CFG_getRAAuthenticated() || strlen(CFG_getRAToken()) == 0) {
		rat_sync_message_screen(screen, "Not authenticated",
								"Set credentials in Settings and authenticate first.");
		return;
	}
	if (!Wifi_isConnected()) {
		rat_sync_message_screen(screen, "No network connection",
								"Connect to WiFi and try again.");
		return;
	}

	const char* username = CFG_getRAUsername();
	const char* token = CFG_getRAToken();
	RATSyncUI ui = {screen, false};

	// Phase 1: submit the offline unlock journal, if any.
	int pending = RA_Offline_pendingCount();
	int synced = 0;
	bool sync_failed = false;
	if (pending > 0) {
		rat_sync_progress(0, pending, &ui);
		synced = RA_OfflineNet_syncAllEx(username, token, rat_sync_progress,
										 rat_sync_cancel, &ui);
		sync_failed = (synced < 0);
	}

	// Phase 2: pull fresh cloud state (points + per-game unlock counts).
	// Skipped when B already cancelled phase 1.
	RA_RefreshStats stats = {0};
	int refresh_rc = -1;
	if (!ui.cancelled) {
		rat_sync_refresh_progress(0, 0, "Checking cloud status", &ui);
		refresh_rc = RA_OfflineNet_refreshCloud(username, token, rat_sync_refresh_progress,
												rat_sync_cancel, &ui, &stats);
	}

	// Result screen.
	char line1[64], line2[160];

	if (ui.cancelled) {
		int remaining = RA_Offline_pendingCount();
		snprintf(line1, sizeof(line1), "Cancelled");
		if (remaining > 0)
			snprintf(line2, sizeof(line2), "%d unlock%s kept for next sync.",
					 remaining, remaining == 1 ? "" : "s");
		else
			snprintf(line2, sizeof(line2), "%d of %d game%s refreshed before stopping.",
					 stats.games_updated, stats.games_checked,
					 stats.games_checked == 1 ? "" : "s");
		rat_sync_message_screen(screen, line1, line2);
		return;
	}

	if (sync_failed) {
		// journal submission is the primary action when unlocks were waiting
		snprintf(line1, sizeof(line1), "Sync failed");
		snprintf(line2, sizeof(line2), "Check credentials and connection.");
		rat_sync_message_screen(screen, line1, line2);
		return;
	}

	int remaining = RA_Offline_pendingCount();

	if (synced > 0)
		snprintf(line1, sizeof(line1), "Synced %d unlock%s", synced, synced == 1 ? "" : "s");
	else if (refresh_rc == 0)
		snprintf(line1, sizeof(line1), stats.games_updated > 0 ? "Refreshed" : "Up to date");
	else
		snprintf(line1, sizeof(line1), "Sync failed");

	if (refresh_rc != 0) {
		// cloud refresh (login2) failed
		if (synced > 0)
			snprintf(line2, sizeof(line2), "Unlocks submitted, but cloud refresh failed.");
		else
			snprintf(line2, sizeof(line2), "Could not refresh cloud status. Try again.");
	} else if (remaining > 0) {
		snprintf(line2, sizeof(line2), "%d unlock%s kept for next sync.",
				 remaining, remaining == 1 ? "" : "s");
	} else {
		uint32_t score = 0, soft = 0;
		char pts[40];
		if (RAT_getCachedScore(&score, &soft))
			snprintf(pts, sizeof(pts), "%u point%s", soft, soft == 1 ? "" : "s");
		else
			snprintf(pts, sizeof(pts), "Cloud status current");
		if (stats.games_checked > 0)
			snprintf(line2, sizeof(line2), "%s, %d of %d game%s updated",
					 pts, stats.games_updated, stats.games_checked,
					 stats.games_checked == 1 ? "" : "s");
		else
			snprintf(line2, sizeof(line2), "%s", pts);
	}
	rat_sync_message_screen(screen, line1, line2);
}
