#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#if defined(PLATFORM_TG5050)
#include "../../tg5050/platform/platform.h"
#else
#include "../../tg5040/platform/platform.h"
#endif
#include "../common/api.h"
#include "../common/display_helper.h"
#include "../common/ui/ui_contextmenu.h"

extern uint32_t SDL_GetTicks(void);
#include "module_common.h"
#include "module_player.h"
#include "music_client.h"
#include "spectrum.h"
#include "browser.h"
#include "playlist.h"
#include "ui_music.h"
#include "../common/ui/ui_listview.h"
#include "ui_album_art.h"
#include "ui_main.h"
#include "../common/ui/ui_confirmdialog.h"
#include "lyrics.h"
#include "settings.h"
#include "add_to_playlist.h"
#include "../common/ui/ui_toast.h"
#include "resume.h"
#include "playlist_m3u.h"
#include "background.h"
#include "album_art.h"
#include "../common/ui/ui_keyboard.h"

// Music folder path
#define MUSIC_PATH SDCARD_PATH "/Music"

// Internal states
typedef enum {
	PLAYER_INTERNAL_BROWSER,
	PLAYER_INTERNAL_PLAYING
} PlayerInternalState;

// Module state
static BrowserContext browser = {0};
static bool shuffle_enabled = false;
static bool repeat_enabled = false;
static PlaylistContext playlist = {0};
static bool playlist_active = false;
static bool initialized = false;

// Context-menu item ids (browser page)
#define PLAYER_CTX_RENAME 1 // rename selected file/folder
#define PLAYER_CTX_DELETE 2 // delete selected file/folder (confirm)
#define PLAYER_CTX_ADD 3	// add selected file/folder to a playlist

// Delete confirmation state
static bool show_delete_confirm = false;
static bool delete_target_is_dir = false;
static char delete_target_path[512] = "";
static char delete_target_name[256] = "";

// Browser action toast (rename/delete feedback)
static char player_toast_message[128] = "";
static uint32_t player_toast_time = 0;

static void player_show_toast(const char* msg) {
	snprintf(player_toast_message, sizeof(player_toast_message), "%s", msg);
	player_toast_time = SDL_GetTicks();
}

// Screen off state (module-local)
static bool screen_off = false;

static char loaded_artwork_path[MUSIC_SERVICE_MAX_PATH] = "";
static char presentation_identity[MUSIC_SERVICE_MAX_PATH * 3 + MUSIC_SERVICE_MAX_TITLE + MUSIC_SERVICE_MAX_ARTIST + 16] = "";
static SDL_Surface* presentation_artwork;

static void clear_owner_presentation(void) {
	cleanup_album_art_background();
	album_art_clear();
	presentation_artwork = NULL;
	loaded_artwork_path[0] = '\0';
	Lyrics_clearGPU();
	Lyrics_clear();
}

static bool sync_owner_presentation(const MusicSnapshotWire* snapshot) {
	char identity[sizeof(presentation_identity)];
	bool changed;
	if (!snapshot->loaded)
		identity[0] = '\0';
	else
		snprintf(identity, sizeof(identity), "%d:%s:%s:%s:%s", snapshot->source,
				 snapshot->current_file, snapshot->artist, snapshot->title, snapshot->artwork_path);
	changed = strcmp(presentation_identity, identity) != 0;
	if (changed) {
		clear_owner_presentation();
		snprintf(presentation_identity, sizeof(presentation_identity), "%s", identity);
	}
	if (!snapshot->loaded)
		return changed;
	if (snapshot->artwork_path[0] && strcmp(loaded_artwork_path, snapshot->artwork_path) != 0) {
		album_art_load_path(snapshot->artwork_path);
		snprintf(loaded_artwork_path, sizeof(loaded_artwork_path), "%s", snapshot->artwork_path);
	} else if (!album_art_get() && (snapshot->artist[0] || snapshot->title[0]))
		album_art_fetch(snapshot->artist, snapshot->title);
	SDL_Surface* artwork = album_art_get();
	if (artwork != presentation_artwork) {
		cleanup_album_art_background();
		presentation_artwork = artwork;
		changed = true;
	}
	if (Settings_getLyricsEnabled())
		Lyrics_fetch(snapshot->artist, snapshot->title, snapshot->duration_ms / 1000);
	return changed;
}


// Queue identities are distinct: an M3U is not a recursively scanned folder.
static char resume_playlist_path[512] = "";
static char resume_folder_path[512] = "";

// Clear all player GPU overlay layers
static void clear_gpu_layers(void) {
	GFX_clearLayers(LAYER_SCROLLTEXT);
	PLAT_clearLayers(LAYER_SPECTRUM);
	PLAT_clearLayers(LAYER_PLAYTIME);
	PLAT_clearLayers(LAYER_LYRICS);
	PLAT_GPU_Flip();
}

// Helper to load directory. Resets the browser ListView so the new
// directory starts at row 0 (Browser_loadDirectory used to zero the
// context's selection/scroll; the ListView owns them now).
static void load_directory(const char* path) {
	Browser_loadDirectory(&browser, path, MUSIC_PATH);
	UI_listViewReset(MusicBrowser_view(), browser.entry_count, browser.entries);
}

// Initialize player module
static void init_player(void) {
	if (initialized)
		return;
	mkdir(MUSIC_PATH, 493);
	load_directory(MUSIC_PATH);
	initialized = true;
}

// Try to load and play a track, returns true on success
static bool try_load_and_play(const char* path) {
	int load_status = (playlist_active && resume_playlist_path[0])
						  ? MusicClient_loadPlaylist(resume_playlist_path, Playlist_getCurrentIndex(&playlist))
						  : MusicClient_load(path);
	if (load_status == 0) {
		(void)MusicClient_setRepeat(repeat_enabled);
		(void)MusicClient_setShuffle(shuffle_enabled);
		(void)MusicClient_play();
		const MusicSnapshotWire* snapshot = MusicClient_snapshot();

		sync_owner_presentation(snapshot);

		return true;
	}
	return false;
}

static bool sync_ui_to_owner(void) {
	const MusicSnapshotWire* snapshot = MusicClient_snapshot();
	bool presentation_changed = sync_owner_presentation(snapshot);
	if (snapshot->source != MUSIC_SOURCE_LOCAL || !snapshot->current_file[0])
		return presentation_changed;

	/* Reconstruct only the queue identity owned by the daemon. A folder and an
	 * M3U need different loaders; treating a folder as an M3U would make resume
	 * persistence and next/previous diverge after a UI reattach. */
	if (snapshot->queue_kind == MUSIC_QUEUE_M3U && snapshot->queue_path[0] &&
		strcmp(resume_playlist_path, snapshot->queue_path) != 0) {
		PlaylistTrack tracks[PLAYLIST_MAX_TRACKS];
		int count = 0;
		if (M3U_loadTracks(snapshot->queue_path, tracks, PLAYLIST_MAX_TRACKS, &count) == 0 && count > 0) {
			Playlist_free(&playlist);
			Playlist_init(&playlist);
			for (int i = 0; i < count; i++)
				playlist.tracks[i] = tracks[i];
			playlist.track_count = count;
			playlist.current_index = 0;
			playlist_active = true;
			snprintf(resume_playlist_path, sizeof(resume_playlist_path), "%s", snapshot->queue_path);
		}
	} else if (snapshot->queue_kind == MUSIC_QUEUE_FOLDER && snapshot->queue_path[0] &&
			   (strcmp(resume_folder_path, snapshot->queue_path) != 0 || !playlist_active)) {
		Playlist_free(&playlist);
		int count = Playlist_buildFromDirectory(&playlist, snapshot->queue_path, snapshot->current_file);
		if (count > 0) {
			playlist_active = true;
			resume_playlist_path[0] = '\0';
			snprintf(resume_folder_path, sizeof(resume_folder_path), "%s", snapshot->queue_path);
		}
	} else if (snapshot->queue_kind == MUSIC_QUEUE_NONE) {
		Playlist_free(&playlist);
		playlist_active = false;
		resume_playlist_path[0] = '\0';
		resume_folder_path[0] = '\0';
	}

	if (playlist_active) {
		for (int i = 0; i < playlist.track_count; i++) {
			if (strcmp(playlist.tracks[i].path, snapshot->current_file) == 0) {
				playlist.current_index = i;
				break;
			}
		}
	}
	for (int i = 0; i < browser.entry_count; i++) {
		if (!browser.entries[i].is_dir && strcmp(browser.entries[i].path, snapshot->current_file) == 0) {
			MusicBrowser_view()->selected = i;
			break;
		}
	}
	return presentation_changed;
}


// Start playback of a track (load + play + init spectrum)
static bool start_playback(const char* path) {
	// Stop any other background player before starting music playback
	if (Background_getActive() != BG_MUSIC) {
		Background_stopAll();
	}
	if (try_load_and_play(path)) {
		Spectrum_init();
		ModuleCommon_recordInputTime();
		ModuleCommon_setAutosleepDisabled(true);
		return true;
	}
	return false;
}

// Clean up playback state
static void cleanup_playback(bool quit_spectrum) {
	clear_owner_presentation();
	clear_gpu_layers();
	PlayTime_clear();
	if (quit_spectrum) {
		Spectrum_quit();
	}
	Playlist_free(&playlist);
	playlist_active = false;
	ModuleCommon_setAutosleepDisabled(false);
}

// Clean up playback UI only (audio keeps playing in background)
static void cleanup_playback_ui(void) {
	clear_gpu_layers();
	PlayTime_clear();
	Lyrics_clearGPU();
	Lyrics_clear();
	Spectrum_quit();
}

// Build a recursive directory queue in the owner, retaining the selected track.
static bool build_and_start_playlist(const char* dir_path, const char* start_file) {
	Playlist_free(&playlist);
	int track_count = Playlist_buildFromDirectory(&playlist, dir_path, start_file);
	const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
	if (!track)
		return false;
	playlist_active = true;
	if (Background_getActive() != BG_MUSIC)
		Background_stopAll();
	if (MusicClient_loadFolder(dir_path, track->path) != MUSIC_STATUS_OK)
		return false;
	snprintf(resume_folder_path, sizeof(resume_folder_path), "%s", dir_path);
	(void)MusicClient_setRepeat(repeat_enabled);
	(void)MusicClient_setShuffle(shuffle_enabled);
	if (MusicClient_play() != MUSIC_STATUS_OK)
		return false;
	sync_owner_presentation(MusicClient_snapshot());
	Spectrum_init();
	ModuleCommon_recordInputTime();
	ModuleCommon_setAutosleepDisabled(true);
	return true;
}

// Render delete confirmation dialog
static void render_delete_dialog(SDL_Surface* screen) {
	UI_renderConfirmDialog(screen, delete_target_is_dir ? "Delete Folder?" : "Delete File?",
						   delete_target_name);
	GFX_flip(screen);
}

// Recursively delete a file or directory tree. Returns 0 on success.
#define REMOVE_MAX_DEPTH 32
static int remove_recursive(const char* path, int depth) {
	if (depth > REMOVE_MAX_DEPTH) // guard against a pathological/looping tree
		return -1;

	struct stat st;
	if (lstat(path, &st) != 0)
		return -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);

	DIR* d = opendir(path);
	if (!d)
		return -1;
	int rc = 0;
	struct dirent* ent;
	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		char p[1024];
		int n = snprintf(p, sizeof(p), "%s/%s", path, ent->d_name);
		if (n < 0 || n >= (int)sizeof(p)) {
			rc = -1;
			continue;
		}
		if (remove_recursive(p, depth + 1) != 0)
			rc = -1;
	}
	closedir(d);
	if (rmdir(path) != 0)
		rc = -1;
	return rc;
}

// Rename the selected browser entry via the on-screen keyboard. Files keep
// their original extension unless the typed name already ends with it.
// May swap *screen_p on TG5050 display recovery.
static void rename_browser_entry(SDL_Surface** screen_p, FileEntry* entry) {
	char prompt[300];
	snprintf(prompt, sizeof(prompt), "Rename: %s", entry->name);
	char* newname = UIKeyboard_open(prompt);
	PAD_poll();
	PAD_reset();
	{
		SDL_Surface* ns = DisplayHelper_getReinitScreen();
		if (ns)
			*screen_p = ns;
	}
	if (!newname || !newname[0] || strchr(newname, '/')) {
		free(newname);
		return;
	}

	const char* ext = entry->is_dir ? NULL : strrchr(entry->name, '.');
	if (!ext)
		ext = "";
	size_t nl = strlen(newname), el = strlen(ext);
	bool has_ext = el > 0 && nl >= el && strcasecmp(newname + nl - el, ext) == 0;

	char new_path[1024];
	snprintf(new_path, sizeof(new_path), "%s/%s%s", browser.current_path,
			 newname, has_ext ? "" : ext);

	if (access(new_path, F_OK) == 0) {
		player_show_toast("Already exists");
	} else if (rename(entry->path, new_path) == 0) {
		player_show_toast(entry->is_dir ? "Folder renamed" : "File renamed");
		load_directory(browser.current_path);
		ListView* v = MusicBrowser_view();
		for (int i = 0; i < browser.entry_count; i++) {
			if (strcmp(browser.entries[i].path, new_path) == 0) {
				v->selected = i;
				break;
			}
		}
	} else {
		player_show_toast("Rename failed");
	}
	free(newname);
}

// Try to start playback from a browser entry (play-all or single file). Returns true on success.
static bool browser_play_entry(FileEntry* entry) {
	/* A browser selection starts a directory-owned queue. Do not reuse an
	 * earlier M3U identity merely because the detached owner is still local. */
	resume_playlist_path[0] = '\0';
	resume_folder_path[0] = '\0';
	if (entry->is_play_all)
		return build_and_start_playlist(entry->path, "");
	if (build_and_start_playlist(browser.current_path, entry->path))
		return true;
	playlist_active = false;
	return start_playback(entry->path);
}

// Handle input in browser state. Returns true if module should exit to menu.
static bool handle_browser_input(PlayerInternalState* state, bool* dirty) {
	ListView* v = MusicBrowser_view();

	// The ListView owns navigation; the module switches on actions.
	ListViewAction act = UI_listViewHandleInput(v);
	switch (act.type) {
	case LISTVIEW_BACK:
		if (strcmp(browser.current_path, MUSIC_PATH) != 0) {
			// Copy to a local first: load_directory → Browser_loadDirectory does
			// strncpy(ctx->current_path, path), and passing browser.current_path
			// directly makes src and dst the same buffer (restrict/aliasing UB).
			char parent[512];
			snprintf(parent, sizeof(parent), "%s", browser.current_path);
			char* last_slash = strrchr(parent, '/');
			if (last_slash) {
				*last_slash = '\0';
				load_directory(parent);
				*dirty = 1;
			}
		} else {
			GFX_clearLayers(LAYER_SCROLLTEXT);
			if (!Background_isPlaying()) {
				Spectrum_quit();
				Browser_freeEntries(&browser);
			}
			return true;
		}
		break;
	case LISTVIEW_ACTIVATED:
		if (act.index >= 0 && act.index < browser.entry_count) {
			FileEntry* entry = &browser.entries[act.index];
			if (entry->is_dir) {
				char path_copy[512];
				snprintf(path_copy, sizeof(path_copy), "%s", entry->path);
				load_directory(path_copy);
				*dirty = 1;
			} else if (browser_play_entry(entry)) {
				*state = PLAYER_INTERNAL_PLAYING;
				*dirty = 1;
			}
		}
		break;
	default:
		// Delete / add-to-playlist moved to the context menu (MENU tap)
		break;
	}

	return false;
}

// Handle input in playing state. Returns true when main loop should continue (skip render).
static bool handle_playing_input(SDL_Surface* screen, PlayerInternalState* state, bool* dirty) {
	// Handle screen off hint timeout
	if (ModuleCommon_isScreenOffHintActive()) {
		if (ModuleCommon_processScreenOffHintTimeout()) {
			screen_off = true;
			GFX_clear(screen);
			GFX_flip(screen);
		}
		MusicClient_update();
		GFX_sync();
		return true;
	}

	// Handle screen off mode
	if (screen_off) {
		// Wake screen with SELECT+A
		if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
			screen_off = false;
			PLAT_enableBacklight(1);
			ModuleCommon_recordInputTime();
			*dirty = 1;
		}
		MusicClient_update();
		ModuleCommon_setAutosleepDisabled(Background_isPlaying());

		GFX_sync();
		return true;
	}

	// Normal input handling
	if (PAD_anyPressed()) {
		ModuleCommon_recordInputTime();
	}

	if (PAD_justPressed(BTN_A)) {
		MusicClient_toggle();
		*dirty = 1;
	} else if (PAD_justPressed(BTN_B)) {
		cleanup_album_art_background();
		cleanup_playback_ui();
		Background_setActive(BG_MUSIC);
		*state = PLAYER_INTERNAL_BROWSER;
		*dirty = 1;
		return true; // Skip track-ended check to prevent auto-advance
	} else if (PAD_justRepeated(BTN_LEFT)) {
		MusicClient_seek(MusicClient_position() - 5000);
		*dirty = 1;
	} else if (PAD_justRepeated(BTN_RIGHT)) {
		MusicClient_seek(MusicClient_position() + 5000);
		*dirty = 1;
	} else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
		PlayerModule_prevTrack();
		*dirty = 1;
	} else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
		PlayerModule_nextTrack();
		*dirty = 1;
	} else if (PAD_justPressed(BTN_X)) {
		shuffle_enabled = !shuffle_enabled;
		(void)MusicClient_setShuffle(shuffle_enabled);
		*dirty = 1;
	} else if (PAD_justPressed(BTN_Y)) {
		repeat_enabled = !repeat_enabled;
		(void)MusicClient_setRepeat(repeat_enabled);
		*dirty = 1;
	} else if (PAD_justPressed(BTN_FN1) || PAD_justPressed(BTN_L2)) {
		Spectrum_cycleNext();
		*dirty = 1;
	} else if (PAD_justPressed(BTN_FN2) || PAD_justPressed(BTN_R2)) {
		Settings_toggleLyrics();
		if (!Settings_getLyricsEnabled()) {
			Lyrics_clear();
		} else {
			// Re-fetch lyrics from copied daemon metadata.
			const MusicSnapshotWire* snapshot = MusicClient_snapshot();
			Lyrics_fetch(snapshot->artist, snapshot->title, snapshot->duration_ms / 1000);
		}
		*dirty = 1;
	} else if (PAD_tappedSelect(SDL_GetTicks())) {
		ModuleCommon_startScreenOffHint();
		clear_gpu_layers();
		*dirty = 1;
	}

	// The owner advances EOF and persists resume position. Refresh only the
	// copied state used by this detached presentation.
	MusicClient_update();
	if (sync_ui_to_owner())
		*dirty = 1;
	if (!PlayerModule_isActive()) {
		cleanup_playback(true);
		*state = PLAYER_INTERNAL_BROWSER;
		return true;
	}

	// Auto screen-off after inactivity
	if (MusicClient_snapshot()->state == MUSIC_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
		clear_gpu_layers();
		*dirty = 1;
	}


	// Animate player GPU layers (skip if screen-off hint just activated)
	if (!ModuleCommon_isScreenOffHintActive()) {
		if (player_needs_scroll_refresh()) {
			player_animate_scroll();
		}
		if (player_title_scroll_needs_render())
			*dirty = 1;
		if (Spectrum_needsRefresh()) {
			Spectrum_renderGPU();
		}
		if (PlayTime_needsRefresh()) {
			PlayTime_renderGPU();
		}
		if (Lyrics_GPUneedsRefresh()) {
			Lyrics_renderGPU();
		}
	}

	return false;
}

ModuleExitReason PlayerModule_run(SDL_Surface* screen) {
	init_player();
	load_directory(browser.current_path[0] ? browser.current_path : MUSIC_PATH);

	PlayerInternalState state = PLAYER_INTERNAL_BROWSER;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	screen_off = false;
	ModuleCommon_resetScreenOffHint();
	ModuleCommon_recordInputTime();

	// Reclaim background music — re-enter playing state. The owner snapshot is
	// authoritative; rebuild the local presentation queue when the UI was
	// detached or the owner advanced it while no screen was open.
	MusicClient_update();
	sync_ui_to_owner();
	if (Background_getActive() == BG_MUSIC && PlayerModule_isActive()) {
		Background_setActive(BG_NONE);
		repeat_enabled = MusicClient_snapshot()->repeat != 0;
		shuffle_enabled = MusicClient_snapshot()->shuffle != 0;
		Spectrum_init();
		ModuleCommon_setAutosleepDisabled(true);
		state = PLAYER_INTERNAL_PLAYING;
	}

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Handle add-to-playlist dialog overlay
		if (AddToPlaylist_isActive()) {
			if (AddToPlaylist_handleInput()) {
				// TG5050: keyboard in add-to-playlist may have triggered display recovery
				{
					SDL_Surface* ns = DisplayHelper_getReinitScreen();
					if (ns)
						screen = ns;
				}
				// Dialog closed — skip rest of input to avoid double-handling buttons
				dirty = 1;
				continue;
			}
			// Still active, render dialog (covers entire screen)
			AddToPlaylist_render(screen);
			GFX_flip(screen);
			GFX_sync();
			continue;
		}

		// Handle delete confirmation dialog (module-specific)
		if (show_delete_confirm) {
			if (PAD_justPressed(BTN_A)) {
				int ok = delete_target_is_dir ? remove_recursive(delete_target_path, 0)
											  : unlink(delete_target_path);
				if (ok == 0) {
					player_show_toast(delete_target_is_dir ? "Folder deleted" : "File deleted");
					// Capture the cursor BEFORE load_directory (it resets
					// selection to 0), then keep it near the deleted row — the
					// next entry has shifted into that slot.
					ListView* v = MusicBrowser_view();
					int prev_selected = v->selected;
					load_directory(browser.current_path);
					if (browser.entry_count > 0)
						v->selected = prev_selected < browser.entry_count
										  ? prev_selected
										  : browser.entry_count - 1;
					else
						v->selected = 0;
				} else {
					player_show_toast("Delete failed");
				}
			}
			if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) {
				delete_target_path[0] = '\0';
				delete_target_name[0] = '\0';
				delete_target_is_dir = false;
				show_delete_confirm = false;
				dirty = 1;
				continue;
			}
			// Render delete dialog
			render_delete_dialog(screen);
			GFX_sync();
			continue;
		}

		// Handle global input (skip if screen off or hint active)
		if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
			int app_state_for_help = (state == PLAYER_INTERNAL_BROWSER) ? 1 : 2; // STATE_BROWSER=1, STATE_PLAYING=2

			// Context menu for the browser page: acts on the selected entry
			// (".." and "Play All" only get Quit App).
			ContextMenuItem ctx_items[4];
			int ctx_count = 0;
			if (state == PLAYER_INTERNAL_BROWSER) {
				ListView* v = MusicBrowser_view();
				if (v->selected >= 0 && v->selected < browser.entry_count) {
					FileEntry* e = &browser.entries[v->selected];
					bool is_parent = e->is_dir && strcmp(e->name, "..") == 0;
					if (!e->is_play_all && !is_parent) {
						ModuleCommon_ctxAdd(ctx_items, &ctx_count,
											e->is_dir ? "Rename Folder" : "Rename File", PLAYER_CTX_RENAME);
						ModuleCommon_ctxAdd(ctx_items, &ctx_count,
											e->is_dir ? "Delete Folder" : "Delete File", PLAYER_CTX_DELETE);
						ModuleCommon_ctxAdd(ctx_items, &ctx_count,
											e->is_dir ? "Add Folder to Playlist" : "Add to Playlist", PLAYER_CTX_ADD);
					}
				}
				ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);
			}

			GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help,
																	  ctx_items, ctx_count);
			if (global.should_quit) {
				cleanup_playback(true);
				Browser_freeEntries(&browser);
				return MODULE_EXIT_QUIT;
			}
			if (global.context_id > 0 && state == PLAYER_INTERNAL_BROWSER) {
				ListView* v = MusicBrowser_view();
				if (v->selected >= 0 && v->selected < browser.entry_count) {
					FileEntry* entry = &browser.entries[v->selected];
					switch (global.context_id) {
					case PLAYER_CTX_RENAME:
						rename_browser_entry(&screen, entry);
						break;
					case PLAYER_CTX_DELETE:
						snprintf(delete_target_path, sizeof(delete_target_path), "%s", entry->path);
						snprintf(delete_target_name, sizeof(delete_target_name), "%s", entry->name);
						delete_target_is_dir = entry->is_dir;
						show_delete_confirm = true;
						GFX_clearLayers(LAYER_SCROLLTEXT);
						break;
					case PLAYER_CTX_ADD:
						if (entry->is_dir)
							AddToPlaylist_openFolder(entry->path, entry->name);
						else
							AddToPlaylist_open(entry->path, entry->name);
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
		}

		if (state == PLAYER_INTERNAL_BROWSER) {
			if (handle_browser_input(&state, &dirty)) {
				return MODULE_EXIT_TO_MENU;
			}
		} else if (state == PLAYER_INTERNAL_PLAYING) {
			if (handle_playing_input(screen, &state, &dirty)) {
				continue;
			}
		}

		// Handle power management
		if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
			ModuleCommon_PWR_update(&dirty, &show_setting);
		}

		// Auto-clear toast after duration; force re-render while visible
		const char* atp_toast = AddToPlaylist_getToastMessage();
		if (atp_toast && atp_toast[0]) {
			if (SDL_GetTicks() - AddToPlaylist_getToastTime() > TOAST_DURATION) {
				AddToPlaylist_clearToast();
				UI_clearToast();
			}
			dirty = 1;
		}

		// Render. The busy check keeps the dirty-flag loop redrawing while
		// the browser view's pill glides or its marquee needs a main-surface
		// render.
		if ((dirty || (state == PLAYER_INTERNAL_BROWSER && UI_listViewBusy(MusicBrowser_view()))) && !screen_off) {
			if (ModuleCommon_isScreenOffHintActive()) {
				GFX_clear(screen);
				render_screen_off_hint(screen);
			} else if (state == PLAYER_INTERNAL_BROWSER) {
				render_browser(screen, show_setting, &browser);
			} else {
				int pl_track = playlist_active ? Playlist_getCurrentIndex(&playlist) + 1 : 0;
				int pl_total = playlist_active ? Playlist_getCount(&playlist) : 0;
				render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
			}

			// Show add-to-playlist toast (if still active after auto-clear check)
			atp_toast = AddToPlaylist_getToastMessage();
			if (atp_toast && atp_toast[0]) {
				UI_renderToast(screen, atp_toast, AddToPlaylist_getToastTime());
			}

			// Browser action toast (rename/delete feedback)
			if (player_toast_message[0]) {
				UI_renderToast(screen, player_toast_message, player_toast_time);
			}

			GFX_flip(screen);
			dirty = 0;

			ModuleCommon_tickToast(player_toast_message, player_toast_time, &dirty);
		} else if (!screen_off) {
			// Idle marquee tick for the browser view (activate-after-delay +
			// steady GPU scroll happen here, not via dirty).
			if (state == PLAYER_INTERNAL_BROWSER)
				UI_listViewTickIdle(MusicBrowser_view());
			GFX_sync();
		}
	}
}

// Check if music player module is active
bool PlayerModule_isActive(void) {
	int state = MusicClient_snapshot()->state;
	return (state == MUSIC_STATE_PLAYING || state == MUSIC_STATE_PAUSED);
}

// Playback navigation belongs to the daemon so recursive queue identity survives.
void PlayerModule_nextTrack(void) {
	if (MusicClient_next() == MUSIC_STATUS_OK)
		sync_ui_to_owner();
}

void PlayerModule_prevTrack(void) {
	if (MusicClient_previous() == MUSIC_STATUS_OK)
		sync_ui_to_owner();
}

// Run the player directly with a pre-built playlist (from PlaylistModule)
ModuleExitReason PlayerModule_runWithPlaylist(SDL_Surface* screen,
											  PlaylistTrack* tracks,
											  int track_count,
											  int start_index) {
	if (!tracks || track_count <= 0)
		return MODULE_EXIT_TO_MENU;

	// Set up the playlist context
	Playlist_free(&playlist);
	Playlist_init(&playlist);
	if (!playlist.tracks)
		return MODULE_EXIT_TO_MENU;
	int copied = 0;
	for (int i = 0; i < track_count && i < PLAYLIST_MAX_TRACKS; i++) {
		playlist.tracks[i] = tracks[i];
		copied++;
	}
	// Clamp the count to what was actually copied (a future caller with >
	// PLAYLIST_MAX_TRACKS would otherwise let Playlist_getTrack read past the
	// copied region), and keep start_index in range.
	playlist.track_count = copied;
	playlist.current_index = (start_index >= 0 && start_index < copied) ? start_index : 0;
	playlist_active = true;

	// Start playback
	const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
	if (!track || !start_playback(track->path)) {
		Playlist_free(&playlist);
		playlist_active = false;
		return MODULE_EXIT_TO_MENU;
	}

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	screen_off = false;
	ModuleCommon_resetScreenOffHint();
	ModuleCommon_recordInputTime();

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Handle add-to-playlist dialog overlay
		if (AddToPlaylist_isActive()) {
			if (AddToPlaylist_handleInput()) {
				// TG5050: keyboard in add-to-playlist may have triggered display recovery
				{
					SDL_Surface* ns = DisplayHelper_getReinitScreen();
					if (ns)
						screen = ns;
				}
				// Dialog closed — skip rest of input to avoid double-handling buttons
				dirty = 1;
				continue;
			}
			// Dialog covers entire screen, no need to render underlying content
			AddToPlaylist_render(screen);
			GFX_flip(screen);
			GFX_sync();
			continue;
		}

		// Handle global input (skip if screen off or hint active)
		if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
			GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 2, NULL, 0); // STATE_PLAYING=2
			if (global.should_quit) {
				cleanup_album_art_background();
				cleanup_playback(true);
				return MODULE_EXIT_QUIT;
			}
			if (global.input_consumed) {
				if (global.dirty)
					dirty = 1;
				GFX_sync();
				continue;
			}
		}

		// Handle screen off hint timeout
		if (ModuleCommon_isScreenOffHintActive()) {
			if (ModuleCommon_processScreenOffHintTimeout()) {
				screen_off = true;
				GFX_clear(screen);
				GFX_flip(screen);
			}
			MusicClient_update();
			GFX_sync();
			continue;
		}

		// Handle screen off mode
		if (screen_off) {
			if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
				screen_off = false;
				PLAT_enableBacklight(1);
				ModuleCommon_recordInputTime();
				dirty = 1;
			}
			MusicClient_update();
			ModuleCommon_setAutosleepDisabled(Background_isPlaying());

			GFX_sync();
			continue;
		}

		// Normal input handling
		if (PAD_anyPressed()) {
			ModuleCommon_recordInputTime();
		}

		if (PAD_justPressed(BTN_A)) {
			MusicClient_toggle();
			dirty = 1;
		} else if (PAD_justPressed(BTN_B)) {
			cleanup_album_art_background();
			cleanup_playback_ui();
			Background_setActive(BG_MUSIC);
			return MODULE_EXIT_TO_MENU;
		} else if (PAD_justRepeated(BTN_LEFT)) {
			MusicClient_seek(MusicClient_position() - 5000);
			dirty = 1;
		} else if (PAD_justRepeated(BTN_RIGHT)) {
			MusicClient_seek(MusicClient_position() + 5000);
			dirty = 1;
		} else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
			PlayerModule_prevTrack();
			dirty = 1;
		} else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
			PlayerModule_nextTrack();
			dirty = 1;
		} else if (PAD_justPressed(BTN_X)) {
			shuffle_enabled = !shuffle_enabled;
			(void)MusicClient_setShuffle(shuffle_enabled);
			dirty = 1;
		} else if (PAD_justPressed(BTN_Y)) {
			repeat_enabled = !repeat_enabled;
			(void)MusicClient_setRepeat(repeat_enabled);
			dirty = 1;
		} else if (PAD_justPressed(BTN_FN1) || PAD_justPressed(BTN_L2)) {
			Spectrum_cycleNext();
			dirty = 1;
		} else if (PAD_justPressed(BTN_FN2) || PAD_justPressed(BTN_R2)) {
			Settings_toggleLyrics();
			if (!Settings_getLyricsEnabled()) {
				Lyrics_clear();
			} else {
				const MusicSnapshotWire* snapshot = MusicClient_snapshot();
				Lyrics_fetch(snapshot->artist, snapshot->title, snapshot->duration_ms / 1000);
			}
			dirty = 1;
		} else if (PAD_tappedSelect(SDL_GetTicks())) {
			ModuleCommon_startScreenOffHint();
			clear_gpu_layers();
			dirty = 1;
		}

		// The owner advances EOF and persists resume position. Refresh its copied
		// state without duplicating queue policy in this UI loop.
		MusicClient_update();
		if (sync_ui_to_owner())
			dirty = 1;
		if (!PlayerModule_isActive()) {
			cleanup_playback(true);
			return MODULE_EXIT_TO_MENU;
		}

		// Auto screen-off after inactivity
		if (MusicClient_snapshot()->state == MUSIC_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
			clear_gpu_layers();
			dirty = 1;
		}

		// Animate player GPU layers
		if (!ModuleCommon_isScreenOffHintActive()) {
			if (player_needs_scroll_refresh()) {
				player_animate_scroll();
			}
			if (player_title_scroll_needs_render())
				dirty = 1;
			if (Spectrum_needsRefresh()) {
				Spectrum_renderGPU();
			}
			if (PlayTime_needsRefresh()) {
				PlayTime_renderGPU();
			}
			if (Lyrics_GPUneedsRefresh()) {
				Lyrics_renderGPU();
			}
		}

		// Handle power management
		if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
			ModuleCommon_PWR_update(&dirty, &show_setting);
		}

		// Auto-clear toast after duration; force re-render while visible
		const char* atp_toast = AddToPlaylist_getToastMessage();
		if (atp_toast && atp_toast[0]) {
			if (SDL_GetTicks() - AddToPlaylist_getToastTime() > TOAST_DURATION) {
				AddToPlaylist_clearToast();
				UI_clearToast();
			}
			dirty = 1;
		}

		// Render
		if (dirty && !screen_off) {
			if (ModuleCommon_isScreenOffHintActive()) {
				GFX_clear(screen);
				render_screen_off_hint(screen);
			} else {
				int pl_track = Playlist_getCurrentIndex(&playlist) + 1;
				int pl_total = Playlist_getCount(&playlist);
				render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
			}

			// Show add-to-playlist toast (if still active after auto-clear check)
			atp_toast = AddToPlaylist_getToastMessage();
			if (atp_toast && atp_toast[0]) {
				UI_renderToast(screen, atp_toast, AddToPlaylist_getToastTime());
			}

			GFX_flip(screen);
			dirty = 0;
		} else if (!screen_off) {
			GFX_sync();
		}
	}
}

// Set the M3U playlist path for resume tracking (call before runWithPlaylist)
void PlayerModule_setResumePlaylistPath(const char* m3u_path) {
	snprintf(resume_playlist_path, sizeof(resume_playlist_path), "%s", m3u_path ? m3u_path : "");
	resume_folder_path[0] = '\0';
}

// Run player restoring a saved resume state
ModuleExitReason PlayerModule_runResume(SDL_Surface* screen, const ResumeState* resume) {
	if (!resume)
		return MODULE_EXIT_TO_MENU;

	/* A daemon restart may finish restoring between the menu snapshot and this
	 * action. Attach to that owner instead of loading/playing/seeking a second
	 * decoder instance. */
	MusicClient_update();
	const MusicSnapshotWire* owner = MusicClient_snapshot();
	if (owner->source == MUSIC_SOURCE_LOCAL && owner->current_file[0] &&
		strcmp(owner->current_file, resume->track_path) == 0)
		return PlayerModule_run(screen);

	if (resume->type == RESUME_TYPE_FILES) {
		resume_playlist_path[0] = '\0';
		resume_folder_path[0] = '\0';
		// Initialize browser with saved folder
		init_player();
		load_directory(resume->folder_path);

		if (!build_and_start_playlist(resume->folder_path, resume->track_path)) {
			cleanup_playback(false);
			return MODULE_EXIT_TO_MENU;
		}
		const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);

		// Seek to saved position
		if (resume->position_ms > 0) {
			MusicClient_seek(resume->position_ms);
		}

		// Relocate the browser view's cursor to the current track for display
		// (load_directory above already Reset the view for the new content)
		for (int i = 0; i < browser.entry_count; i++) {
			if (strcmp(browser.entries[i].path, track->path) == 0) {
				MusicBrowser_view()->selected = i;
				break;
			}
		}

		// Use the shared playing loop via handle_playing_input
		bool dirty = true;
		IndicatorType show_setting = INDICATOR_NONE;
		screen_off = false;
		ModuleCommon_resetScreenOffHint();
		ModuleCommon_recordInputTime();
		PlayerInternalState state = PLAYER_INTERNAL_PLAYING;

		while (1) {
			GFX_startFrame();
			PAD_poll();

			// Handle add-to-playlist dialog overlay
			if (AddToPlaylist_isActive()) {
				if (AddToPlaylist_handleInput()) {
					// TG5050: keyboard in add-to-playlist may have triggered display recovery
					{
						SDL_Surface* ns = DisplayHelper_getReinitScreen();
						if (ns)
							screen = ns;
					}
					dirty = 1;
					continue;
				}
				AddToPlaylist_render(screen);
				GFX_flip(screen);
				GFX_sync();
				continue;
			}

			// Handle global input
			if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
				GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 2, NULL, 0);
				if (global.should_quit) {
					cleanup_album_art_background();
					cleanup_playback(true);
					return MODULE_EXIT_QUIT;
				}
				if (global.input_consumed) {
					if (global.dirty)
						dirty = 1;
					GFX_sync();
					continue;
				}
			}

			// Delegate to shared playing input handler
			if (handle_playing_input(screen, &state, &dirty)) {
				// If state left playing (BTN_B or all tracks ended), return to menu immediately
				// Don't continue the loop or handle_playing_input will re-trigger track-ended logic
				if (state != PLAYER_INTERNAL_PLAYING) {
					return MODULE_EXIT_TO_MENU;
				}
				continue;
			}

			// If state left playing, return to menu
			if (state != PLAYER_INTERNAL_PLAYING) {
				return MODULE_EXIT_TO_MENU;
			}

			// Handle power management
			if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
				ModuleCommon_PWR_update(&dirty, &show_setting);
			}

			// Auto-clear toast
			const char* atp_toast = AddToPlaylist_getToastMessage();
			if (atp_toast && atp_toast[0]) {
				if (SDL_GetTicks() - AddToPlaylist_getToastTime() > TOAST_DURATION) {
					AddToPlaylist_clearToast();
					UI_clearToast();
				}
				dirty = 1;
			}

			// Render
			if (dirty && !screen_off) {
				if (ModuleCommon_isScreenOffHintActive()) {
					GFX_clear(screen);
					render_screen_off_hint(screen);
				} else {
					int pl_track = Playlist_getCurrentIndex(&playlist) + 1;
					int pl_total = Playlist_getCount(&playlist);
					render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
				}

				atp_toast = AddToPlaylist_getToastMessage();
				if (atp_toast && atp_toast[0]) {
					UI_renderToast(screen, atp_toast, AddToPlaylist_getToastTime());
				}

				GFX_flip(screen);
				dirty = 0;
			} else if (!screen_off) {
				GFX_sync();
			}
		}

	} else if (resume->type == RESUME_TYPE_PLAYLIST) {
		// Load the M3U playlist tracks
		PlaylistTrack m3u_tracks[PLAYLIST_MAX_TRACKS];
		int m3u_count = 0;
		if (M3U_loadTracks(resume->playlist_path, m3u_tracks, PLAYLIST_MAX_TRACKS, &m3u_count) != 0 || m3u_count <= 0) {
			return MODULE_EXIT_TO_MENU;
		}

		// Find the track index in the loaded playlist
		int start_idx = 0;
		for (int i = 0; i < m3u_count; i++) {
			if (strcmp(m3u_tracks[i].path, resume->track_path) == 0) {
				start_idx = i;
				break;
			}
		}

		// Set resume playlist path so the playing loop saves correctly
		PlayerModule_setResumePlaylistPath(resume->playlist_path);

		// Run with the playlist
		ModuleExitReason reason = PlayerModule_runWithPlaylist(screen, m3u_tracks, m3u_count, start_idx);

		resume_playlist_path[0] = '\0';
		resume_folder_path[0] = '\0';
		return reason;
	}

	return MODULE_EXIT_TO_MENU;
}

// Background playback belongs to musicplayerd; the UI only refreshes its copy.
void PlayerModule_backgroundTick(void) {
	MusicClient_update();
}
