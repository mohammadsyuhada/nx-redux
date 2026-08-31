#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <msettings.h>

#include "api.h"
#include "iv_defines.h"
#include "iv_browser.h"
#include "iv_fileops.h"
#include "iv_loader.h"
#include "iv_viewer.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_contextmenu.h"
#include "ui_draw.h"
#include "ui_emptystate.h"
#include "ui_icons.h"
#include "ui_keyboard.h"
#include "ui_listview.h"
#include "ui_menubar.h"
#include "ui_splash.h"
#include "ui_toast.h"

#define ICON_FOLDER RES_PATH "/icon-folder.png"
#define ICON_IMAGE RES_PATH "/icon-image.png"

#define IV_CTX_RENAME 1
#define IV_CTX_DELETE 2

typedef enum { SCREEN_BROWSER,
			   SCREEN_VIEWER } AppScreen;

static volatile sig_atomic_t quit = false;
static SDL_Surface* screen;
static AppScreen app_screen = SCREEN_BROWSER;
static ImageBrowserContext browser;
static ListView browser_view;
static char browser_row_buf[256];
static char browser_toast[64];
static uint32_t browser_toast_time;
static SDL_Surface* icon_folder;
static SDL_Surface* icon_folder_inv;
static SDL_Surface* icon_image;
static SDL_Surface* icon_image_inv;

// Remembers which row the browser context menu (MENU tap) was opened for,
// since the ListView is not fed input while the menu is up.
static int ctx_target_index = -1;

// Preview pane geometry, computed once from the screen size in main().
static int pane_x, pane_y, pane_w, pane_h;

// Preview debounce/result state. preview_want is the selected file's path
// ("" for a dir/empty selection); preview_requested/preview_have/preview_error
// track, by path comparison, what's been asked for, what's on screen, and
// what most recently failed, so a stale result for an old selection is never
// drawn.
static char preview_want[512];
static uint32_t preview_want_tick;
static char preview_requested[512];
static char preview_have[512];
static char preview_error[512];
static SDL_Surface* preview_surf;
static int preview_ow, preview_oh;

static void sigHandler(int sig) {
	if (sig == SIGINT || sig == SIGTERM)
		quit = true;
}

// Keeps `message`'s GPU toast layer in sync with its lifetime: forces a
// redraw every frame it's visible, then once more on the expiry edge
// (clearing the message) so the next render call actually wipes the layer -
// otherwise UI_renderToast only re-clears when called again, and nothing
// would call it once the loop goes idle. Mirrors ModuleCommon_tickToast in
// workspace/all/mediaplayer/module_common.c.
static void tick_toast(char* message, uint32_t toast_time, bool* dirty) {
	if (message[0] == '\0')
		return;
	if (SDL_GetTicks() - toast_time < TOAST_DURATION) {
		*dirty = true;
	} else {
		message[0] = '\0';
		*dirty = true;
	}
}

static void load_image_directory(const char* path) {
	ImageBrowser_loadDirectory(&browser, path, IMAGES_ROOT);
	UI_listViewReset(&browser_view, browser.entry_count, browser.entries);
}

// Row provider: files show without extension; dirs show the folder icon
// with no brackets, or fall back to [Name] with no icon if the asset failed
// to load.
static void browser_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	ImageBrowserContext* b = ctx;
	ImageEntry* e = &b->entries[i];
	if (e->is_dir) {
		if (icon_folder) {
			snprintf(browser_row_buf, sizeof(browser_row_buf), "%s", e->name);
			out->icon = selected ? icon_folder : icon_folder_inv;
		} else {
			snprintf(browser_row_buf, sizeof(browser_row_buf), "[%s]", e->name);
		}
	} else {
		ImageBrowser_getDisplayName(e->name, browser_row_buf, sizeof(browser_row_buf));
		if (icon_folder)
			out->icon = icon_image ? (selected ? icon_image : icon_image_inv) : Icons_getEmpty(selected);
	}
	out->label = browser_row_buf;
}

// Format a file size for the preview metadata line: "245 KB" below 1 MB,
// "1.2 MB" at or above.
static void format_size(long bytes, char* out, size_t out_len) {
	if (bytes >= 1024 * 1024)
		snprintf(out, out_len, "%.1f MB", bytes / (1024.0 * 1024.0));
	else
		snprintf(out, out_len, "%ld KB", bytes / 1024);
}

// Preview pane for the selected file: the async-loaded preview once it
// matches the selection, a dimmed placeholder while it's loading, or
// "Can't preview" on a load error. Draws nothing for a dir/empty selection.
static void render_preview_pane(void) {
	if (browser_view.selected < 0 || browser_view.selected >= browser.entry_count)
		return;
	ImageEntry* e = &browser.entries[browser_view.selected];
	if (e->is_dir)
		return;

	int box_h = pane_h - SCALE1(24); // metadata line reserved below the image
	if (preview_surf && preview_have[0] && strcmp(preview_have, e->path) == 0) {
		int img_x = pane_x + (pane_w - preview_surf->w) / 2;
		int img_y = pane_y + (box_h - preview_surf->h) / 2;
		SDL_BlitSurface(preview_surf, NULL, screen, &(SDL_Rect){img_x, img_y});

		char size_buf[32];
		format_size(e->size_bytes, size_buf, sizeof(size_buf));
		char meta[64];
		snprintf(meta, sizeof(meta), "%dx%d · %s", preview_ow, preview_oh, size_buf);
		SDL_Surface* meta_surf = GFX_renderText(font.small, meta, COLOR_GRAY);
		if (meta_surf) {
			SDL_Rect dst = {pane_x + (pane_w - meta_surf->w) / 2,
							pane_y + box_h + (SCALE1(24) - meta_surf->h) / 2};
			SDL_BlitSurface(meta_surf, NULL, screen, &dst);
			SDL_FreeSurface(meta_surf);
		}
		return;
	}

	// Loading or errored: dimmed placeholder matching the unselected list
	// pill background.
	UI_fillRoundedRect(screen, pane_x, pane_y, pane_w, box_h, SCALE1(7), THEME_COLOR2);
	if (preview_error[0] && strcmp(preview_error, e->path) == 0) {
		SDL_Surface* err = GFX_renderText(font.small, "Can't preview", COLOR_WHITE);
		if (err) {
			SDL_Rect dst = {pane_x + (pane_w - err->w) / 2, pane_y + (box_h - err->h) / 2};
			SDL_BlitSurface(err, NULL, screen, &dst);
			SDL_FreeSurface(err);
		}
	}
}

static void render_browser(void) {
	GFX_clear(screen);

	const char* title = "Images";
	if (strcmp(browser.current_path, IMAGES_ROOT) != 0) {
		const char* slash = strrchr(browser.current_path, '/');
		if (slash && slash[1] != '\0')
			title = slash + 1;
	}
	UI_renderMenuBar(screen, title);

	ListView* v = &browser_view;
	v->title = NULL;
	v->font = font.medium;
	v->count = browser.entry_count;
	v->get_row = browser_get_row;
	v->ctx = &browser;
	v->list_id = (const void*)browser.entries;
	v->empty_title = "No images found";
	v->empty_subtitle = "Screenshots you take will appear here";
	v->max_width_override = (int)(screen->w * 0.55); // leave room for the preview pane
	static char* hints[] = {"B", "BACK", "A", "OPEN", NULL};
	v->hint_pairs = hints;
	UI_listViewRender(v, screen);

	render_preview_pane();

	// Unconditional: UI_renderToast clears its GPU layer itself when the
	// message is empty or expired, which is what actually makes the toast
	// disappear once tick_toast() clears the message on the expiry edge.
	UI_renderToast(screen, browser_toast, browser_toast_time);

	if (ContextMenu_isOpen())
		ContextMenu_render(screen);
}

// Display name for the context-menu prompt/title: extension-stripped for
// files (rename only ever touches the base name), full name for dirs.
static void ctx_display_name(const ImageEntry* e, char* out, int out_sz) {
	if (e->is_dir)
		snprintf(out, out_sz, "%s", e->name);
	else
		ImageBrowser_getDisplayName(e->name, out, out_sz);
}

// Rename flow for the MENU > Rename item: blocking keyboard prompt, then the
// matching fileops call. "Name already used" is distinguished from any other
// failure via the access(out_path, F_OK) contract documented in
// iv_fileops.h (do_rename there writes out_path with the colliding path on
// that specific refusal, and leaves it untouched otherwise).
static void handle_ctx_rename(ImageEntry* e) {
	char disp[256];
	ctx_display_name(e, disp, sizeof(disp));
	char prompt[300];
	snprintf(prompt, sizeof(prompt), "Rename \"%s\"", disp);

	char* newname = UIKeyboard_open(prompt);
	if (!newname || !newname[0]) {
		free(newname);
		return;
	}

	char out_path[512] = "";
	bool ok = e->is_dir
				  ? IvFileops_renameDir(e->path, newname, out_path, sizeof(out_path))
				  : IvFileops_renameFileKeepExt(e->path, newname, out_path, sizeof(out_path));
	free(newname);

	if (ok) {
		char path_copy[512];
		snprintf(path_copy, sizeof(path_copy), "%s", browser.current_path);
		load_image_directory(path_copy);
		for (int i = 0; i < browser.entry_count; i++) {
			if (strcmp(browser.entries[i].path, out_path) == 0) {
				browser_view.selected = i;
				break;
			}
		}
	} else {
		const char* msg = (out_path[0] && access(out_path, F_OK) == 0)
							  ? "Name already used"
							  : "Rename failed";
		snprintf(browser_toast, sizeof(browser_toast), "%s", msg);
		browser_toast_time = SDL_GetTicks();
	}
}

// Delete flow for the MENU > Delete item: blocking confirm modal, then the
// matching fileops call. Per the brief, the listing is only reloaded on
// success; a failed delete just toasts (deleteTree's own best-effort
// continuation means a partial failure is rare and not worth reloading for).
static void handle_ctx_delete(ImageEntry* e, int target_index) {
	char disp[256];
	ctx_display_name(e, disp, sizeof(disp));
	char title[300];
	snprintf(title, sizeof(title), "Delete \"%s\"?", disp);

	bool confirmed = UI_confirmModal(
		screen, title,
		e->is_dir ? "The folder and everything inside it will be deleted" : NULL,
		NULL, true, true);
	if (!confirmed)
		return;

	bool ok = e->is_dir ? IvFileops_deleteTree(e->path) : IvFileops_deleteFile(e->path);
	if (ok) {
		char path_copy[512];
		snprintf(path_copy, sizeof(path_copy), "%s", browser.current_path);
		load_image_directory(path_copy);
		if (browser.entry_count > 0)
			browser_view.selected = target_index < browser.entry_count
										? target_index
										: browser.entry_count - 1;
	} else {
		snprintf(browser_toast, sizeof(browser_toast), "Delete failed");
		browser_toast_time = SDL_GetTicks();
	}
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	screen = GFX_init(MODE_MAIN);
	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	UI_showSplashScreen(screen, "Image Viewer");

	InitSettings();
	PAD_init();
	PWR_init();
	UI_loadIconPair(ICON_FOLDER, &icon_folder, &icon_folder_inv);
	UI_loadIconPair(ICON_IMAGE, &icon_image, &icon_image_inv);
	UI_initEmptyIcon();

	pane_x = (int)(screen->w * 0.58);
	pane_w = screen->w - pane_x - SCALE1(BUTTON_MARGIN * 2);
	pane_y = SCALE1(PILL_SIZE) + SCALE1(BUTTON_MARGIN);
	pane_h = screen->h - pane_y - SCALE1(PILL_SIZE);
	IvLoader_init(screen, pane_w, pane_h - SCALE1(24)); // 24: reserved for the metadata line

	signal(SIGINT, sigHandler);
	signal(SIGTERM, sigHandler);

	mkdir(IMAGES_ROOT, 0755);
	load_image_directory(IMAGES_ROOT);

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		if (app_screen == SCREEN_BROWSER) {
			// The context menu (opened below on a MENU tap) consumes input
			// on its own while up; the ListView gets none of it, so it can't
			// see the row move out from under the menu.
			if (ContextMenu_isOpen()) {
				ContextMenuResult cmr = ContextMenu_handleInput();
				if (cmr.action == CONTEXTMENU_SELECTED) {
					if (ctx_target_index >= 0 && ctx_target_index < browser.entry_count) {
						ImageEntry* e = &browser.entries[ctx_target_index];
						if (cmr.id == IV_CTX_RENAME)
							handle_ctx_rename(e);
						else if (cmr.id == IV_CTX_DELETE)
							handle_ctx_delete(e, ctx_target_index);
					}
					dirty = true;
				} else if (cmr.action == CONTEXTMENU_CANCEL) {
					dirty = true;
				}
			} else {
				ListViewAction act = UI_listViewHandleInput(&browser_view);
				switch (act.type) {
				case LISTVIEW_BACK:
					if (strcmp(browser.current_path, IMAGES_ROOT) != 0) {
						// Copy to a local first: load_image_directory -> ImageBrowser_loadDirectory
						// does strncpy(ctx->current_path, path, ...), and passing browser.current_path
						// directly makes src and dst the same buffer (restrict/aliasing UB).
						char parent[512];
						snprintf(parent, sizeof(parent), "%s", browser.current_path);
						char* last_slash = strrchr(parent, '/');
						if (last_slash && last_slash != parent) {
							*last_slash = '\0';
							load_image_directory(parent);
						} else {
							load_image_directory(IMAGES_ROOT);
						}
						GFX_clearLayers(LAYER_SCROLLTEXT);
						dirty = true;
					} else {
						quit = true;
					}
					break;
				case LISTVIEW_ACTIVATED:
					if (act.index >= 0 && act.index < browser.entry_count) {
						ImageEntry* e = &browser.entries[act.index];
						if (e->is_dir) {
							char path_copy[512];
							snprintf(path_copy, sizeof(path_copy), "%s", e->path);
							load_image_directory(path_copy);
							GFX_clearLayers(LAYER_SCROLLTEXT);
							dirty = true;
						} else {
							IvViewer_open(&browser, act.index);
							app_screen = SCREEN_VIEWER;
							GFX_clearLayers(LAYER_SCROLLTEXT);
							dirty = true;
						}
					}
					break;
				case LISTVIEW_MENU:
					if (act.index >= 0 && act.index < browser.entry_count &&
						strcmp(browser.entries[act.index].name, "..") != 0) {
						ctx_target_index = act.index;
						ContextMenuItem items[2];
						snprintf(items[0].label, sizeof(items[0].label), "Rename");
						items[0].id = IV_CTX_RENAME;
						snprintf(items[1].label, sizeof(items[1].label), "Delete");
						items[1].id = IV_CTX_DELETE;
						ContextMenu_open(items, 2);
						GFX_clearLayers(LAYER_SCROLLTEXT);
						dirty = true;
					}
					break;
				default:
					break;
				}
			}

			// Selected-file preview: debounce the request so fast scrolling
			// never fires a decode per row, then drain whatever the loader
			// has finished. This runs every frame, ahead of the dirty
			// check below, so results land whether this frame renders or
			// idles.
			//
			// Re-check app_screen: LISTVIEW_ACTIVATED above may have just
			// called IvViewer_open() and flipped us to SCREEN_VIEWER while
			// still inside this BROWSER branch. IvViewer_open() issues its
			// own FULL request for the same file the debounce below would
			// also target, and a tiny image can decode within this same
			// frame; without this guard the drain loop's else-branch (which
			// frees any result that isn't ours) would steal and free that
			// FULL result, leaving the viewer stuck on "Loading image..."
			// until B is pressed.
			if (app_screen == SCREEN_BROWSER) {
				char selected_path[512] = "";
				if (browser_view.selected >= 0 && browser_view.selected < browser.entry_count) {
					ImageEntry* e = &browser.entries[browser_view.selected];
					if (!e->is_dir)
						snprintf(selected_path, sizeof(selected_path), "%s", e->path);
				}
				if (strcmp(selected_path, preview_want) != 0) {
					snprintf(preview_want, sizeof(preview_want), "%s", selected_path);
					preview_want_tick = SDL_GetTicks();
				}
				if (preview_want[0] && strcmp(preview_want, preview_have) != 0 &&
					strcmp(preview_want, preview_requested) != 0 &&
					SDL_GetTicks() - preview_want_tick > 150) {
					IvLoader_request(preview_want, IV_LOAD_PREVIEW);
					snprintf(preview_requested, sizeof(preview_requested), "%s", preview_want);
				}

				// Drains the whole shared loader queue while the browser is the
				// active screen. Only PREVIEW results are ours; the viewer's
				// FULL/PREFETCH requests are only ever in flight while it's the
				// active screen instead (mutually exclusive with this branch),
				// so anything else here is stale and just needs freeing.
				IvLoadResult res;
				while (IvLoader_poll(&res)) {
					if (res.purpose != IV_LOAD_PREVIEW || strcmp(res.path, preview_want) != 0) {
						if (res.surface)
							SDL_FreeSurface(res.surface);
						continue;
					}
					if (preview_surf) {
						SDL_FreeSurface(preview_surf);
						preview_surf = NULL;
					}
					if (res.status == IV_LOAD_OK) {
						preview_surf = res.surface;
						preview_ow = res.orig_w;
						preview_oh = res.orig_h;
						snprintf(preview_have, sizeof(preview_have), "%s", res.path);
						preview_error[0] = '\0';
					} else {
						preview_have[0] = '\0';
						snprintf(preview_error, sizeof(preview_error), "%s", res.path);
					}
					dirty = true;
				}

				// Toast tick: keeps redrawing while a browser toast is
				// visible, then once more on the expiry edge (clearing the
				// message) so that one last render call wipes the GPU
				// layer. Mirrors ModuleCommon_tickToast in
				// workspace/all/mediaplayer/module_common.c.
				tick_toast(browser_toast, browser_toast_time, &dirty);
			}
		} else { // SCREEN_VIEWER
			IvViewerStatus st = IvViewer_update(screen, &dirty);
			if (st != IV_VIEWER_CONTINUE) {
				if (st == IV_VIEWER_ERROR) {
					snprintf(browser_toast, sizeof(browser_toast), "%s",
							 IvViewer_lastError() == IV_LOAD_ERR_TOO_LARGE ? "Image too large" : "Couldn't load image");
					browser_toast_time = SDL_GetTicks();
				}
				IvViewer_close();
				app_screen = SCREEN_BROWSER;
				UI_clearToast();
				// A preview request for the still-selected file may have been
				// in flight when the viewer opened; its result was drained
				// (and freed) by the viewer, not applied here, so drop the
				// "already requested" marker to let the debounce re-request
				// it now that we're back.
				preview_requested[0] = '\0';
				dirty = true;
			}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (dirty || (app_screen == SCREEN_BROWSER
						  ? (UI_listViewBusy(&browser_view) || ContextMenu_isOpen())
						  : IvViewer_busy())) {
			if (app_screen == SCREEN_BROWSER)
				render_browser();
			else
				IvViewer_render(screen);
			GFX_flip(screen);
			dirty = false;
		} else {
			if (app_screen == SCREEN_BROWSER)
				UI_listViewTickIdle(&browser_view);
			GFX_sync();
		}
	}

	if (app_screen == SCREEN_VIEWER)
		IvViewer_close();
	ImageBrowser_freeEntries(&browser);
	if (preview_surf)
		SDL_FreeSurface(preview_surf);
	UI_clearToast();
	UI_freeIconPair(&icon_folder, &icon_folder_inv);
	UI_freeIconPair(&icon_image, &icon_image_inv);
	UI_quitEmptyIcon();
	QuitSettings();
	IvLoader_quit();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
