// Game list screen: the ROM/console browser that nextui.c boots into.
// Input handling, background/thumbnail resolution and rendering for this
// screen live here; nextui.c only dispatches to it. (split from nextui.c)

#include "gamelist.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "display_helper.h"
#include "shortcuts.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_message.h"
#include "ui_contextmenu.h"
#include "ui_keyboard.h"
#include "ui_list.h"
#include "ui_listdialog.h"
#include "ui_pindialog.h"
#include "utils.h"

#include "content.h"
#include "gameswitcher.h"
#include "imgloader.h"
#include "launcher.h"
#include "recents.h"
#include "search.h"
#include "types.h"

#include <assert.h>
#include <dirent.h>
#include <msettings.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool gl_simple_mode = false;

static ScrollTextState list_scroll = {0};

static bool had_thumb = false;
static int ox;
static char folderBgPath[1024] = {0};
// last background type loaded; file-scope so it can be reset alongside
// folderBgPath when another screen clears the shared background surface
static int bgLastType = -1;

void GameList_invalidateBackground(void) {
	// force resolveAndLoadBackground to reload next render — call this whenever
	// something else (eg. Search) clears the shared folder background, otherwise
	// the change-detection cache thinks it's still loaded and skips the reload
	folderBgPath[0] = '\0';
	bgLastType = -1;
}

void GameList_init(bool simple_mode) {
	gl_simple_mode = simple_mode;
}

bool GameList_scrollBusy(void) {
	return ScrollText_isScrolling(&list_scroll) || ScrollText_needsRender(&list_scroll);
}

bool GameList_scrollIsScrolling(void) {
	return ScrollText_isScrolling(&list_scroll);
}

void GameList_scrollTickIdle(void) {
	ScrollText_activateAfterDelay(&list_scroll);
	if (ScrollText_isScrolling(&list_scroll)) {
		ScrollText_animateOnly(&list_scroll);
	}
}

void GameList_clearScroll(void) {
	ScrollText_clear(&list_scroll);
}

static void resolveAndLoadBackground(Entry* entry, const char* rompath,
									 bool* list_show_entry_names) {
	// Persists across calls to avoid redundant background reloads
	// (file-scope bgLastType so GameList_invalidateBackground can reset it)
	int* lastType = &bgLastType;

	char defaultBgPath[512];
	snprintf(defaultBgPath, sizeof(defaultBgPath), SDCARD_PATH "/bg.png");

	// Resolve: what path to compare for changes, and what bg image to load
	const char* cmpPath = NULL;
	char bgPath[512] = {0};

	if (entry && (entry->type == ENTRY_DIR || entry->type == ENTRY_ROM) &&
		Shortcuts_exists(entry->path + strlen(SDCARD_PATH))) {
		cmpPath = entry->path;
	} else if (entry && (entry->type == ENTRY_DIR || entry->type == ENTRY_ROM) &&
			   CFG_getRomsUseFolderBackground()) {
		cmpPath = entry->type == ENTRY_DIR ? entry->path : rompath;
		snprintf(bgPath, sizeof(bgPath), "%s/.media/%s.png", cmpPath,
				 entry->type == ENTRY_DIR ? "bg" : "bglist");
		if (!exists(bgPath))
			strncpy(bgPath, defaultBgPath, sizeof(bgPath) - 1);
	} else if (entry && entry->type == ENTRY_PAK && suffixMatch(".pak", entry->path)) {
		cmpPath = entry->path;
		snprintf(bgPath, sizeof(bgPath), TOOLS_PATH "/.media/%s/bg.png",
				 Shortcuts_getPakBasename(entry->path));
	} else if (exists(defaultBgPath) &&
			   strcmp(defaultBgPath, folderBgPath) != 0) {
		cmpPath = defaultBgPath;
		strncpy(bgPath, defaultBgPath, sizeof(bgPath) - 1);
	} else {
		*list_show_entry_names = true;
		return;
	}

	if (!cmpPath)
		return;

	// Skip if background hasn't changed
	int curType = entry ? entry->type : -1;
	if (strcmp(cmpPath, folderBgPath) == 0 && *lastType == curType)
		return;

	*lastType = curType;
	strncpy(folderBgPath, cmpPath, sizeof(folderBgPath) - 1);

	// Load background, or clear if image doesn't exist
	if (bgPath[0] && exists(bgPath))
		startLoadFolderBackground(bgPath, onBackgroundLoaded);
	else {
		onBackgroundLoaded(NULL);
		*list_show_entry_names = true;
	}
}

///////////////////////////////////////
// Context-menu actions (dispatched from nextui.c by the item id built above)

// Rebuild the directory at stack index `idx` from its path, clamping selection
// and the visible window. Used to refresh the list after a mutating action.
static void reloadDirectoryAt(int idx, int keep_selected) {
	if (idx < 0 || idx >= stack->count)
		return;
	Directory* old = stack->items[idx];
	char path[MAX_PATH];
	strncpy(path, old->path, sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	Directory* fresh = Directory_new(path, 0);
	int n = fresh->entries->count;
	int sel = keep_selected;
	if (sel >= n)
		sel = n > 0 ? n - 1 : 0;
	if (sel < 0)
		sel = 0;
	fresh->selected = sel;

	int rc = MAIN_ROW_COUNT - 1;
	fresh->start = 0;
	fresh->end = (n < rc) ? n : rc;
	if (sel >= fresh->end && n > rc) {
		fresh->end = sel + 1;
		fresh->start = fresh->end - rc;
	}

	Directory_free(old);
	stack->items[idx] = fresh;
	if (idx == stack->count - 1)
		top = fresh;
}

// A file "belongs" to base if it is exactly <base> or starts with "<base>.".
// The trailing-dot boundary is what makes this safe: it renames Game.gba /
// Game.png / Game.srm / Game.gba.sav / Game.state without ever touching
// Game2.gba or its saves.
static bool nameMatchesBase(const char* name, const char* base, size_t baselen) {
	if (strncmp(name, base, baselen) != 0)
		return false;
	return name[baselen] == '\0' || name[baselen] == '.';
}

// Rename every file in `dir` that belongs to oldbase so its leading oldbase
// becomes newbase (suffix preserved). No-ops if `dir` isn't a directory.
static void renameSweepDir(const char* dir, const char* oldbase, const char* newbase) {
	DIR* dh = opendir(dir);
	if (!dh)
		return;
	size_t oldlen = strlen(oldbase);
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
									 (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
			continue; // "." / ".."
		if (!nameMatchesBase(dp->d_name, oldbase, oldlen))
			continue;
		char from[MAX_PATH];
		char to[MAX_PATH];
		snprintf(from, sizeof(from), "%s/%s", dir, dp->d_name);
		snprintf(to, sizeof(to), "%s/%s%s", dir, newbase, dp->d_name + oldlen);
		rename(from, to);
	}
	closedir(dh);
}

// Real file rename of a ROM plus its art, saves and states, all keyed by the
// ROM's base name (filename minus final extension). Returns the new full ROM
// path in new_path (>= MAX_PATH), or false if the rename was rejected.
static bool renameRomFiles(Entry* entry, const char* newbase, char* new_path) {
	// split path into <dir>/<filename>
	char dir[MAX_PATH];
	strncpy(dir, entry->path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char* slash = strrchr(dir, '/');
	if (!slash)
		return false;
	*slash = '\0';
	const char* filename = slash + 1;

	// oldbase = filename minus final extension; ext keeps the leading dot
	char oldbase[MAX_PATH];
	strncpy(oldbase, filename, sizeof(oldbase) - 1);
	oldbase[sizeof(oldbase) - 1] = '\0';
	const char* fdot = strrchr(filename, '.');
	char ext[MAX_PATH];
	if (fdot) {
		oldbase[fdot - filename] = '\0';
		strncpy(ext, fdot, sizeof(ext) - 1);
		ext[sizeof(ext) - 1] = '\0';
	} else {
		ext[0] = '\0';
	}

	if (strcmp(oldbase, newbase) == 0)
		return false; // no change

	// refuse to clobber an existing ROM of the new name
	snprintf(new_path, MAX_PATH, "%s/%s%s", dir, newbase, ext);
	if (exists(new_path))
		return false;

	char emu[MAX_PATH];
	getEmuName(entry->path, emu);

	// 1) the ROM folder itself (renames Game.gba + any Game.cue / Game.m3u)
	renameSweepDir(dir, oldbase, newbase);

	// 2) box/thumbnail art
	char media[MAX_PATH];
	snprintf(media, sizeof(media), "%s/.media", dir);
	renameSweepDir(media, oldbase, newbase);

	// 3) SRAM / RTC saves
	char saves[MAX_PATH];
	snprintf(saves, sizeof(saves), "%s/Saves/%s", SDCARD_PATH, emu);
	renameSweepDir(saves, oldbase, newbase);

	// 4) resume slot + save-state preview bitmaps
	char minui[MAX_PATH];
	snprintf(minui, sizeof(minui), "%s/.minui/%s", SHARED_USERDATA_PATH, emu);
	renameSweepDir(minui, oldbase, newbase);

	// 5) save-state binaries live under one dir per core: <emu>-<corename>
	char emu_prefix[MAX_PATH];
	snprintf(emu_prefix, sizeof(emu_prefix), "%s-", emu);
	size_t plen = strlen(emu_prefix);
	DIR* ud = opendir(SHARED_USERDATA_PATH);
	if (ud) {
		struct dirent* dp;
		while ((dp = readdir(ud)) != NULL) {
			if (strncmp(dp->d_name, emu_prefix, plen) != 0)
				continue;
			char states[MAX_PATH];
			snprintf(states, sizeof(states), "%s/%s", SHARED_USERDATA_PATH, dp->d_name);
			renameSweepDir(states, oldbase, newbase);
		}
		closedir(ud);
	}

	return true;
}

// Full-screen blocking confirm dialog. Returns true on A, false on B.
static bool confirmModal(const char* title, const char* subtitle) {
	bool result = false;
	GFX_clearLayers(LAYER_ALL);
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A)) {
			result = true;
			break;
		}
		if (PAD_justPressed(BTN_B)) {
			result = false;
			break;
		}
		UI_renderConfirmDialog(screen, title, subtitle);
		GFX_flip(screen);
	}
	GFX_clearLayers(LAYER_ALL);
	return result;
}

// Simple mode: launching Settings requires the parent PIN (when one is set).
// Re-prompts on a wrong PIN, B cancels. Returns true when launch may proceed.
static bool settingsPinAllows(Entry* entry) {
	if (!gl_simple_mode || !entry || entry->type != ENTRY_PAK)
		return true;

	char settings_path[MAX_PATH];
	snprintf(settings_path, sizeof(settings_path), "%s/Tools/%s/Settings.pak",
			 SDCARD_PATH, PLATFORM);
	if (!exactMatch(entry->path, settings_path))
		return true;

	char pin[PINDIALOG_PIN_LEN + 1];
	if (!SimpleMode_readPin(pin))
		return true; // legacy flag file without a PIN: ungated

	bool allowed = false;
	const char* error = NULL;
	GFX_clearLayers(LAYER_ALL);
	while (!allowed) {
		PinDialog_init("Enter Settings PIN");
		PinDialog_setError(error);
		bool cancelled = false;
		PinDialogResult r = {PINDIALOG_NONE, ""};
		while (1) {
			GFX_startFrame();
			PAD_poll();
			r = PinDialog_handleInput();
			if (r.action == PINDIALOG_CONFIRMED)
				break;
			if (r.action == PINDIALOG_CANCEL) {
				cancelled = true;
				break;
			}
			PinDialog_render(screen);
			GFX_flip(screen);
		}
		PinDialog_quit();
		if (cancelled)
			break;
		if (strcmp(r.pin, pin) == 0)
			allowed = true;
		else
			error = "Wrong PIN. Try again."; // re-init also resets digits to 0
	}
	GFX_clearLayers(LAYER_ALL);
	return allowed;
}

// Full-screen blocking collection picker. Returns the chosen collection index
// (0..count-1), COLLECTION_PICK_NEW for "New Collection…", or -1 on cancel.
#define COLLECTION_PICK_NEW (-2)
static int pickCollectionModal(Array* collections) {
	ListDialogItem items[LISTDIALOG_MAX_ITEMS];
	int n = collections->count;
	if (n > LISTDIALOG_MAX_ITEMS - 1)
		n = LISTDIALOG_MAX_ITEMS - 1;
	for (int i = 0; i < n; i++) {
		Entry* c = collections->items[i];
		memset(&items[i], 0, sizeof(ListDialogItem));
		strncpy(items[i].text, c->name, LISTDIALOG_MAX_TEXT - 1);
		items[i].prepend_icons[0] = -1;
		items[i].append_icons[0] = -1;
	}
	memset(&items[n], 0, sizeof(ListDialogItem));
	strncpy(items[n].text, "New Collection...", LISTDIALOG_MAX_TEXT - 1);
	items[n].prepend_icons[0] = -1;
	items[n].append_icons[0] = -1;
	int count = n + 1;

	ListDialog_init("Add to Collection");
	ListDialog_setItems(items, count);

	int chosen = -1;
	GFX_clearLayers(LAYER_ALL);
	while (1) {
		GFX_startFrame();
		PAD_poll();
		ListDialogResult r = ListDialog_handleInput();
		if (r.action == LISTDIALOG_SELECTED) {
			chosen = r.index;
			break;
		}
		if (r.action == LISTDIALOG_CANCEL) {
			chosen = -1;
			break;
		}
		ListDialog_render(screen);
		GFX_flip(screen);
	}
	ListDialog_quit();
	GFX_clearLayers(LAYER_ALL);

	if (chosen < 0)
		return -1;
	if (chosen == n)
		return COLLECTION_PICK_NEW;
	return chosen;
}

// Append the ROM's SD-relative path to a collection .txt (deduped).
static void addRomToCollectionFile(const char* collection_path, Entry* entry) {
	if (!prefixMatch(SDCARD_PATH, entry->path))
		return;
	const char* rel = entry->path + strlen(SDCARD_PATH);

	FILE* f = fopen(collection_path, "r");
	if (f) {
		char line[MAX_PATH];
		while (fgets(line, sizeof(line), f) != NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (exactMatch(line, rel)) {
				fclose(f);
				return; // already present
			}
		}
		fclose(f);
	}

	FILE* out = fopen(collection_path, "a");
	if (out) {
		fprintf(out, "%s\n", rel);
		fclose(out);
	}
}

static void doAddToCollection(Entry* entry) {
	Array* collections = getCollections();
	int pick = pickCollectionModal(collections);

	if (pick == COLLECTION_PICK_NEW) {
		char* name = UIKeyboard_open("New collection name");
		DisplayHelper_recoverDisplay();
		if (name && strlen(name) > 0 && !strchr(name, '/')) {
			mkdir_p(COLLECTIONS_PATH);
			char coll_path[MAX_PATH];
			snprintf(coll_path, sizeof(coll_path), "%s/%s.txt", COLLECTIONS_PATH, name);
			addRomToCollectionFile(coll_path, entry);
		}
		if (name)
			free(name);
	} else if (pick >= 0 && pick < collections->count) {
		Entry* coll = collections->items[pick];
		addRomToCollectionFile(coll->path, entry);
	}

	EntryArray_free(collections);
}

static void doRename(Entry* entry, int sel) {
	char prompt[MAX_PATH];
	snprintf(prompt, sizeof(prompt), "Rename: %s", entry->name);
	char* newname = UIKeyboard_open(prompt);
	DisplayHelper_recoverDisplay();
	if (newname && strlen(newname) > 0 && !strchr(newname, '/')) {
		char new_path[MAX_PATH];
		if (renameRomFiles(entry, newname, new_path))
			reloadDirectoryAt(stack->count - 1, sel);
	}
	if (newname)
		free(newname);
}

// Dispatch a selected context-menu item (ids assigned in GameList_handleInput).
// Runs in nextui.c's main loop; blocking modals here are safe (the flip is
// synchronous, and UIKeyboard_open already blocks mid-loop from Search).
void GameList_runContextAction(int id) {
	int sel = top->selected;
	Entry* entry = (top->entries->count > 0 && sel >= 0 && sel < top->entries->count)
					   ? top->entries->items[sel]
					   : NULL;
	int root_sel = ((Directory*)stack->items[0])->selected;

	switch (id) {
	case 1: // Refresh Roms (root)
		Content_invalidateEmulist();
		reloadDirectoryAt(0, root_sel);
		break;
	case 10: // Remove Game (Recently Played)
		if (entry) {
			Recents_removeAt(sel);
			reloadDirectoryAt(stack->count - 1, sel);
		}
		break;
	case 20: // Pin Tool
	case 30: // Pin Item
		if (entry) {
			Shortcuts_add(entry);
			reloadDirectoryAt(0, root_sel);
			reloadDirectoryAt(stack->count - 1, sel);
		}
		break;
	case 21: // Unpin Tool
	case 31: // Unpin Item
		if (entry) {
			Shortcuts_remove(entry);
			reloadDirectoryAt(0, root_sel);
			reloadDirectoryAt(stack->count - 1, sel);
		}
		break;
	case 32: // Delete Rom
		if (entry && entry->type == ENTRY_ROM) {
			if (confirmModal("Delete ROM?", entry->name)) {
				unlink(entry->path);
				reloadDirectoryAt(stack->count - 1, sel);
			}
		}
		break;
	case 33: // Rename Rom
		if (entry && entry->type == ENTRY_ROM)
			doRename(entry, sel);
		break;
	case 34: // Add to Collection
		if (entry && entry->type == ENTRY_ROM)
			doAddToCollection(entry);
		break;
	default:
		break;
	}
}

GameListResult GameList_handleInput(unsigned long now, int currentScreen,
									IndicatorType show_setting, bool* dirty) {
	GameListResult result = {
		.screen = currentScreen,
		.animdir = ANIM_NONE,
		.folderbgchanged = false,
	};

	int selected = top->selected;
	int total = top->entries->count;
	int row_count = MAIN_ROW_COUNT - 1;

	if (PAD_tappedMenu(now) && !ContextMenu_isOpen()) {
		// Open contextual menu based on current page
		Entry* entry = (total > 0) ? top->entries->items[selected] : NULL;
		int idx = 0;
		ContextMenuItem items[CONTEXTMENU_MAX_ITEMS];

		if (stack->count == 1) {
			// Root menu (main console list)
			strncpy(items[idx].label, "Refresh Roms", CONTEXTMENU_MAX_TEXT);
			items[idx].id = 1;
			idx++;
		} else if (exactMatch(top->path, FAUX_RECENT_PATH)) {
			// Recently Played
			if (entry) {
				strncpy(items[idx].label, "Remove Game", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 10;
				idx++;
			}
		} else if (Shortcuts_isInToolsFolder(top->path)) {
			// Tools listing
			if (entry) {
				if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH))) {
					strncpy(items[idx].label, "Unpin Tool", CONTEXTMENU_MAX_TEXT);
					items[idx].id = 21;
				} else {
					strncpy(items[idx].label, "Pin Tool", CONTEXTMENU_MAX_TEXT);
					items[idx].id = 20;
				}
				idx++;
			}
		} else if (entry) {
			// ROM listing (console directory or subfolder)
			if (canPinEntry(entry)) {
				if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH))) {
					strncpy(items[idx].label, "Unpin Item", CONTEXTMENU_MAX_TEXT);
					items[idx].id = 31;
				} else {
					strncpy(items[idx].label, "Pin Item", CONTEXTMENU_MAX_TEXT);
					items[idx].id = 30;
				}
				idx++;
			}
			if (entry->type == ENTRY_ROM) {
				strncpy(items[idx].label, "Delete Rom", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 32;
				idx++;
				strncpy(items[idx].label, "Rename Rom", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 33;
				idx++;
				strncpy(items[idx].label, "Add to Collection", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 34;
				idx++;
			}
		}

		if (idx > 0) {
			ContextMenu_open("Options", items, idx);
			*dirty = true;
		}

		return result;
	} else if (PAD_tappedSelect(now)) {
		result.screen = SCREEN_GAMESWITCHER;
		GameSwitcher_resetSelection();
		result.animdir = SLIDE_UP;
		*dirty = true;
		GFX_clearLayers(LAYER_SCROLLTEXT);
		ScrollText_clear(&list_scroll);
		return result;
	} else if (total > 0) {
		if (PAD_justRepeated(BTN_UP)) {
			if (selected == 0 && !PAD_justPressed(BTN_UP)) {
			} else {
				selected -= 1;
				if (selected < 0) {
					selected = total - 1;
					int start = total - row_count;
					top->start = (start < 0) ? 0 : start;
					top->end = total;
				} else if (selected < top->start) {
					top->start -= 1;
					top->end -= 1;
				}
			}
		} else if (PAD_justRepeated(BTN_DOWN)) {
			if (selected == total - 1 && !PAD_justPressed(BTN_DOWN)) {
			} else {
				selected += 1;
				if (selected >= total) {
					selected = 0;
					top->start = 0;
					top->end = (total < row_count) ? total : row_count;
				} else if (selected >= top->end) {
					top->start += 1;
					top->end += 1;
				}
			}
		}
		if (PAD_justRepeated(BTN_LEFT)) {
			selected -= row_count;
			if (selected < 0) {
				selected = 0;
				top->start = 0;
				top->end = (total < row_count) ? total : row_count;
			} else if (selected < top->start) {
				top->start -= row_count;
				if (top->start < 0)
					top->start = 0;
				top->end = top->start + row_count;
			}
		} else if (PAD_justRepeated(BTN_RIGHT)) {
			selected += row_count;
			if (selected >= total) {
				selected = total - 1;
				int start = total - row_count;
				top->start = (start < 0) ? 0 : start;
				top->end = total;
			} else if (selected >= top->end) {
				top->end += row_count;
				if (top->end > total)
					top->end = total;
				top->start = top->end - row_count;
			}
		}
	}

	if (total > 0 && PAD_justRepeated(BTN_L1) &&
		!PAD_isPressed(BTN_R1) &&
		!PWR_ignoreSettingInput(BTN_L1, show_setting)) { // previous alpha
		Entry* entry = top->entries->items[selected];
		int i = entry->alpha - 1;
		if (i >= 0) {
			selected = top->alphas.items[i];
			if (total > row_count) {
				top->start = selected;
				top->end = top->start + row_count;
				if (top->end > total)
					top->end = total;
				top->start = top->end - row_count;
			}
		}
	} else if (total > 0 && PAD_justRepeated(BTN_R1) &&
			   !PAD_isPressed(BTN_L1) &&
			   !PWR_ignoreSettingInput(BTN_R1, show_setting)) { // next alpha
		Entry* entry = top->entries->items[selected];
		int i = entry->alpha + 1;
		if (i < top->alphas.count) {
			selected = top->alphas.items[i];
			if (total > row_count) {
				top->start = selected;
				top->end = top->start + row_count;
				if (top->end > total)
					top->end = total;
				top->start = top->end - row_count;
			}
		}
	}

	if (selected != top->selected) {
		top->selected = selected;
		*dirty = true;
	}

	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;

	if (*dirty && total > 0)
		readyResume(entry);

	if (total > 0 && resume.can_resume && PAD_justReleased(BTN_RESUME) && !PAD_isPressed(BTN_L2) && !PAD_isPressed(BTN_R2)) {
		resume.should_resume = true;
		Entry_open(entry);
		*dirty = true;
	}
	// Y to search at root (pin/unpin now lives in the context menu)
	else if (stack->count == 1 && PAD_justReleased(BTN_Y)) {
		if (Search_open()) {
			result.screen = SCREEN_SEARCH;
			result.animdir = SLIDE_LEFT;
			GFX_clearLayers(LAYER_SCROLLTEXT);
			ScrollText_clear(&list_scroll);
		}
		*dirty = true;
		return result;
	} else if (total > 0 && PAD_justPressed(BTN_A)) {
		if (settingsPinAllows(entry)) {
			Entry_open(entry);
			if (entry->type == ENTRY_DIR && !startgame) {
				result.animdir = SLIDE_LEFT;
			}
		}
		*dirty = true;

		if (top->entries->count > 0)
			readyResume(top->entries->items[top->selected]);
	} else if (PAD_justPressed(BTN_B) && stack->count > 1) {
		closeDirectory();
		result.animdir = SLIDE_RIGHT;
		*dirty = true;

		if (top->entries->count > 0)
			readyResume(top->entries->items[top->selected]);
	}

	return result;
}

void GameList_render(SDL_Surface* screen, int lastScreen,
					 IndicatorType show_setting, SDL_Surface* blackBG) {
	int total = top->entries->count;

	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;
	char path_copy[1024];
	char res_copy[1024] = {0};
	char* rompath = NULL;

	if (entry) {
		char tmp_path[MAX_PATH];
		strncpy(tmp_path, entry->path, sizeof(tmp_path) - 1);
		tmp_path[sizeof(tmp_path) - 1] = '\0';

		char* res_name = strrchr(tmp_path, '/');
		if (res_name)
			res_name++;
		else
			res_name = tmp_path;

		strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
		path_copy[sizeof(path_copy) - 1] = '\0';

		rompath = dirname(path_copy);

		strncpy(res_copy, res_name, sizeof(res_copy) - 1);
		res_copy[sizeof(res_copy) - 1] = '\0';

		char* dot = strrchr(res_copy, '.');
		if (dot)
			*dot = '\0';
	}

	// this is only a choice on the root folder
	bool list_show_entry_names =
		stack->count > 1 || CFG_getShowFolderNamesAtRoot();

	// load folder background
	resolveAndLoadBackground(entry, rompath, &list_show_entry_names);

	// load game thumbnails
	if (total > 0) {
		if (CFG_getShowGameArt()) {
			char thumbpath[1024];
			snprintf(thumbpath, sizeof(thumbpath), "%s/.media/%s.png", rompath,
					 res_copy);
			had_thumb = startLoadThumb(thumbpath);
			int max_w = (int)(screen->w - (screen->w * CFG_getGameArtWidth()));
			if (had_thumb)
				ox = (int)(max_w)-SCALE1(BUTTON_MARGIN * 5);
			else
				ox = screen->w;
		}
	}

	// buttons
	{
		char* right_pairs[16] = {NULL};
		int p = 0;

		// search hint at root
		if (!(show_setting && !GetHDMI()) && !GetHDMI() &&
			stack->count == 1 && total > 0) {
			right_pairs[p++] = "Y";
			right_pairs[p++] = "SEARCH";
		}

		// navigation actions
		if (total == 0) {
			if (stack->count > 1) {
				right_pairs[p++] = "B";
				right_pairs[p++] = "BACK";
			}
		} else {
			if (resume.can_resume) {
				right_pairs[p++] = "X";
				right_pairs[p++] = "RESUME";
				if (stack->count > 1) {
					right_pairs[p++] = "B";
					right_pairs[p++] = "BACK";
				}
				right_pairs[p++] = "A";
				right_pairs[p++] = "OPEN";
			} else if (stack->count > 1) {
				right_pairs[p++] = "B";
				right_pairs[p++] = "BACK";
				right_pairs[p++] = "A";
				right_pairs[p++] = "OPEN";
			} else {
				right_pairs[p++] = "A";
				right_pairs[p++] = "OPEN";
			}
		}

		if (right_pairs[0])
			UI_renderButtonHintBar(screen, right_pairs);
	}

	if (total > 0) {
		int selected_row = top->selected - top->start;

		for (int i = top->start, j = 0; i < top->end; i++, j++) {
			Entry* entry = top->entries->items[i];
			char* entry_name = entry->name;
			char* entry_unique = entry->unique;
			bool row_is_selected = (j == selected_row);

			// Calculate per-item available width (thumbnail-aware)
			int available_width =
				MAX(0, (had_thumb ? ox + SCALE1(BUTTON_MARGIN)
								  : screen->w - SCALE1(BUTTON_MARGIN)) -
						   SCALE1(PADDING * 2));

			// Prepare display text: prefer unique name, fall back to entry name
			trimSortingMeta(&entry_name);
			if (entry_unique)
				trimSortingMeta(&entry_unique);
			char* display_text = entry_unique ? entry_unique : entry_name;

			int top_offset = PILL_SIZE;
			int y = SCALE1(PADDING + top_offset + j * PILL_SIZE);

			if (list_show_entry_names) {
				char truncated[256];
				ListLayout item_layout = {
					.item_h = SCALE1(PILL_SIZE),
					.max_width = available_width,
				};
				ListItemPos pos = UI_renderListItemPill(
					screen, &item_layout, font.large,
					display_text, truncated, y, row_is_selected, 0);
				int text_width = pos.pill_width - SCALE1(BUTTON_PADDING * 2);
				UI_renderListItemText(screen,
									  row_is_selected ? &list_scroll : NULL,
									  display_text, font.large,
									  pos.text_x, pos.text_y, text_width, row_is_selected);
			}
		}
		UI_renderScrollIndicators(screen, top->start, MAIN_ROW_COUNT - 1, total);

		if (lastScreen == SCREEN_OFF) {
			GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255,
									  0, CFG_getMenuTransitions() ? 200 : 20,
									  LAYER_THUMBNAIL);
		}

	} else {
		UI_renderCenteredMessage(screen, "Empty folder");
	}
}
