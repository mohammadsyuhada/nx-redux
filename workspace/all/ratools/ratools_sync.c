#include "ratools_sync.h"

#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ra_offline.h"
#include "ra_offline_net.h"
#include "ui_emptystate.h"

static void rat_sync_message_screen(SDL_Surface* screen, const char* line1, const char* line2);

typedef struct {
	SDL_Surface* screen;
} RATSyncUI;

static void rat_sync_render_line(SDL_Surface* screen, const char* text, int y) {
	SDL_Surface* s = TTF_RenderUTF8_Blended(font.medium, text,
											(SDL_Color){255, 255, 255, 255});
	if (!s)
		return;
	SDL_BlitSurface(s, NULL, screen,
					&(SDL_Rect){(screen->w - s->w) / 2, y, s->w, s->h});
	SDL_FreeSurface(s);
}

// progress callback: rendered inline (submission is blocking; each HTTP call
// is one curl with a 30s cap, so the screen only updates between entries)
static void rat_sync_progress(int done, int total, void* userdata) {
	RATSyncUI* ui = (RATSyncUI*)userdata;
	char msg[64];
	snprintf(msg, sizeof(msg), "Submitting %d/%d...", done, total);
	GFX_clear(ui->screen);
	rat_sync_render_line(ui->screen, "Syncing offline unlocks", ui->screen->h / 2 - SCALE1(30));
	rat_sync_render_line(ui->screen, msg, ui->screen->h / 2);
	GFX_flip(ui->screen);
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
	int pending = RA_Offline_pendingCount();
	if (pending == 0) {
		rat_sync_message_screen(screen, "Nothing to sync", "No offline unlocks are waiting.");
		return;
	}
	if (!CFG_getRAAuthenticated() || strlen(CFG_getRAToken()) == 0) {
		rat_sync_message_screen(screen, "Not authenticated",
								"Set credentials in Settings and authenticate first.");
		return;
	}
	if (!PLAT_wifiConnected()) {
		rat_sync_message_screen(screen, "No network connection",
								"Connect to WiFi and try again.");
		return;
	}

	RATSyncUI ui = {screen};
	rat_sync_progress(0, pending, &ui);
	int synced = RA_OfflineNet_syncAll(CFG_getRAUsername(), CFG_getRAToken(),
									   rat_sync_progress, &ui);

	char line1[64], line2[96];
	int remaining = RA_Offline_pendingCount();
	if (synced < 0) {
		snprintf(line1, sizeof(line1), "Sync failed");
		snprintf(line2, sizeof(line2), "Check credentials and connection.");
	} else if (remaining > 0) {
		snprintf(line1, sizeof(line1), "Synced %d unlock%s", synced, synced == 1 ? "" : "s");
		snprintf(line2, sizeof(line2), "%d could not be submitted - kept for next sync.", remaining);
	} else {
		snprintf(line1, sizeof(line1), "Synced %d unlock%s", synced, synced == 1 ? "" : "s");
		snprintf(line2, sizeof(line2), "All offline unlocks are on the server.");
	}
	rat_sync_message_screen(screen, line1, line2);
}
