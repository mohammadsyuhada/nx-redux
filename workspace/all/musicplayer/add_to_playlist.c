#include <stdio.h>
#include <string.h>
#include "api.h"
#include "add_to_playlist.h"
#include "playlist.h"
#include "playlist_m3u.h"
#include "ui_keyboard.h"
#include "ui_listdialog.h"
#include "display_helper.h"

// Internal state
static bool active = false;
static bool is_folder = false; // track_path is a directory: add all audio under it
static char track_path[512];
static char track_display_name[256];

static PlaylistInfo playlists[MAX_PLAYLISTS];
static int playlist_count = 0;

// Toast state (shown after adding)
static char toast_msg[128] = "";
static uint32_t toast_time = 0;

static void populate_items(void) {
	int total = playlist_count + 1; // +1 for "New Playlist"
	ListDialogItem items[LISTDIALOG_MAX_ITEMS];
	memset(items, 0, sizeof(ListDialogItem) * total);

	// Index 0: New Playlist
	snprintf(items[0].text, LISTDIALOG_MAX_TEXT, "+ New Playlist");
	items[0].prepend_icons[0] = -1;
	items[0].append_icons[0] = -1;

	// Index 1..N: existing playlists
	for (int i = 0; i < playlist_count; i++) {
		snprintf(items[i + 1].text, LISTDIALOG_MAX_TEXT, "%s", playlists[i].name);
		snprintf(items[i + 1].detail, LISTDIALOG_MAX_TEXT, "%d track%s",
				 playlists[i].track_count, playlists[i].track_count == 1 ? "" : "s");
		items[i + 1].prepend_icons[0] = -1;
		items[i + 1].append_icons[0] = -1;
	}

	ListDialog_setItems(items, total);
}

void AddToPlaylist_open(const char* path, const char* display_name) {
	if (!path)
		return;

	M3U_init();
	is_folder = false;
	snprintf(track_path, sizeof(track_path), "%s", path);
	snprintf(track_display_name, sizeof(track_display_name), "%s",
			 display_name ? display_name : "");

	playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);

	ListDialog_init("Add to Playlist");
	populate_items();

	active = true;
}

void AddToPlaylist_openFolder(const char* dir_path, const char* display_name) {
	AddToPlaylist_open(dir_path, display_name);
	if (active)
		is_folder = true;
}

// Add every audio file under track_path (a directory) to the playlist.
// Returns the number of tracks actually added (duplicates skipped).
static int add_folder_tracks(const char* m3u_path) {
	PlaylistContext ctx = {0};
	int n = Playlist_buildFromDirectory(&ctx, track_path, "");
	// Batch-append (reads the existing .m3u once instead of once per track).
	int added = (n > 0 && ctx.tracks) ? M3U_addTracks(m3u_path, ctx.tracks, n) : 0;
	Playlist_free(&ctx);
	return added;
}

// Add the pending track/folder to the playlist at `m3u_path` named `name`,
// and set the result toast.
static void add_to(const char* m3u_path, const char* name) {
	if (is_folder) {
		int added = add_folder_tracks(m3u_path);
		if (added > 0)
			snprintf(toast_msg, sizeof(toast_msg), "Added %d track%s to %s",
					 added, added == 1 ? "" : "s", name);
		else
			snprintf(toast_msg, sizeof(toast_msg), "Already in %s", name);
	} else if (M3U_containsTrack(m3u_path, track_path)) {
		snprintf(toast_msg, sizeof(toast_msg), "Already in %s", name);
	} else {
		M3U_addTrack(m3u_path, track_path, track_display_name);
		snprintf(toast_msg, sizeof(toast_msg), "Added to %s", name);
	}
	toast_time = SDL_GetTicks();
}

bool AddToPlaylist_isActive(void) {
	return active;
}

int AddToPlaylist_handleInput(void) {
	if (!active)
		return 1;

	ListDialogResult result = ListDialog_handleInput();

	if (result.action == LISTDIALOG_CANCEL) {
		ListDialog_quit();
		active = false;
		return 1;
	}

	if (result.action == LISTDIALOG_SELECTED) {
		if (result.index == 0) {
			// New Playlist
			char* name = UIKeyboard_open("Playlist name");
			PAD_poll();
			PAD_reset();
			if (name && name[0]) {
				if (M3U_create(name) == 0) {
					char new_path[512];
					snprintf(new_path, sizeof(new_path), "%s/%s.m3u", PLAYLISTS_DIR, name);
					add_to(new_path, name);
				}
				free(name);
			}
		} else {
			// Existing playlist
			int idx = result.index - 1;
			if (idx >= 0 && idx < playlist_count) {
				add_to(playlists[idx].path, playlists[idx].name);
			}
		}
		ListDialog_quit();
		active = false;
		return 1;
	}

	return 0;
}

void AddToPlaylist_render(SDL_Surface* screen) {
	if (!active)
		return;

	ListDialog_render(screen);
}

// Get toast message and time (for callers to display after dialog closes)
const char* AddToPlaylist_getToastMessage(void) {
	return toast_msg;
}

uint32_t AddToPlaylist_getToastTime(void) {
	return toast_time;
}

void AddToPlaylist_clearToast(void) {
	toast_msg[0] = '\0';
	toast_time = 0;
}
