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
#include <sys/stat.h>
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

// A "folder game" is an ENTRY_DIR under Roms containing a folder-named .cue
// or .m3u (multi-disc game); opening it auto-launches disc 1, so the context
// menu treats it like a ROM. Fills game_file_out (>= MAX_PATH) with the
// resolved cue/m3u — the entry's effective ROM file for all actions.
static bool entryFolderGame(Entry* entry, char* game_file_out) {
	if (!entry || entry->type != ENTRY_DIR)
		return false;
	if (!prefixMatch(ROMS_PATH, entry->path))
		return false;
	// a console dir is a library, never a game — even if it happens to
	// contain a playlist named after itself
	if (isConsoleDir(entry->path))
		return false;
	return dirGameFile(entry->path, game_file_out) != 0;
}

// True when `path` is the folder-named .cue/.m3u of its parent dir (basename
// minus extension == parent dir name) — the file that makes the parent a
// folder game. Fills parent_out (>= MAX_PATH) with the parent dir path.
static bool isFolderGameFile(const char* path, char* parent_out) {
	if (!suffixMatch(".cue", path) && !suffixMatch(".m3u", path))
		return false;
	if (!prefixMatch(ROMS_PATH, path))
		return false;
	char work[MAX_PATH];
	strncpy(work, path, sizeof(work) - 1);
	work[sizeof(work) - 1] = '\0';
	char* slash = strrchr(work, '/');
	if (!slash)
		return false;
	char base[MAX_PATH];
	strncpy(base, slash + 1, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	char* dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
	*slash = '\0'; // work = parent dir
	// never treat a console dir as the "game folder" — a stray playlist named
	// after the console (eg. DC/DC.m3u) must not delete the whole library
	if (isConsoleDir(work))
		return false;
	char* parent_name = strrchr(work, '/');
	if (!parent_name || !exactMatch(parent_name + 1, base))
		return false;
	strncpy(parent_out, work, MAX_PATH - 1);
	parent_out[MAX_PATH - 1] = '\0';
	return true;
}

// Depth-first recursive delete. Symlink-safe: lstat, so symlinks are
// unlinked without ever being followed; files unlink, dirs rmdir last.
static void removeRecursive(const char* path) {
	struct stat st;
	if (lstat(path, &st) != 0)
		return;
	if (!S_ISDIR(st.st_mode)) {
		unlink(path);
		return;
	}
	DIR* dh = opendir(path);
	if (dh) {
		struct dirent* dp;
		while ((dp = readdir(dh)) != NULL) {
			if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
										 (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
				continue; // "." / ".."
			char child[MAX_PATH];
			snprintf(child, sizeof(child), "%s/%s", path, dp->d_name);
			removeRecursive(child);
		}
		closedir(dh);
	}
	rmdir(path);
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

// Rewrite or prune one line across every Collections/*.txt. Lines that exactly
// match old_rel (an SD-relative path in the on-disk format addRomToCollectionFile
// writes: leading '/', no SDCARD_PATH prefix) are replaced with new_rel, or
// dropped when new_rel is NULL. Only files that actually change are rewritten,
// via a .tmp + rename so a crash mid-write can't truncate a collection.
static void updateCollectionLines(const char* old_rel, const char* new_rel) {
	DIR* d = opendir(COLLECTIONS_PATH);
	if (!d)
		return;
	struct dirent* dp;
	while ((dp = readdir(d)) != NULL) {
		if (!suffixMatch(".txt", dp->d_name))
			continue;
		char coll_path[MAX_PATH];
		snprintf(coll_path, sizeof(coll_path), "%s/%s", COLLECTIONS_PATH, dp->d_name);

		FILE* in = fopen(coll_path, "r");
		if (!in)
			continue;
		char tmp_path[MAX_PATH];
		snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", coll_path);
		FILE* out = fopen(tmp_path, "w");
		if (!out) {
			fclose(in);
			continue;
		}

		bool changed = false;
		char line[MAX_PATH];
		while (fgets(line, sizeof(line), in) != NULL) {
			char trimmed[MAX_PATH];
			strncpy(trimmed, line, sizeof(trimmed) - 1);
			trimmed[sizeof(trimmed) - 1] = '\0';
			normalizeNewline(trimmed);
			trimTrailingNewlines(trimmed);
			if (exactMatch(trimmed, old_rel)) {
				changed = true;
				if (new_rel)
					fprintf(out, "%s\n", new_rel);
				// new_rel == NULL: drop the line (delete)
			} else {
				fputs(line, out); // preserve the original line verbatim
			}
		}
		fclose(in);
		fclose(out);

		if (changed)
			rename(tmp_path, coll_path);
		else
			unlink(tmp_path);
	}
	closedir(d);
}

// Whether a map.txt (key<TAB>alias) has a display alias for `key` (a filename;
// map.txt is keyed by filename, not path — see content.c Directory_index).
static bool mapHasKey(const char* map_path, const char* key) {
	FILE* f = fopen(map_path, "r");
	if (!f)
		return false;
	bool found = false;
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), f) != NULL) {
		char work[MAX_PATH];
		strncpy(work, line, sizeof(work) - 1);
		work[sizeof(work) - 1] = '\0';
		normalizeNewline(work);
		trimTrailingNewlines(work);
		char* tab = strchr(work, '\t');
		if (tab) {
			*tab = '\0';
			if (exactMatch(work, key)) {
				found = true;
				break;
			}
		}
	}
	fclose(f);
	return found;
}

// Upsert a display alias into a map.txt: rewrite `key`'s value to `value`, or
// append the pair if absent. Atomic via .tmp + rename. Used when renaming an
// aliased entry — the alias IS the shown name, so we edit it instead of the
// file. No-op only if the parent dir is unwritable (fopen of .tmp fails).
static void setMapAlias(const char* map_path, const char* key, const char* value) {
	char tmp_path[MAX_PATH];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", map_path);
	FILE* out = fopen(tmp_path, "w");
	if (!out)
		return;

	bool replaced = false;
	FILE* in = fopen(map_path, "r");
	if (in) {
		char line[MAX_PATH];
		while (fgets(line, sizeof(line), in) != NULL) {
			char work[MAX_PATH];
			strncpy(work, line, sizeof(work) - 1);
			work[sizeof(work) - 1] = '\0';
			normalizeNewline(work);
			trimTrailingNewlines(work);
			char* tab = strchr(work, '\t');
			if (tab) {
				*tab = '\0';
				if (exactMatch(work, key)) {
					fprintf(out, "%s\t%s\n", key, value);
					replaced = true;
					continue;
				}
			}
			fputs(line, out);
		}
		fclose(in);
	}
	if (!replaced)
		fprintf(out, "%s\t%s\n", key, value);
	fclose(out);
	rename(tmp_path, map_path);
}

// Remove a basename-keyed alias line from a map.txt (key<TAB>alias). Used on
// delete so a later file that reuses the name doesn't inherit the dead entry's
// alias. No-op if the file or the key is absent.
static void dropMapKey(const char* map_path, const char* key) {
	FILE* in = fopen(map_path, "r");
	if (!in)
		return;
	char tmp_path[MAX_PATH];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", map_path);
	FILE* out = fopen(tmp_path, "w");
	if (!out) {
		fclose(in);
		return;
	}

	bool changed = false;
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), in) != NULL) {
		char work[MAX_PATH];
		strncpy(work, line, sizeof(work) - 1);
		work[sizeof(work) - 1] = '\0';
		normalizeNewline(work);
		trimTrailingNewlines(work);
		char* tab = strchr(work, '\t');
		if (tab) {
			*tab = '\0';
			if (exactMatch(work, key)) {
				changed = true;
				continue; // drop the aliased line
			}
		}
		fputs(line, out);
	}
	fclose(in);
	fclose(out);

	if (changed)
		rename(tmp_path, map_path);
	else
		unlink(tmp_path);
}

// Arcade cores resolve the romset from the zip's filename (including
// parent/clone lookups), so physically renaming the file breaks loading.
// Rename must always edit the map.txt display alias for these emu tags —
// FBN ships with us; the rest are common community arcade paks.
static bool entryUsesRomsetNames(Entry* entry) {
	if (!prefixMatch(ROMS_PATH, entry->path))
		return false;
	char emu_name[MAX_PATH];
	getEmuName(entry->path, emu_name);
	return exactMatch(emu_name, "FBN") || exactMatch(emu_name, "FBNEO") ||
		   exactMatch(emu_name, "FBA") || exactMatch(emu_name, "NEOGEO") ||
		   prefixMatch("MAME", emu_name) || prefixMatch("CPS", emu_name);
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

	// oldbase = filename minus final extension; ext keeps the leading dot.
	// Folder games are directories, so the whole name is the base — a dotted
	// folder like "Game v1.1" must not be split on its last dot.
	bool is_dir_game = entry->type == ENTRY_DIR;
	char oldbase[MAX_PATH];
	strncpy(oldbase, filename, sizeof(oldbase) - 1);
	oldbase[sizeof(oldbase) - 1] = '\0';
	const char* fdot = is_dir_game ? NULL : strrchr(filename, '.');
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

	// 0) folder games: rename the inner folder-named .cue/.m3u (and matching
	// art in the folder's own .media) BEFORE the folder itself moves — the
	// old folder path must still be valid. Discs keep their names, so
	// cue/m3u contents that reference them stay correct.
	if (is_dir_game) {
		const char* game_exts[] = {".cue", ".m3u"};
		for (int i = 0; i < 2; i++) {
			char from[MAX_PATH];
			snprintf(from, sizeof(from), "%s/%s%s", entry->path, oldbase, game_exts[i]);
			if (exists(from)) {
				char to[MAX_PATH];
				snprintf(to, sizeof(to), "%s/%s%s", entry->path, newbase, game_exts[i]);
				rename(from, to);
			}
		}
		char inner_media[MAX_PATH];
		snprintf(inner_media, sizeof(inner_media), "%s/.media", entry->path);
		renameSweepDir(inner_media, oldbase, newbase);
	}

	// 1) the ROM folder itself (renames Game.gba + any Game.cue / Game.m3u,
	// or the game folder for a folder game)
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

	// 6) collections store this ROM by path; a file rename must rewrite those
	// lines or the game silently drops out (getCollection filters missing paths).
	// Only reached for non-aliased entries (doRename routes aliased renames to
	// the alias, leaving the file — and these paths — untouched), so there is no
	// display alias to fix up here.
	{
		char old_line[MAX_PATH] = "";
		char new_line[MAX_PATH] = "";

		if (is_dir_game) {
			// Collections store the resolved folder-named cue/m3u, not the
			// folder itself; find which extension this game uses (post-rename).
			const char* game_exts[] = {".cue", ".m3u"};
			for (int i = 0; i < 2; i++) {
				char cand[MAX_PATH];
				snprintf(cand, sizeof(cand), "%s/%s/%s%s", dir, newbase, newbase, game_exts[i]);
				if (exists(cand)) {
					// both the folder segment and the basename change
					snprintf(old_line, sizeof(old_line), "%s/%s/%s%s", dir, oldbase, oldbase, game_exts[i]);
					snprintf(new_line, sizeof(new_line), "%s/%s/%s%s", dir, newbase, newbase, game_exts[i]);
					break;
				}
			}
		} else {
			snprintf(old_line, sizeof(old_line), "%s/%s%s", dir, oldbase, ext);
			snprintf(new_line, sizeof(new_line), "%s/%s%s", dir, newbase, ext);
		}

		if (old_line[0] && prefixMatch(SDCARD_PATH, old_line) && prefixMatch(SDCARD_PATH, new_line))
			updateCollectionLines(old_line + strlen(SDCARD_PATH), new_line + strlen(SDCARD_PATH));
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
	snprintf(settings_path, sizeof(settings_path), "%s/Settings.pak", TOOLS_PATH);
	if (!exists(settings_path))
		snprintf(settings_path, sizeof(settings_path), "%s/Tools/Settings.pak", PAKS_PATH);
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
// rom_path is a file path: for folder games the caller passes the resolved
// folder-named cue/m3u (getCollection types every non-.pak line ENTRY_ROM,
// so a bare folder line would produce a broken entry).
static void addRomToCollectionFile(const char* collection_path, const char* rom_path) {
	if (!prefixMatch(SDCARD_PATH, rom_path))
		return;
	const char* rel = rom_path + strlen(SDCARD_PATH);

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

static void doAddToCollection(const char* rom_path) {
	Array* collections = getCollections();
	int pick = pickCollectionModal(collections);

	if (pick == COLLECTION_PICK_NEW) {
		// tg5050: release the DRM display before the external keyboard binary
		// grabs it, then recover after — without the release, recoverDisplay is
		// a no-op and the returning pageflip wedges black (mirrors Search_open)
		DisplayHelper_prepareForExternal();
		char* name = UIKeyboard_open("New collection name");
		DisplayHelper_recoverDisplay();
		if (name && strlen(name) > 0 && !strchr(name, '/')) {
			mkdir_p(COLLECTIONS_PATH);
			char coll_path[MAX_PATH];
			snprintf(coll_path, sizeof(coll_path), "%s/%s.txt", COLLECTIONS_PATH, name);
			addRomToCollectionFile(coll_path, rom_path);
		}
		if (name)
			free(name);
	} else if (pick >= 0 && pick < collections->count) {
		Entry* coll = collections->items[pick];
		addRomToCollectionFile(coll->path, rom_path);
	}

	EntryArray_free(collections);
}

static void doRename(Entry* entry, int sel) {
	char prompt[MAX_PATH];
	snprintf(prompt, sizeof(prompt), "Rename: %s", entry->name);
	// tg5050: release the DRM display before the external keyboard binary grabs
	// it, then recover after — without the release, recoverDisplay is a no-op and
	// the returning pageflip wedges the screen black (mirrors Search_open)
	DisplayHelper_prepareForExternal();
	char* newname = UIKeyboard_open(prompt);
	DisplayHelper_recoverDisplay();
	if (!newname || strlen(newname) == 0) {
		free(newname);
		return;
	}

	// The shown name may come from a map.txt display alias rather than the
	// filename. If so, the user is renaming what they SEE — the alias — so edit
	// the alias (map.txt is keyed by filename; see content.c Directory_index) and
	// leave the file, its saves/states/art and collection paths untouched.
	// Aliases are keyed by basename:
	//   list view  -> <rom dir>/map.txt keyed by the entry's own basename
	//   collection -> Collections/map.txt keyed by the collection line's basename
	//                 (the resolved cue/m3u for folder games)
	char parent_dir[MAX_PATH];
	strncpy(parent_dir, entry->path, sizeof(parent_dir) - 1);
	parent_dir[sizeof(parent_dir) - 1] = '\0';
	char* pslash = strrchr(parent_dir, '/');
	char home_key[MAX_PATH];
	strncpy(home_key, pslash ? pslash + 1 : parent_dir, sizeof(home_key) - 1);
	home_key[sizeof(home_key) - 1] = '\0';
	if (pslash)
		*pslash = '\0';
	char home_map[MAX_PATH];
	char coll_map[MAX_PATH];
	snprintf(home_map, sizeof(home_map), "%s/map.txt", parent_dir);
	snprintf(coll_map, sizeof(coll_map), "%s/map.txt", COLLECTIONS_PATH);

	char coll_key[MAX_PATH];
	char game_file[MAX_PATH];
	if (entryFolderGame(entry, game_file)) {
		char* gslash = strrchr(game_file, '/');
		strncpy(coll_key, gslash ? gslash + 1 : game_file, sizeof(coll_key) - 1);
	} else {
		strncpy(coll_key, home_key, sizeof(coll_key) - 1);
	}
	coll_key[sizeof(coll_key) - 1] = '\0';

	if (entryUsesRomsetNames(entry) || mapHasKey(home_map, home_key) || mapHasKey(coll_map, coll_key)) {
		// aliased (or arcade, where the filename IS the romset id and must not
		// change): rename the display name in both maps; the file stays put, so
		// saves/states/art/collection paths need no changes. setMapAlias creates
		// the map.txt on first use.
		setMapAlias(home_map, home_key, newname);
		setMapAlias(coll_map, coll_key, newname);
		reloadDirectoryAt(stack->count - 1, sel);
	} else if (!strchr(newname, '/')) {
		// no alias: rename the real file (+ sweep collection paths / saves / states)
		char new_path[MAX_PATH];
		if (renameRomFiles(entry, newname, new_path))
			reloadDirectoryAt(stack->count - 1, sel);
	}

	free(newname);
}

// Netplay-capable = the entry's owning emu pak ships a "netplay" marker
// file beside its launch.sh. Cached by path: this is called from the
// input poll and the hint bar every frame.
static char netplay_cap_path[MAX_PATH] = {0};
static bool netplay_cap = false;
static bool entryNetplayCapable(Entry* entry) {
	if (!entry)
		return false;
	if (exactMatch(netplay_cap_path, entry->path))
		return netplay_cap;
	strncpy(netplay_cap_path, entry->path, MAX_PATH - 1);
	netplay_cap_path[MAX_PATH - 1] = '\0';
	netplay_cap = false;
	// eligibility lands in the cache too (false for ineligible paths) so the
	// per-frame hint bar never re-stats folder-game probes for the same entry
	char game_file[MAX_PATH];
	if (entry->type != ENTRY_ROM && !entryFolderGame(entry, game_file))
		return false;
	if (!prefixMatch(ROMS_PATH, entry->path))
		return false;
	char emu_name[MAX_PATH];
	getEmuName(entry->path, emu_name);
	char pak_path[MAX_PATH];
	getEmuPath(emu_name, pak_path);
	char* slash = strrchr(pak_path, '/');
	if (slash) {
		strcpy(slash + 1, "netplay"); // replaces "launch.sh"; same dir
		netplay_cap = exists(pak_path);
	}
	return netplay_cap;
}

// Mirrors entryNetplayCapable: an emu pak opts into the pre-launch options
// editor by shipping options.sh beside its launch.sh. Single-slot cache
// because the context-menu builder can ask for this repeatedly.
static char emuopts_cap_path[MAX_PATH] = {0};
static bool emuopts_cap = false;
static bool entryEmuOptionsCapable(Entry* entry) {
	if (!entry)
		return false;
	if (exactMatch(emuopts_cap_path, entry->path))
		return emuopts_cap;
	strncpy(emuopts_cap_path, entry->path, MAX_PATH - 1);
	emuopts_cap_path[MAX_PATH - 1] = '\0';
	emuopts_cap = false;
	// eligibility lands in the cache too — see entryNetplayCapable
	char game_file[MAX_PATH];
	if (entry->type != ENTRY_ROM && !entryFolderGame(entry, game_file))
		return false;
	if (!prefixMatch(ROMS_PATH, entry->path))
		return false;
	char emu_name[MAX_PATH];
	getEmuName(entry->path, emu_name);
	char pak_path[MAX_PATH];
	getEmuPath(emu_name, pak_path);
	char* slash = strrchr(pak_path, '/');
	if (slash) {
		strcpy(slash + 1, "options.sh"); // replaces "launch.sh"; same dir
		emuopts_cap = exists(pak_path);
	}
	return emuopts_cap;
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
	case 2: { // Tools (root)
		openDirectory(TOOLS_PATH, 0);
		break;
	}
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
		if (entry) {
			char game_file[MAX_PATH];
			char parent_dir[MAX_PATH];
			bool folder_game = entryFolderGame(entry, game_file);
			if (entry->type != ENTRY_ROM && !folder_game)
				break;
			if (confirmModal("Delete ROM?", entry->name)) {
				if (folder_game)
					removeRecursive(entry->path);
				else if (isFolderGameFile(entry->path, parent_dir))
					// the folder-named cue/m3u IS the game (eg. selected from a
					// collection): take the whole folder, don't orphan the discs
					removeRecursive(parent_dir);
				else
					unlink(entry->path);
				// prune the deleted game from any collection that lists it
				// (folder games are stored as their resolved cue/m3u path)
				const char* del_line = folder_game ? game_file : entry->path;
				if (prefixMatch(SDCARD_PATH, del_line))
					updateCollectionLines(del_line + strlen(SDCARD_PATH), NULL);
				// drop its display alias too, so a future file that reuses the
				// name doesn't inherit the dead entry's alias (mirrors rename)
				{
					char adir[MAX_PATH];
					strncpy(adir, entry->path, sizeof(adir) - 1);
					adir[sizeof(adir) - 1] = '\0';
					char* aslash = strrchr(adir, '/');
					const char* home_key = aslash ? aslash + 1 : adir;
					const char* gslash = strrchr(del_line, '/');
					const char* coll_key = gslash ? gslash + 1 : del_line;
					char amap[MAX_PATH];
					if (aslash) {
						*aslash = '\0'; // adir -> parent dir; home_key still valid
						snprintf(amap, sizeof(amap), "%s/map.txt", adir);
						dropMapKey(amap, home_key);
					}
					snprintf(amap, sizeof(amap), "%s/map.txt", COLLECTIONS_PATH);
					dropMapKey(amap, coll_key);
				}
				reloadDirectoryAt(stack->count - 1, sel);
			}
		}
		break;
	case 33: // Rename Rom
		if (entry) {
			char game_file[MAX_PATH];
			if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file))
				doRename(entry, sel);
		}
		break;
	case 34: // Add to Collection
		if (entry) {
			char game_file[MAX_PATH];
			if (entry->type == ENTRY_ROM)
				doAddToCollection(entry->path);
			else if (entryFolderGame(entry, game_file))
				// collections hold file paths: write the resolved cue/m3u
				doAddToCollection(game_file);
		}
		break;
	case 35: // Launch with Netplay
		if (entry && entryNetplayCapable(entry)) {
			// snapshot before Entry_open: its directory fall-through can
			// rebuild the stack and free entry
			bool was_dir = entry->type == ENTRY_DIR;
			putFile(NETPLAY_LAUNCH_PATH, "1\n");
			Entry_open(entry);
			// folder games launch via openDirectory's auto-launch; if that
			// fell through (eg. empty m3u) nothing was queued and the flag
			// would stay armed until next boot — disarm it
			if (was_dir && !quit)
				unlink(NETPLAY_LAUNCH_PATH);
		}
		break;
	case 36: // Emulator Options (pre-launch editor)
		if (entry && entryEmuOptionsCapable(entry)) {
			// folder games: hand options.sh the resolved cue/m3u, not the
			// folder (a dotted folder name would derive a different rom key),
			// but keep last_path on the folder so loadLast reselects it
			char game_file[MAX_PATH];
			char* rom_arg = entry->path;
			if (entry->type == ENTRY_DIR && entryFolderGame(entry, game_file))
				rom_arg = game_file;
			char emu_name[MAX_PATH];
			getEmuName(entry->path, emu_name);
			char pak_path[MAX_PATH];
			getEmuPath(emu_name, pak_path);
			char* slash = strrchr(pak_path, '/');
			if (slash) {
				strcpy(slash + 1, "options.sh"); // replaces "launch.sh"; same dir
				// options.sh cd's to its own dir, so it must be invoked by the
				// absolute path getEmuPath already produced.
				openScript(pak_path, rom_arg, entry->path);
			}
		}
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
			// Tools must stay reachable here even when "Show Tools" is off:
			// Settings.pak lives inside Tools, so hiding Tools would
			// otherwise lock the user out of re-enabling it.
			if (!gl_simple_mode && hasTools()) {
				strncpy(items[idx].label, "Tools", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 2;
				idx++;
			}
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
			char game_file[MAX_PATH];
			if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file)) {
				strncpy(items[idx].label, "Delete Rom", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 32;
				idx++;
				strncpy(items[idx].label, "Rename Rom", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 33;
				idx++;
				strncpy(items[idx].label, "Add to Collection", CONTEXTMENU_MAX_TEXT);
				items[idx].id = 34;
				idx++;
				// Netplay launch lives on the Y button (with its own hint), so it
				// intentionally has no context-menu entry; case 35 stays as the
				// shared launch path the Y handler documents.
				if (entryEmuOptionsCapable(entry)) {
					strncpy(items[idx].label, "Emulator Options", CONTEXTMENU_MAX_TEXT);
					items[idx].id = 36;
					idx++;
				}
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
	// Y launches netplay-capable ROMs with netplay (works at root for
	// pinned games too — root Search moved to START for this)
	else if (total > 0 && PAD_justReleased(BTN_Y) && entryNetplayCapable(entry)) {
		// snapshot before Entry_open: its directory fall-through can rebuild
		// the stack and free entry
		bool was_dir = entry->type == ENTRY_DIR;
		putFile(NETPLAY_LAUNCH_PATH, "1\n");
		Entry_open(entry);
		// folder games: disarm if the auto-launch fell through (see case 35)
		if (was_dir && !quit)
			unlink(NETPLAY_LAUNCH_PATH);
		*dirty = true;
	}
	// START to search at root (was Y; pin/unpin lives in the context menu).
	// Use a tap, not a raw release: holding START + volume is the color-temp
	// combo (BTN_MOD_COLORTEMP), and that release must not open search.
	else if (stack->count == 1 && PAD_tappedStart(now)) {
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
			// snapshot before Entry_open: its directory fall-through can rebuild
			// the stack and free entry (same defect the Y / case-35 sites guard)
			bool was_dir = entry->type == ENTRY_DIR;
			Entry_open(entry);
			if (was_dir && !startgame) {
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
			right_pairs[p++] = "START";
			right_pairs[p++] = "SEARCH";
		}

		// navigation actions
		if (total == 0) {
			if (stack->count > 1) {
				right_pairs[p++] = "B";
				right_pairs[p++] = "BACK";
			}
		} else {
			bool netplay_hint = entryNetplayCapable(entry);
			if (stack->count > 1) {
				right_pairs[p++] = "B";
				right_pairs[p++] = "BACK";
			}
			if (netplay_hint) {
				right_pairs[p++] = "Y";
				right_pairs[p++] = "NETPLAY";
			}
			if (resume.can_resume) {
				right_pairs[p++] = "X";
				right_pairs[p++] = "RESUME";
			}
			right_pairs[p++] = "A";
			right_pairs[p++] = "OPEN";
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
				// This call site is the only place list_scroll resyncs (via
				// ScrollText_update's strcmp), so while it's gated off below a
				// context action that changes the selection (Delete/Rename Rom,
				// Remove Game, Pin, Tools, Refresh) would leave the state
				// describing the *old* title at the *old* row — and the guard
				// frames after close would then animate that stale band over the
				// correct list. Drop the state instead: ScrollText_clear empties
				// text and needs_scroll, so GameList_scrollBusy() goes false and
				// ScrollText_animateOnly cannot fire until a real (ungated)
				// ScrollText_render has refreshed last_x/last_y/last_font. Note
				// ScrollText_reset would NOT be safe here — it leaves
				// needsRender() true with those last_* fields stale.
				if (row_is_selected && ContextMenu_isOpen() &&
					strcmp(list_scroll.text, display_text) != 0)
					ScrollText_clear(&list_scroll);
				// Freeze the marquee while the context menu is up: its GPU path
				// presents the screen itself (ScrollText_render ->
				// GFX_scrollTextTexture -> PLAT_GPU_Flip), so it would show one
				// full vsync frame *after* nextui.c cleared LAYER_OVERLAY but
				// *before* ContextMenu_render — the undimmed, menu-less list =
				// a full-screen flash on every keypress. Passing NULL takes the
				// static/truncated path (no present), which is also the correct
				// modal behaviour. Same class of bug as 81a0c40a.
				UI_renderListItemText(screen,
									  (row_is_selected && !ContextMenu_isOpen())
										  ? &list_scroll
										  : NULL,
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
