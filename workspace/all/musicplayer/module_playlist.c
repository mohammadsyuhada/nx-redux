#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "ui_toast.h"
#include "module_playlist.h"
#include "module_player.h"
#include "playlist_m3u.h"
#include "playlist.h"
#include "ui_keyboard.h"
#include "display_helper.h"
#include "ui_confirmdialog.h"
#include "ui_playlist.h"
#include "ui_listview.h"

// Internal states
typedef enum {
	PLAYLIST_INTERNAL_LIST,
	PLAYLIST_INTERNAL_DETAIL
} PlaylistInternalState;

// List state (selection/scroll live in the ListViews owned by
// ui_playlist.c - see PlaylistList_view()/PlaylistDetail_view())
static PlaylistInfo playlists[MAX_PLAYLISTS];
static int playlist_count = 0;

// Detail state
static PlaylistTrack detail_tracks[PLAYLIST_MAX_TRACKS];
static int detail_track_count = 0;
static int current_playlist_index = -1; // Index into playlists[] for current detail view

// Toast
static char playlist_toast_message[128] = "";
static uint32_t playlist_toast_time = 0;

// Confirmation dialog state
static bool show_confirm = false;
static char confirm_name[256] = "";
static int confirm_action = 0; // 0 = delete playlist, 1 = remove track
static int confirm_target = -1;

// Controls help state IDs (for render_controls_help)
#define PLAYLIST_LIST_HELP_STATE 50
#define PLAYLIST_DETAIL_HELP_STATE 51

// Context-menu item ids
#define PLAYLIST_CTX_RENAME 1		// list: rename selected playlist
#define PLAYLIST_CTX_DELETE 2		// list: delete selected playlist (confirm)
#define PLAYLIST_CTX_REMOVE_TRACK 3 // detail: remove selected track (confirm)

static void refresh_playlists(void) {
	playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);
}

static void refresh_detail(void) {
	if (current_playlist_index < 0 || current_playlist_index >= playlist_count)
		return;
	M3U_loadTracks(playlists[current_playlist_index].path, detail_tracks, PLAYLIST_MAX_TRACKS, &detail_track_count);
}

static void show_toast(const char* msg) {
	snprintf(playlist_toast_message, sizeof(playlist_toast_message), "%s", msg);
	playlist_toast_time = SDL_GetTicks();
}

// New Playlist via the on-screen keyboard (Y anywhere, A on the empty list).
// May swap *screen_p on TG5050 display recovery.
static void create_playlist(SDL_Surface** screen_p) {
	char* name = UIKeyboard_open("Playlist name");
	PAD_poll();
	PAD_reset();
	{
		SDL_Surface* ns = DisplayHelper_getReinitScreen();
		if (ns)
			*screen_p = ns;
	}
	if (name && name[0]) {
		if (M3U_create(name) == 0) {
			show_toast("Playlist created");
			refresh_playlists();
		} else {
			show_toast("Already exists");
		}
	}
	free(name);
}

// Rename the selected playlist via the on-screen keyboard.
static void rename_playlist(SDL_Surface** screen_p, int idx) {
	if (idx < 0 || idx >= playlist_count)
		return;
	char prompt[300];
	snprintf(prompt, sizeof(prompt), "Rename: %s", playlists[idx].name);
	char* name = UIKeyboard_open(prompt);
	PAD_poll();
	PAD_reset();
	{
		SDL_Surface* ns = DisplayHelper_getReinitScreen();
		if (ns)
			*screen_p = ns;
	}
	if (name && name[0]) {
		if (M3U_rename(playlists[idx].path, name) == 0) {
			show_toast("Playlist renamed");
			refresh_playlists();
			ListView* lv = PlaylistList_view();
			UI_listViewReset(lv, playlist_count, playlists);
			// Follow the renamed playlist to its new (sorted) position
			for (int i = 0; i < playlist_count; i++) {
				if (strcmp(playlists[i].name, name) == 0) {
					lv->selected = i;
					break;
				}
			}
		} else {
			show_toast("Already exists");
		}
	}
	free(name);
}

ModuleExitReason PlaylistModule_run(SDL_Surface* screen) {
	M3U_init();
	UIKeyboard_init();
	refresh_playlists();

	PlaylistInternalState state = PLAYLIST_INTERNAL_LIST;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Handle confirmation dialog
		if (show_confirm) {
			if (PAD_justPressed(BTN_A)) {
				if (confirm_action == 0) {
					// Delete playlist. Content changed: reset the view to the
					// rebuilt array (glide snap + marquee clear), then restore
					// the cursor clamped like the old code did.
					int idx = confirm_target;
					if (idx >= 0 && idx < playlist_count) {
						M3U_delete(playlists[idx].path);
						refresh_playlists();
						ListView* lv = PlaylistList_view();
						int prev_selected = lv->selected;
						UI_listViewReset(lv, playlist_count, playlists);
						if (playlist_count > 0)
							lv->selected = (prev_selected < playlist_count)
											   ? prev_selected
											   : playlist_count - 1;
						show_toast("Playlist deleted");
					}
				} else if (confirm_action == 1) {
					// Remove track. Same Reset-then-assign: the tracks buffer
					// was refilled in place, keyed by the playlist's name.
					int idx = confirm_target;
					if (current_playlist_index >= 0 && current_playlist_index < playlist_count) {
						M3U_removeTrack(playlists[current_playlist_index].path, idx);
						refresh_detail();
						// Update parent count
						playlists[current_playlist_index].track_count = detail_track_count;
						ListView* dv = PlaylistDetail_view();
						int prev_selected = dv->selected;
						UI_listViewReset(dv, detail_track_count,
										 playlists[current_playlist_index].name);
						if (detail_track_count > 0)
							dv->selected = (prev_selected < detail_track_count)
											   ? prev_selected
											   : detail_track_count - 1;
						show_toast("Track removed");
					}
				}
				show_confirm = false;
				dirty = 1;
				continue;
			}
			if (PAD_justPressed(BTN_B)) {
				show_confirm = false;
				dirty = 1;
				continue;
			}
			// Render confirmation (dialog covers entire screen)
			const char* confirm_title = (confirm_action == 0) ? "Delete Playlist?" : "Remove Track?";
			UI_renderConfirmDialog(screen, confirm_title, confirm_name);
			GFX_flip(screen);
			GFX_sync();
			continue;
		}

		// Context menu for the current page
		ContextMenuItem ctx_items[4];
		int ctx_count = 0;
		if (state == PLAYLIST_INTERNAL_LIST) {
			ListView* lv = PlaylistList_view();
			if (playlist_count > 0 && lv->selected >= 0 && lv->selected < playlist_count) {
				ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Rename Playlist", PLAYLIST_CTX_RENAME);
				ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Delete Playlist", PLAYLIST_CTX_DELETE);
			}
		} else {
			ListView* dv = PlaylistDetail_view();
			if (detail_track_count > 0 && dv->selected >= 0 && dv->selected < detail_track_count)
				ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Remove from Playlist", PLAYLIST_CTX_REMOVE_TRACK);
		}
		ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

		// Handle global input
		int app_state_for_help = (state == PLAYLIST_INTERNAL_LIST) ? PLAYLIST_LIST_HELP_STATE : PLAYLIST_DETAIL_HELP_STATE;
		GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help, ctx_items, ctx_count);
		if (global.should_quit) {
			return MODULE_EXIT_QUIT;
		}
		if (global.context_id > 0) {
			switch (global.context_id) {
			case PLAYLIST_CTX_RENAME:
				rename_playlist(&screen, PlaylistList_view()->selected);
				break;
			case PLAYLIST_CTX_DELETE: {
				int idx = PlaylistList_view()->selected;
				if (idx >= 0 && idx < playlist_count) {
					snprintf(confirm_name, sizeof(confirm_name), "%s", playlists[idx].name);
					confirm_action = 0;
					confirm_target = idx;
					show_confirm = true;
					GFX_clearLayers(LAYER_SCROLLTEXT);
				}
				break;
			}
			case PLAYLIST_CTX_REMOVE_TRACK: {
				int idx = PlaylistDetail_view()->selected;
				if (idx >= 0 && idx < detail_track_count) {
					snprintf(confirm_name, sizeof(confirm_name), "%s", detail_tracks[idx].name);
					confirm_action = 1;
					confirm_target = idx;
					show_confirm = true;
					GFX_clearLayers(LAYER_SCROLLTEXT);
				}
				break;
			}
			}
			dirty = 1;
		}
		if (global.input_consumed) {
			if (global.dirty)
				dirty = 1;
			GFX_sync();
			continue;
		}

		if (state == PLAYLIST_INTERNAL_LIST) {
			ListView* v = PlaylistList_view();

			// The ListView owns navigation; the module switches on actions.
			ListViewAction act = UI_listViewHandleInput(v);
			switch (act.type) {
			case LISTVIEW_ACTIVATED:
				// Enter playlist detail. Reset keys the detail view to this
				// playlist's name (glide snap + marquee clear + cursor to 0).
				if (act.index >= 0 && act.index < playlist_count) {
					current_playlist_index = act.index;
					refresh_detail();
					UI_listViewReset(PlaylistDetail_view(), detail_track_count,
									 playlists[act.index].name);
					state = PLAYLIST_INTERNAL_DETAIL;
					dirty = 1;
				}
				break;
			case LISTVIEW_BACK:
				GFX_clearLayers(LAYER_SCROLLTEXT);
				return MODULE_EXIT_TO_MENU;
			case LISTVIEW_BUTTON:
				if (act.btn == BTN_Y ||
					(act.btn == BTN_A && playlist_count == 0)) {
					// New Playlist: Y anywhere, or A from the empty state
					// (its hint advertises A/NEW; act.index is -1 there).
					// Delete moved to the context menu (MENU tap).
					create_playlist(&screen);
					dirty = 1;
				}
				break;
			default:
				break;
			}

		} else if (state == PLAYLIST_INTERNAL_DETAIL) {
			ListView* v = PlaylistDetail_view();

			// The ListView owns navigation; the module switches on actions.
			ListViewAction act = UI_listViewHandleInput(v);
			switch (act.type) {
			case LISTVIEW_ACTIVATED:
				if (act.index >= 0 && act.index < detail_track_count) {
					// Play the playlist starting from selected track
					GFX_clearLayers(LAYER_SCROLLTEXT);
					PlayerModule_setResumePlaylistPath(playlists[current_playlist_index].path);
					PlayerModule_runWithPlaylist(screen, detail_tracks, detail_track_count, act.index);
					PlayerModule_setResumePlaylistPath(NULL);
					// On return, refresh and go back to detail
					refresh_detail();
					if (v->selected >= detail_track_count)
						v->selected = detail_track_count - 1;
					if (v->selected < 0)
						v->selected = 0;
					dirty = 1;
				}
				break;
			case LISTVIEW_BACK:
				GFX_clearLayers(LAYER_SCROLLTEXT);
				refresh_playlists(); // Refresh counts
				state = PLAYLIST_INTERNAL_LIST;
				dirty = 1;
				break;
			default:
				// Remove-track moved to the context menu (MENU tap)
				break;
			}
		}

		// Power management
		ModuleCommon_PWR_update(&dirty, &show_setting);

		// Render. The busy checks keep the dirty-flag loop redrawing while
		// the active view's pill glides or its marquee needs a main-surface
		// render.
		if (dirty ||
			(state == PLAYLIST_INTERNAL_LIST && UI_listViewBusy(PlaylistList_view())) ||
			(state == PLAYLIST_INTERNAL_DETAIL && UI_listViewBusy(PlaylistDetail_view()))) {
			// Bounds check: if current playlist was deleted externally, go back to list
			if (state == PLAYLIST_INTERNAL_DETAIL &&
				(current_playlist_index < 0 || current_playlist_index >= playlist_count)) {
				state = PLAYLIST_INTERNAL_LIST;
			}

			if (state == PLAYLIST_INTERNAL_LIST) {
				render_playlist_list(screen, show_setting, playlists, playlist_count);
			} else {
				render_playlist_detail(screen, show_setting, playlists[current_playlist_index].name,
									   detail_tracks, detail_track_count);
			}

			// Toast
			UI_renderToast(screen, playlist_toast_message, playlist_toast_time);

			GFX_flip(screen);
			dirty = 0;

			ModuleCommon_tickToast(playlist_toast_message, playlist_toast_time, &dirty);
		} else {
			// Idle marquee tick for the active list view (activate-after-
			// delay + steady GPU scroll happen here, not via dirty).
			if (state == PLAYLIST_INTERNAL_LIST)
				UI_listViewTickIdle(PlaylistList_view());
			else
				UI_listViewTickIdle(PlaylistDetail_view());
			GFX_sync();
		}
	}
}
