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
#include "ui_loadingoverlay.h"
#include "ui_message.h"
#include "ui_contextmenu.h"
#include "ui_keyboard.h"
#include "ui_list.h"
#include "ui_listdialog.h"
#include "ui_pindialog.h"
#include "utils.h"
#include "wifi.h"

#include "collcount.h"
#include "collname.h"
#include "content.h"
#include "gameswitcher.h"
#include "imgloader.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gridview.h"
#include "rowview.h"
#include "home.h"
#include "infoband.h"
#include "launcher.h"
#include "menuart.h"
#include "menulogo.h"
#include "menutabs.h"
#include "recents.h"
#include "search.h"
#include "types.h"

#include <dirent.h>
#include <msettings.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool gl_simple_mode = false;

static ScrollTextState list_scroll = {0};
// Selection-pill slide animation: the highlight glides to the selected row
// instead of snapping. list_pill_target tracks the last target y so we only
// (re)start the glide when the selection actually moves (-1 = first render,
// which snaps).
static PillAnimState list_pill_anim = {0};
static int list_pill_target = -1;
// Previous selected index, to detect a wrap (last<->first) so the pill enters
// from the near edge in the travel direction instead of sliding the whole list.
static int list_pill_prev_sel = -1;
// Previous directory, to detect a page/list change (folder enter/exit) so the
// pill snaps to the new list's selection instead of gliding from the stale row.
static const void* list_pill_prev_top = NULL;
static unsigned list_pill_prev_gen = 0;

static bool had_thumb = false;
static int ox;
// Set once a main-menu tab without game art has cleared the thumbnail
static bool list_art_cleared = false;
static char folderBgPath[1024] = {0};
// last background type loaded; file-scope so it can be reset alongside
// folderBgPath when another screen clears the shared background surface
static int bgLastType = -1;
// Per-frame short-circuit for resolveAndLoadBackground: the same entry is
// re-rendered every frame during animation, and the full resolve does
// filesystem stats (Shortcuts_exists / exists) on the SD card each call.
// bgResolveForcedNames replays the only caller-visible side effect
// (*list_show_entry_names forced true when there is genuinely no background).
static char bgResolvePath[1024] = {0};
static bool bgResolveValid = false;
static bool bgResolveForcedNames = false;

void GameList_invalidateBackground(void) {
	// force resolveAndLoadBackground to reload next render — call this whenever
	// something else (eg. Search) clears the shared folder background, otherwise
	// the change-detection cache thinks it's still loaded and skips the reload
	folderBgPath[0] = '\0';
	bgLastType = -1;
	bgResolveValid = false;
}

void GameList_init(bool simple_mode) {
	gl_simple_mode = simple_mode;
}

bool GameList_scrollBusy(void) {
	return ScrollText_isScrolling(&list_scroll) || ScrollText_needsRender(&list_scroll);
}

// True while the selection pill is mid-glide — nextui.c forces a redraw each
// frame until it settles (like GameList_scrollBusy for the marquee).
bool GameList_pillAnimating(void) {
	return UI_pillAnimIsActive(&list_pill_anim);
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

	// Same entry as the last call (every frame of a glide/marquee redraw):
	// skip the resolve — and with it the per-frame SD-card stats below.
	// No entry (an empty list, or a main-menu tab without game art) is keyed
	// as "", which no entry path can be.
	const char* key = entry ? entry->path : "";
	if (bgResolveValid && exactMatch(key, bgResolvePath)) {
		if (bgResolveForcedNames)
			*list_show_entry_names = true;
		return;
	}
	strncpy(bgResolvePath, key, sizeof(bgResolvePath) - 1);
	bgResolvePath[sizeof(bgResolvePath) - 1] = '\0';
	bgResolveValid = true;
	bgResolveForcedNames = false;

	char defaultBgPath[512];
	snprintf(defaultBgPath, sizeof(defaultBgPath), "%s/bg.png", SDCARD_PATH);

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
		snprintf(bgPath, sizeof(bgPath), "%s/.media/%s/bg.png", TOOLS_PATH,
				 Shortcuts_getPakBasename(entry->path));
	} else if (exists(defaultBgPath)) {
		// default background is entry-independent: the change-skip below must
		// compare paths only, or the "already loaded" case would fall through
		// and clobber the caller's configured list_show_entry_names
		cmpPath = defaultBgPath;
		strncpy(bgPath, defaultBgPath, sizeof(bgPath) - 1);
	} else {
		// genuinely no background to show — the list needs its names. Drop a
		// background an earlier entry loaded (a game list's .media art, a
		// tool's), or it would stay behind a main-menu tab that shows none.
		if (folderBgPath[0]) {
			folderBgPath[0] = '\0';
			*lastType = -1;
			onBackgroundLoaded(NULL);
		}
		*list_show_entry_names = true;
		bgResolveForcedNames = true; // replay this on per-entry skips above
		return;
	}

	if (!cmpPath)
		return;

	// Skip if background hasn't changed
	int curType = (cmpPath == defaultBgPath) ? -1 : (entry ? entry->type : -1);
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

	int rc = GameList_rowCount();
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

bool GameList_entryIsFolderGame(Entry* entry) {
	char game_file[MAX_PATH];
	return entryFolderGame(entry, game_file);
}

static const char* ART_FETCH_EXCLUDED_TAGS[] = {"PORTS", "CUSTOM", NULL};

static bool artFetchTagExcluded(const char* tag) {
	for (int i = 0; ART_FETCH_EXCLUDED_TAGS[i]; i++)
		if (strcasecmp(tag, ART_FETCH_EXCLUDED_TAGS[i]) == 0)
			return true;
	return false;
}

// Resolve art-fetch details for `entry`. rom_to_hash/out_png/tag are MAX_PATH
// buffers. Returns true iff eligible: a ROM or folder-game whose console tag is
// not excluded. out_png follows nxredux art conventions:
//   flat rom:    <console>/.media/<rom-basename>.png
//   folder game: <console>/.media/<game-folder-name>.png
static bool entryArtInfo(Entry* entry, char* rom_to_hash, char* out_png, char* tag) {
	if (!entry)
		return false;

	char game_file[MAX_PATH];
	bool folder = false;
	if (entry->type == ENTRY_ROM) {
		snprintf(rom_to_hash, MAX_PATH, "%s", entry->path);
	} else if (entryFolderGame(entry, game_file)) {
		folder = true;
		snprintf(rom_to_hash, MAX_PATH, "%s", game_file);
	} else {
		return false;
	}

	getEmuName(entry->path, tag); // Roms/<Console (TAG)>/... -> "TAG" (or folder name)
	if (tag[0] == '\0' || artFetchTagExcluded(tag))
		return false;

	// out_png follows GameList_render's thumbnail-path derivation exactly (see
	// ROM_mediaArtPath) for BOTH flat and folder entries, so the fetched art lands
	// where the list looks for it.
	ROM_mediaArtPath(entry->path, out_png, MAX_PATH);
	return true;
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

// Prune one line across every Collections/*.txt. Lines that exactly match
// old_rel (an SD-relative path in the on-disk format addRomToCollectionFile
// writes: leading '/', no SDCARD_PATH prefix) are dropped. Only files that
// actually change are rewritten, via a .tmp + rename so a crash mid-write
// can't truncate a collection. Used on delete — a missing path would make
// the game silently drop out (getCollection filters missing paths).
static void removeCollectionLines(const char* old_rel) {
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
				changed = true; // drop the line
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

// Full-screen blocking confirm dialog. Returns true on A, false on B.
static bool confirmModal(const char* title, const char* subtitle) {
	return UI_confirmModal(screen, title, subtitle, NULL, true, false);
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
		char entered[PINDIALOG_PIN_LEN + 1];
		bool confirmed = UI_pinModal(screen, "Enter Settings PIN", error, entered, NULL, false, false);
		if (!confirmed)
			break;
		if (strcmp(entered, pin) == 0)
			allowed = true;
		else
			error = "Wrong PIN. Try again."; // re-init also resets digits to 0
	}
	GFX_clearLayers(LAYER_ALL);
	return allowed;
}

bool GameList_settingsPinAllows(Entry* entry) {
	return settingsPinAllows(entry);
}

typedef struct {
	int chosen;
} PickCollectionCtx;

static void pickCollectionModal_render(SDL_Surface* screen, void* vctx) {
	(void)vctx;
	ListDialog_render(screen);
}

static int pickCollectionModal_handle(void* vctx) {
	PickCollectionCtx* ctx = vctx;
	ListDialogResult r = ListDialog_handleInput();
	if (r.action == LISTDIALOG_SELECTED) {
		ctx->chosen = r.index;
		return 1;
	}
	if (r.action == LISTDIALOG_CANCEL)
		return 0;
	// any held/pressed button may have moved the selection
	return PAD_anyPressed() ? UI_MODAL_DIRTY : UI_MODAL_CONTINUE;
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

	PickCollectionCtx ctx = {.chosen = -1};
	UI_ModalOpts opts = {
		.screen = screen,
		.render = pickCollectionModal_render,
		.handle = pickCollectionModal_handle,
		.ctx = &ctx,
		.quit_flag = NULL,
		.timeout_ms = 0,
		.clear_layers = true,
		.reset_pad = false,
	};
	UI_modalLoop(&opts);
	ListDialog_quit();

	if (ctx.chosen < 0)
		return -1;
	if (ctx.chosen == n)
		return COLLECTION_PICK_NEW;
	return ctx.chosen;
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
		char* name = UIKeyboard_open("New collection name");
		requestBackgroundReupload(); // keyboard cleared the layers, same row stays
		if (name && strlen(name) > 0 && !strchr(name, '/')) {
			mkdir_p(COLLECTIONS_PATH);
			char coll_path[MAX_PATH];
			snprintf(coll_path, sizeof(coll_path), "%s/%s.txt", COLLECTIONS_PATH, name);
			addRomToCollectionFile(coll_path, rom_path);
			CollCount_invalidate(coll_path);
		}
		if (name)
			free(name);
	} else if (pick >= 0 && pick < collections->count) {
		Entry* coll = collections->items[pick];
		addRomToCollectionFile(coll->path, rom_path);
		CollCount_invalidate(coll->path); // the stamps would catch it too; FAT mtime is 2 s coarse
	}

	EntryArray_free(collections);
}

// Returns true when the display name was renamed, so the caller knows to
// refresh other views (eg. a pinned copy at root).
//
// A rename NEVER touches the ROM file: it edits the map.txt display alias
// (created on first rename; keyed by filename, not path — see content.c
// Directory_index). Filenames stay stable, so nothing keyed by them — saves,
// states, art, cheat lookups (ma_cheats.c), play-time records, collection
// paths, netplay peer matching — can be broken or orphaned by a rename.
// Arcade cores REQUIRE this (the zip filename is the romset id the core
// loads by); every other core just shares the same behavior.
// Aliases are keyed by basename:
//   list view  -> <rom dir>/map.txt keyed by the entry's own basename
//   collection -> Collections/map.txt keyed by the collection line's basename
//                 (the resolved cue/m3u for folder games)
static bool doRename(Entry* entry, int sel) {
	char prompt[MAX_PATH];
	snprintf(prompt, sizeof(prompt), "Rename: %s", entry->name);
	// start from the name the row shows (e.g. "Alien vs. Predator (Japan)"
	// for an arcade clone), so a small edit doesn't mean retyping it all
	const char* current = entry->unique ? entry->unique : entry->name;
	char* newname = UIKeyboard_openWith(prompt, current, 0);
	requestBackgroundReupload(); // keyboard cleared the layers, same row stays
	// confirming the untouched name is a cancel: writing it as an alias would
	// pin today's label into map.txt for nothing
	if (!newname || strlen(newname) == 0 || exactMatch(newname, current)) {
		free(newname);
		return false;
	}
	// aliases feed cheat-file paths ('/' would escape the Cheats dir) and a
	// leading '.' would hide() the entry from every list
	if (strchr(newname, '/') || newname[0] == '.') {
		free(newname);
		return false;
	}

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
	bool folder_game = entryFolderGame(entry, game_file);
	if (folder_game) {
		char* gslash = strrchr(game_file, '/');
		strncpy(coll_key, gslash ? gslash + 1 : game_file, sizeof(coll_key) - 1);
	} else {
		strncpy(coll_key, home_key, sizeof(coll_key) - 1);
	}
	coll_key[sizeof(coll_key) - 1] = '\0';

	setMapAlias(home_map, home_key, newname);
	setMapAlias(coll_map, coll_key, newname);

	// Recently Played and the game switcher render an alias snapshot taken at
	// launch (recent.txt: path<TAB>alias); re-point it so the new name shows
	// there before the game's next launch. The stored path is the launched
	// file — the resolved cue/m3u for a folder game, redirected to a sibling
	// .m3u when one exists (see openRom's recent_path).
	char launched[MAX_PATH];
	strncpy(launched, folder_game ? game_file : entry->path, sizeof(launched) - 1);
	launched[sizeof(launched) - 1] = '\0';
	char m3u_path[MAX_PATH];
	if (hasM3u(launched, m3u_path)) {
		strncpy(launched, m3u_path, sizeof(launched) - 1);
		launched[sizeof(launched) - 1] = '\0';
	}
	if (prefixMatch(SDCARD_PATH, launched))
		Recents_updateAlias(launched + strlen(SDCARD_PATH), newname);

	reloadDirectoryAt(stack->count - 1, sel);
	free(newname);
	return true;
}

///////////////////////////////////////
// Collections: Rename and Delete (spec 2026-10-01-menu-sp7-page-titles-design.md, "Collections")

// A row of the Collections tab: a Collections/<name>.txt at the root. Simple mode never renames or deletes one
// (kids can't remove the curated collections), so its menu items, hint and actions all key off this.
static bool isCollectionRow(Entry* entry) {
	size_t n = strlen(COLLECTIONS_PATH);
	return !gl_simple_mode && entry && stack->count == 1 && exactMatch(top->path, COLLECTIONS_PATH) && entry->type == ENTRY_DIR &&
		   suffixMatch(".txt", entry->path) && strncmp(entry->path, COLLECTIONS_PATH, n) == 0 &&
		   entry->path[n] == '/' && !strchr(entry->path + n + 1, '/');
}

// A collection's name: its file name without ".txt".
static void collectionStem(const char* path, char* out /* MAX_PATH */) {
	const char* slash = strrchr(path, '/');
	snprintf(out, MAX_PATH, "%s", slash ? slash + 1 : path);
	size_t n = strlen(out);
	if (n > 4 && suffixMatch(".txt", out))
		out[n - 4] = '\0';
}

// A one-button notice (A or B closes it).
static void collectionNotice(const char* title, const char* subtitle) {
	UI_confirmModalHints(screen, title, subtitle, (char*[]){"A", "OK", NULL}, NULL, true, false);
}

// Move the file. A case-only change goes old -> <new>.txt.part -> <new>.txt: on case-insensitive storage (the FAT
// card) a direct rename to the same name may do nothing. getCollections finishes a .part left by a cut-short rename.
static bool moveCollectionFile(const char* old_path, const char* new_path, bool case_only) {
	if (!case_only) {
		if (exists((char*)new_path))
			return false; // a file the list doesn't show (the validator saw only the listed names)
		return rename(old_path, new_path) == 0;
	}
	char part_path[MAX_PATH];
	snprintf(part_path, sizeof(part_path), "%s.part", new_path);
	if (exists(part_path) || rename(old_path, part_path) != 0)
		return false;
	if (rename(part_path, new_path) != 0) {
		rename(part_path, old_path); // put it back; else the next listing finishes it
		return false;
	}
	return true;
}

// The root row showing `path` (after a reload), or -1.
static int rootRowFor(const char* path) {
	Directory* root = stack->items[0];
	for (int i = 0; i < root->entries->count; i++)
		if (exactMatch(((Entry*)root->entries->items[i])->path, path))
			return i;
	return -1;
}

// Rename: the keyboard prefilled with the name; a rejected name says why and reopens the keyboard with it. The
// games and their order stay (the file is moved, not rewritten). Returns true when the collection was renamed.
static bool doRenameCollection(Entry* entry, int root_sel) {
	char old_path[MAX_PATH], old_stem[MAX_PATH];
	snprintf(old_path, sizeof(old_path), "%s", entry->path);
	collectionStem(old_path, old_stem);

	Array* colls = getCollections();
	int n = colls->count;
	char (*stems)[MAX_PATH] = n ? malloc((size_t)n * MAX_PATH) : NULL;
	const char** names = n ? malloc((size_t)n * sizeof(char*)) : NULL;
	if (n && (!stems || !names))
		n = 0;
	for (int i = 0; i < n; i++) {
		collectionStem(((Entry*)colls->items[i])->path, stems[i]);
		names[i] = stems[i];
	}
	EntryArray_free(colls);

	char* attempt = strdup(old_stem);
	char* newname = NULL;
	CollNameResult r = COLLNAME_SAME;
	while (attempt) {
		newname = UIKeyboard_openWith("Rename collection", attempt, 0);
		requestBackgroundReupload(); // the keyboard cleared the layers
		free(attempt);
		attempt = NULL;
		if (!newname) // B, or an emptied line
			break;
		r = CollName_validate(old_stem, newname, names, n);
		if (r == COLLNAME_OK || r == COLLNAME_SAME)
			break;
		collectionNotice("Can't use that name", CollName_reason(r));
		attempt = newname; // back to the keyboard with what was typed
		newname = NULL;
	}
	free(stems);
	free(names);
	if (!newname || r != COLLNAME_OK) {
		free(newname);
		return false;
	}

	char new_path[MAX_PATH];
	snprintf(new_path, sizeof(new_path), "%s/%s.txt", COLLECTIONS_PATH, newname);
	if (!moveCollectionFile(old_path, new_path, CollName_isCaseOnly(old_stem, newname))) {
		LOG_warn("Collections: rename %s -> %s failed\n", old_path, new_path);
		collectionNotice("Couldn't rename the collection", newname);
		free(newname);
		return false;
	}

	// The old name's alias in Collections/map.txt would hide the new name (an alias is the shown name): drop it.
	char coll_map[MAX_PATH];
	snprintf(coll_map, sizeof(coll_map), "%s/map.txt", COLLECTIONS_PATH);
	dropMapKey(coll_map, strrchr(old_path, '/') + 1);
	// a pin of the collection follows it, in its place
	char new_name[MAX_PATH];
	getDisplayName(new_path, new_name);
	Shortcuts_replacePath(old_path + strlen(SDCARD_PATH), new_path + strlen(SDCARD_PATH), new_name);
	CollCount_invalidate(old_path);
	CollCount_invalidate(new_path);

	// the highlight follows the renamed row (the list is sorted by name, so it can move)
	MenuTabs_reload(root_sel);
	int row = rootRowFor(new_path);
	Directory* root = stack->items[0];
	if (row >= 0 && row != root->selected) { // windowed as reloadDirectoryAt
		int total = root->entries->count, rc = GameList_rowCount();
		root->selected = row;
		if (row >= root->end) { // below the window: it ends on the row
			root->end = row + 1;
			root->start = root->end > rc ? root->end - rc : 0;
		} else if (row < root->start) { // above it: it starts on the row
			root->start = row;
			root->end = row + rc < total ? row + rc : total;
		}
	}
	free(newname);
	return true;
}

// Delete: only Collections/<name>.txt goes (the games stay), with its alias, pins and count. The highlight lands on
// the next row (the same index), else the previous one; deleting the last collection drops the tab.
static void doDeleteCollection(Entry* entry, int root_sel) {
	char title[MAX_PATH + 16];
	snprintf(title, sizeof(title), "Delete '%s'?", entry->name);
	if (!UI_confirmModalHints(screen, title, "The games stay in your library.",
							  (char*[]){"B", "CANCEL", "A", "DELETE", NULL}, NULL, true, false))
		return;
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s", entry->path);
	if (unlink(path) != 0) {
		LOG_warn("Collections: delete %s failed\n", path);
		collectionNotice("Couldn't delete the collection", entry->name);
		return;
	}
	char coll_map[MAX_PATH];
	snprintf(coll_map, sizeof(coll_map), "%s/map.txt", COLLECTIONS_PATH);
	dropMapKey(coll_map, strrchr(path, '/') + 1);
	Shortcuts_remove(entry);
	CollCount_invalidate(path);
	MenuTabs_reload(root_sel); // entry is freed from here on
}

// Resolve <emu-pak-dir>/<marker> for `entry` — the marker file an emu pak ships
// beside its launch.sh to opt into a feature. Returns false if no pak path can
// be formed. Shared by entryEmuMarker (capability probes) and case 36 below.
static bool entryEmuMarkerPath(Entry* entry, const char* marker, char* out_path) {
	char emu_name[MAX_PATH];
	getEmuName(entry->path, emu_name);
	getEmuPath(emu_name, out_path);
	char* slash = strrchr(out_path, '/');
	if (!slash)
		return false;
	strcpy(slash + 1, marker); // replaces "launch.sh"; same dir
	return true;
}

// Single-slot, per-entry-path cache of a marker probe: does `entry`'s owning emu
// pak ship `marker` beside its launch.sh? The result (including false for
// ineligible paths) is cached — these are called from the input poll and the
// hint bar every frame — so the same entry is never re-stat'd.
static bool entryEmuMarker(Entry* entry, const char* marker, char* cache_path, bool* cache_val) {
	if (!entry)
		return false;
	if (exactMatch(cache_path, entry->path))
		return *cache_val;
	strncpy(cache_path, entry->path, MAX_PATH - 1);
	cache_path[MAX_PATH - 1] = '\0';
	*cache_val = false;
	// eligibility lands in the cache too (false for ineligible paths) so the
	// per-frame hint bar never re-stats folder-game probes for the same entry
	char game_file[MAX_PATH];
	if (entry->type != ENTRY_ROM && !entryFolderGame(entry, game_file))
		return false;
	if (!prefixMatch(ROMS_PATH, entry->path))
		return false;
	char pak_path[MAX_PATH];
	if (entryEmuMarkerPath(entry, marker, pak_path))
		*cache_val = exists(pak_path);
	return *cache_val;
}

// Netplay-capable = the entry's owning emu pak ships a "netplay" marker file
// beside its launch.sh.
static char netplay_cap_path[MAX_PATH] = {0};
static bool netplay_cap = false;
static bool entryNetplayCapable(Entry* entry) {
	return entryEmuMarker(entry, "netplay", netplay_cap_path, &netplay_cap);
}

// Exported for search.c: same one-entry cache, same marker probe.
bool GameList_entryNetplayCapable(Entry* entry) {
	return entryNetplayCapable(entry);
}

// Mirrors entryNetplayCapable: an emu pak opts into the pre-launch options
// editor by shipping options.sh beside its launch.sh.
static char emuopts_cap_path[MAX_PATH] = {0};
static bool emuopts_cap = false;
static bool entryEmuOptionsCapable(Entry* entry) {
	return entryEmuMarker(entry, "options.sh", emuopts_cap_path, &emuopts_cap);
}

#define ARTFETCH_STATUS_PATH "/tmp/nextui_artfetch.status"
#define ARTFETCH_TIMEOUT_MS 60000
#define ARTFETCH_RESULT_MS 1500

// Fit a game name onto one line of the modal title (font.large), ellipsizing
// when it's wider than the modal (the loading overlay assumes a single-line
// title, so an unbounded name would wrap into the subtitle). UTF-8 aware.
static void artFetchTitle(const char* name, char* out, size_t out_size) {
	int maxw = screen->w - SCALE1(PADDING * 6);
	snprintf(out, out_size, "%s", name);
	int w = 0;
	GFX_measureText(font.large, out, &w, NULL);
	if (w <= maxw)
		return;
	size_t len = strlen(out);
	while (len > 0) {
		len--;
		while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80)
			len--; // don't split a UTF-8 sequence
		char cand[256];
		snprintf(cand, sizeof(cand), "%.*s...", (int)len, name);
		GFX_measureText(font.large, cand, &w, NULL);
		if (w <= maxw) {
			snprintf(out, out_size, "%s", cand);
			return;
		}
	}
}

typedef struct {
	const char* title;
	const char* subtitle;
} ArtFetchNoticeCtx;

static void artFetchNotice_render(SDL_Surface* screen, void* vctx) {
	ArtFetchNoticeCtx* ctx = vctx;
	GFX_clear(screen);
	UI_renderLoadingOverlay(screen, ctx->title, ctx->subtitle);
}

static int artFetchNotice_handle(void* vctx) {
	(void)vctx;
	return PAD_justPressed(BTN_B) ? 0 : UI_MODAL_CONTINUE;
}

// Blocking notice shown on the same modal for up to ARTFETCH_RESULT_MS (B
// dismisses early). Used for the pre-flight "no WiFi" message.
static void artFetchNotice(const char* game_name, const char* subtitle) {
	char title[256];
	artFetchTitle(game_name, title, sizeof(title));
	ArtFetchNoticeCtx ctx = {title, subtitle};
	UI_ModalOpts opts = {
		.screen = screen,
		.render = artFetchNotice_render,
		.handle = artFetchNotice_handle,
		.ctx = &ctx,
		.quit_flag = NULL,
		.timeout_ms = ARTFETCH_RESULT_MS,
		.clear_layers = false,
		.reset_pad = true,
	};
	UI_modalLoop(&opts);
	GFX_clearLayers(LAYER_ALL);
}

typedef struct {
	unsigned long start;
	char stage[16];
	const char* result; // terminal/cancel message once set
	unsigned long result_until;
	const char* title;
	const char* out_png;
} ArtFetchModalCtx;

static void artFetchModal_render(SDL_Surface* screen, void* vctx) {
	ArtFetchModalCtx* ctx = vctx;
	const char* sub = ctx->result;
	if (!sub) {
		if (strcmp(ctx->stage, "searching") == 0)
			sub = "Searching...";
		else if (strcmp(ctx->stage, "downloading") == 0)
			sub = "Downloading...";
		else if (strcmp(ctx->stage, "compositing") == 0)
			sub = "Adding art...";
		else
			sub = "Starting...";
	}
	GFX_clear(screen);
	UI_renderLoadingOverlay(screen, ctx->title, sub);
}

static int artFetchModal_handle(void* vctx) {
	ArtFetchModalCtx* ctx = vctx;
	unsigned long now = SDL_GetTicks();

	if (!ctx->result) {
		if (PAD_justPressed(BTN_B)) {
			ctx->result = "Cancelled";
		} else {
			char status[32] = "";
			getFile(ARTFETCH_STATUS_PATH, status, sizeof(status));
			char* nl = strchr(status, '\n');
			if (nl)
				*nl = '\0';

			if (strcmp(status, "done") == 0) {
				thumbCacheInvalidate(ctx->out_png);
				ctx->result = "Box art added";
			} else if (strcmp(status, "notfound") == 0) {
				ctx->result = "No art found";
			} else if (strcmp(status, "error") == 0) {
				ctx->result = "Art fetch failed";
			} else if (now - ctx->start > ARTFETCH_TIMEOUT_MS) {
				ctx->result = "Art fetch timed out";
			} else if (status[0] && strcmp(status, ctx->stage) != 0) {
				snprintf(ctx->stage, sizeof(ctx->stage), "%s", status);
				return UI_MODAL_DIRTY;
			}
		}
		if (ctx->result) {
			ctx->result_until = now + ARTFETCH_RESULT_MS;
			return UI_MODAL_DIRTY;
		}
	} else if (now >= ctx->result_until) {
		return 0;
	}
	return UI_MODAL_CONTINUE;
}

// Blocking box-art fetch modal. Shows staged progress while the spawned scraper
// runs, polling the status file each frame; B cancels (the background scraper is
// left to finish on its own). On "done" the thumbnail cache is invalidated so
// the game list reloads the new art when the modal closes (nextui.c marks the
// list dirty after runContextAction). Mirrors the blocking-modal idiom used by
// Delete/Rename in this same dispatcher.
static void artFetchModal(const char* game_name, const char* out_png) {
	char title[256];
	artFetchTitle(game_name, title, sizeof(title));
	ArtFetchModalCtx ctx = {
		.start = SDL_GetTicks(),
		.stage = "starting",
		.title = title,
		.out_png = out_png,
	};
	UI_ModalOpts opts = {
		.screen = screen,
		.render = artFetchModal_render,
		.handle = artFetchModal_handle,
		.ctx = &ctx,
		.quit_flag = NULL,
		.timeout_ms = 0,
		.clear_layers = false,
		.reset_pad = true,
	};
	UI_modalLoop(&opts);

	unlink(ARTFETCH_STATUS_PATH);
	GFX_clearLayers(LAYER_ALL);
}

static Entry* ctx_target = NULL; // Home's entry while its menu is open (an owned copy)

void GameList_contextMenuClosed(void) {
	if (ctx_target)
		Entry_free(ctx_target);
	ctx_target = NULL;
}

// Dispatch a selected context-menu item (ids assigned in GameList_handleInput).
// Runs in nextui.c's main loop; blocking modals here are safe (the flip is
// synchronous, and UIKeyboard_open already blocks mid-loop from Search).
void GameList_runContextAction(int id) {
	int sel = top->selected;
	// a menu opened from Home acts on Home's entry (an owned copy), not the pins list's row
	bool from_home = ctx_target != NULL;
	Entry* entry = from_home															? ctx_target
				   : (top->entries->count > 0 && sel >= 0 && sel < top->entries->count) ? top->entries->items[sel]
																						: NULL;
	int root_sel = ((Directory*)stack->items[0])->selected;

	switch (id) {
	case 1: // Refresh Roms (root)
		Content_invalidateEmulist();
		MenuTabs_reload(root_sel);
		break;
	case 2: { // Tools (root, offered while the Tools tab is hidden)
		// Push the Tools folder over the current tab so B comes back to it.
		// Tools is a direct child of the Home tab only; from any other tab
		// openDirectory would rebuild the stack on Home.
		Directory* tools = Directory_new(TOOLS_PATH, 0);
		int rc = GameList_rowCount();
		int count = tools->entries->count;
		tools->start = 0;
		tools->end = (count < rc) ? count : rc;
		MenuTabs_setFocused(false); // a list opened: B back to the root returns to the content
		Array_push(stack, tools);
		top = tools;
		break;
	}
	case 20: // Pin Tool
	case 30: // Pin Item
		if (entry) {
			Shortcuts_add(entry);
			MenuTabs_reload(root_sel);
			if (stack->count > 1)
				reloadDirectoryAt(stack->count - 1, sel);
		}
		break;
	case 3:	 // Unpin (root pinned row)
	case 21: // Unpin Tool
	case 31: // Unpin Item
		if (entry) {
			Shortcuts_remove(entry);
			MenuTabs_reload(root_sel);
			if (stack->count > 1)
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
				bool caches_fresh = Content_romCachesFresh();
				const char* removed = entry->path;
				if (folder_game)
					removeRecursive(entry->path);
				else if (isFolderGameFile(entry->path, parent_dir)) {
					// the folder-named cue/m3u IS the game (eg. selected from a
					// collection): take the whole folder, don't orphan the discs
					removeRecursive(parent_dir);
					removed = parent_dir;
				} else
					unlink(entry->path);
				// prune the deleted game from any collection that lists it
				// (folder games are stored as their resolved cue/m3u path)
				const char* del_line = folder_game ? game_file : entry->path;
				if (prefixMatch(SDCARD_PATH, del_line))
					removeCollectionLines(del_line + strlen(SDCARD_PATH));
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
				// drop it from the rom index so console counts and Search follow (after the
				// map.txt clean-up: both feed the index fingerprint)
				Content_forgetRom(removed, caches_fresh);
				// root too: a pinned copy of the deleted rom must not linger
				// as a dead shortcut (mirrors pin/unpin)
				MenuTabs_reload(root_sel);
				if (stack->count > 1)
					reloadDirectoryAt(stack->count - 1, sel);
			}
		}
		break;
	case 33: // Rename Rom
		if (entry) {
			char game_file[MAX_PATH];
			if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file))
				if (doRename(entry, sel))
					// root too: refresh any pinned copy (mirrors pin/unpin)
					MenuTabs_reload(root_sel);
		}
		break;
	case 40: // Rename (a Collections-tab row)
		if (isCollectionRow(entry))
			doRenameCollection(entry, root_sel);
		break;
	case 41: // Delete (a Collections-tab row)
		if (isCollectionRow(entry))
			doDeleteCollection(entry, root_sel);
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
			putFile(NETPLAY_LAUNCH_PATH, "1\n");
			if (from_home)
				MenuTabs_markHomeLaunch(); // Entry_open clears it
			Entry_open(entry);
			// if no launch was queued (folder auto-launch fell through, or any
			// early-out in the rom path) the flag would stay armed and turn the
			// next unrelated launch into a netplay launch — disarm it
			if (!quit)
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
			char pak_path[MAX_PATH];
			if (entryEmuMarkerPath(entry, "options.sh", pak_path)) {
				// options.sh cd's to its own dir, so it must be invoked by the
				// absolute path getEmuPath already produced.
				// From Home (Continue or a pin) the return must open Home, as for its own launches;
				// saveLast consumes the mark, and a script that never launched must not leave it set.
				if (from_home)
					MenuTabs_markHomeLaunch();
				openScript(pak_path, rom_arg, entry->path);
				MenuTabs_clearHomeLaunch();
			}
		}
		break;
	case 37: // Fetch Box Art
		if (entry) {
			char af_rom[MAX_PATH], af_out[MAX_PATH], af_tag[MAX_PATH];
			if (!entryArtInfo(entry, af_rom, af_out, af_tag))
				break;
			if (!Wifi_isConnected()) {
				artFetchNotice(entry->name, "Connect to WiFi to fetch art");
				break;
			}
			putFile(ARTFETCH_STATUS_PATH, "starting");
			openArtFetch(af_rom, af_out, af_tag, ARTFETCH_STATUS_PATH);
			artFetchModal(entry->name, af_out); // blocking modal until done/cancel/timeout
		}
		break;
	default:
		break;
	}

	// Home rebuilds its Continue card and pins (pin, unpin, delete and rename all change them)
	if (from_home) {
		GameList_contextMenuClosed();
		Home_reset();
	}

	// Actions can swap `top` or its selection (a reload clamps the row, an
	// unpin changes the Home list, Tools pushes a folder): recompute the
	// resume state for whatever is selected now, so the X hint and handler
	// don't act on the previous row. readyResume(NULL) clears it.
	bool has_row = top->entries->count > 0 && top->selected >= 0 && top->selected < top->entries->count;
	readyResume(has_row ? top->entries->items[top->selected] : NULL);
}

// After the root tab changed: reset what belonged to the old one.
static void tabChanged(GameListResult* result, bool* dirty) {
	GFX_clearLayers(LAYER_SCROLLTEXT);
	ScrollText_clear(&list_scroll);
	result->folderbgchanged = true;
	*dirty = true;
	// an empty tab clears the previous tab's resume state
	readyResume(top->entries->count > 0 ? top->entries->items[top->selected] : NULL);
}

// L1/R1 (and LEFT/RIGHT) at the root: step the tab and reset what belonged to the old one.
static void switchTab(int delta, GameListResult* result, bool* dirty) {
	if (MenuTabs_step(delta))
		tabChanged(result, dirty);
}

// Home's edge moves and "Pick a game". The caller (GameList_handleInput) sees the tab generation
// change and requests the background refresh itself.
void GameList_switchTab(int delta, bool* dirty) {
	GameListResult ignored = {0};
	switchTab(delta, &ignored, dirty);
}

void GameList_openTab(MenuTabId id, bool* dirty) {
	if (!MenuTabs_isVisible(id) || stack->count != 1)
		return;
	GameListResult ignored = {0};
	MenuTabs_openRoot(id);
	tabChanged(&ignored, dirty);
}

///////////////////////////////////////
// Context-menu items

static void addItem(ContextMenuItem* items, int* idx, const char* label, int id) {
	if (*idx >= CONTEXTMENU_MAX_ITEMS)
		return;
	snprintf(items[*idx].label, CONTEXTMENU_MAX_TEXT, "%s", label);
	items[*idx].id = id;
	(*idx)++;
}

// A game's items, as in a ROM listing. allow_pin false leaves out Pin/Unpin Item.
static void romItems(Entry* entry, bool allow_pin, ContextMenuItem* items, int* idx) {
	if (allow_pin && canPinEntry(entry)) {
		if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH)))
			addItem(items, idx, "Unpin Item", 31);
		else
			addItem(items, idx, "Pin Item", 30);
	}
	char game_file[MAX_PATH];
	if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file)) {
		addItem(items, idx, "Delete Rom", 32);
		addItem(items, idx, "Rename Rom", 33);
		addItem(items, idx, "Add to Collection", 34);
		// Netplay launch lives on the Y button (with its own hint), so it
		// intentionally has no context-menu entry; case 35 stays as the
		// shared launch path the Y handler documents.
		if (entryEmuOptionsCapable(entry))
			addItem(items, idx, "Emulator Options", 36);
		char af_rom[MAX_PATH], af_out[MAX_PATH], af_tag[MAX_PATH];
		if (entryArtInfo(entry, af_rom, af_out, af_tag) && !exists(af_out))
			addItem(items, idx, "Fetch Box Art", 37);
	}
}

// A tool's item (Tools tab, Tools listing, a pinned tool on Home).
static void toolItems(Entry* entry, ContextMenuItem* items, int* idx) {
	if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH)))
		addItem(items, idx, "Unpin Tool", 21);
	else
		addItem(items, idx, "Pin Tool", 20);
}

// The main menu's own items.
static void rootItems(ContextMenuItem* items, int* idx) {
	addItem(items, idx, "Refresh Roms", 1);
	// Tools must stay reachable here even when "Show Tools" is off:
	// Settings.pak lives inside Tools, so hiding Tools would
	// otherwise lock the user out of re-enabling it. With a Tools
	// tab it is one R1 away, so the item is left out.
	if (!gl_simple_mode && hasTools() && !MenuTabs_isVisible(MENU_TAB_TOOLS))
		addItem(items, idx, "Tools", 2);
}

void GameList_openContextMenuFor(Entry* entry, bool is_pin, bool is_continue) {
	(void)is_continue; // Continue gets the same items as the game in a list
	if (!entry)
		return;
	ContextMenuItem items[CONTEXTMENU_MAX_ITEMS];
	int idx = 0;
	char game_file[MAX_PATH];
	if (entry->type == ENTRY_PAK) {
		// simple mode: kids can't unpin the curated shortcuts (as on the Tools tab)
		if (!gl_simple_mode)
			toolItems(entry, items, &idx);
	} else if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file)) {
		// Home is the root: simple mode hides Pin/Unpin there, as the root's Unpin
		romItems(entry, !gl_simple_mode, items, &idx);
	} else if (is_pin && !gl_simple_mode && Shortcuts_exists(entry->path + strlen(SDCARD_PATH))) {
		addItem(items, &idx, "Unpin", 3); // a legacy pinned folder
	}
	rootItems(items, &idx);
	if (idx == 0)
		return;
	GameList_contextMenuClosed();
	ctx_target = Entry_newNamed(entry->path, entry->type, entry->name);
	if (entry->unique)
		ctx_target->unique = strdup(entry->unique);
	ContextMenu_open(items, idx);
}

// UP on the tab row: focus returns to the bottom of the content (List's last row, Grid's bottom row in the same
// column, Home's last pin; Carousel and Backdrop are one row, so the item stays).
static void contentToBottom(void) {
	if (Home_active()) {
		Home_focusBottom();
		return;
	}
	if (RowView_active())
		return;
	int total = top->entries->count;
	if (total <= 0)
		return;
	if (GridView_active()) {
		GridView_focusBottom();
	} else { // List: the last row, windowed as the List's own wrap to the bottom
		int rc = GameList_rowCount();
		top->selected = total - 1;
		top->start = total > rc ? total - rc : 0;
		top->end = total;
	}
	readyResume(top->entries->items[top->selected]);
}

// L1/R1 at the root, a fresh press: -1 / +1 to step the tab, else 0. Not while the other shoulder is held (both together
// are a combo) or while the press belongs to a setting indicator (PWR_ignoreSettingInput).
static int shoulderTabDelta(IndicatorType show_setting) {
	if (!PAD_justPressed(BTN_L1) && !PAD_justPressed(BTN_R1))
		return 0;
	bool l1 = PAD_justPressed(BTN_L1);
	if (PAD_isPressed(l1 ? BTN_R1 : BTN_L1) || PWR_ignoreSettingInput(l1 ? BTN_L1 : BTN_R1, show_setting))
		return 0;
	return l1 ? -1 : 1;
}

// X (BTN_RESUME) and Y pressed while the tab row had focus (BTN_* bits): their release belongs to the row, not to the content's
// resume (X) or netplay launch (Y) once focus has gone back.
static int tab_row_held = 0;

// The tab row has focus (spec 2026-10-01-menu-sp6-tab-focus-design.md). It owns the D-pad, A, B, X, Y and L1/R1 until
// the content takes focus back: LEFT/RIGHT and L1/R1 switch tabs (fresh presses; focus stays), DOWN, A and B return to
// the content on the tab's item, UP wraps to its bottom, the rest does nothing. SELECT, START, MENU and the F keys
// leave the menu: they hand focus back and keep their meaning (false: the root's handlers run).
static bool tabRowInput(unsigned long now, IndicatorType show_setting, GameListResult* result, bool* dirty) {
	if (PAD_tappedSelect(now) || PAD_tappedStart(now) || PAD_tappedMenu(now) ||
		(HAS_FN_KEYS && (PAD_justPressed(BTN_FN1) || PAD_justPressed(BTN_FN2)))) {
		MenuTabs_setFocused(false);
		*dirty = true;
		return false;
	}
	int delta = PAD_justPressed(BTN_LEFT) ? -1 : PAD_justPressed(BTN_RIGHT) ? 1
																			: 0;
	if (!delta)
		delta = shoulderTabDelta(show_setting);
	if (delta) {
		switchTab(delta, result, dirty);
		return true;
	}
	if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) {
		MenuTabs_setFocused(false);
		*dirty = true;
		return true;
	}
	if (PAD_justPressed(BTN_UP)) {
		MenuTabs_setFocused(false);
		contentToBottom();
		*dirty = true;
		return true;
	}
	static const int owned[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L1, BTN_R1};
	for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); i++)
		if (PAD_justPressed(owned[i]) || PAD_justRepeated(owned[i]) || PAD_justReleased(owned[i]))
			return true; // a held key, X, Y, a release: nothing, and the content doesn't see it
	return false;
}

GameListResult GameList_handleInput(unsigned long now, int currentScreen,
									IndicatorType show_setting, bool* dirty) {
	GameListResult result = {
		.screen = currentScreen,
		.animdir = ANIM_NONE,
		.folderbgchanged = false,
	};

	// latch X/Y pressed on the tab row; a latched release is swallowed below (a press with the content focused re-arms)
	bool row_focused = stack->count == 1 && MenuTabs_focused();
	int row_released = 0; // resume/netplay keys released now whose press the tab row owned
	static const int row_keys[] = {BTN_RESUME, BTN_Y};
	for (size_t i = 0; i < sizeof(row_keys) / sizeof(row_keys[0]); i++) {
		int k = row_keys[i];
		if (PAD_justPressed(k))
			tab_row_held = row_focused ? (tab_row_held | k) : (tab_row_held & ~k);
		if (PAD_justReleased(k) && (tab_row_held & k))
			row_released |= k;
	}
	tab_row_held &= ~row_released;

	if (row_focused && tabRowInput(now, show_setting, &result, dirty))
		return result;

	// Home owns the D-pad, A and MENU; SELECT, START, the F keys and L1/R1 fall through to the root's handlers
	// below, and the list's own row actions (X, Y, A, the row moves) stay off.
	bool home = Home_active();
	if (home) {
		unsigned gen = MenuTabs_generation();
		if (Home_handleInput(now, dirty)) {
			if (MenuTabs_generation() != gen)
				result.folderbgchanged = true; // an edge move or "Pick a game" opened another tab
			else if (stack->count > 1 && !startgame)
				result.animdir = SLIDE_LEFT; // a pinned plain folder opened (as the list's A does)
			return result;
		}
	}

	// Grid owns the D-pad only; every other button falls through to the list's own handlers below
	if (!home && GridView_active()) {
		bool switched = false;
		int was = top->selected;
		if (GridView_handleInput(now, dirty, &switched)) {
			if (switched)
				result.folderbgchanged = true; // switchTab already reset the resume state for the new tab
			else if (top->selected != was && top->entries->count > 0)
				readyResume(top->entries->items[top->selected]);
			return result;
		}
	}

	// Carousel and Backdrop own the D-pad the same way (UP/DOWN swallowed: the row has no rows)
	if (!home && RowView_active()) {
		unsigned gen = MenuTabs_generation();
		int was = top->selected;
		if (RowView_handleInput(now, dirty)) {
			if (MenuTabs_generation() != gen)
				result.folderbgchanged = true; // switchTab already reset the resume state for the new tab
			else if (top->selected != was && top->entries->count > 0)
				readyResume(top->entries->items[top->selected]);
			return result;
		}
	}

	int selected = top->selected;
	int total = home ? 0 : top->entries->count;
	int row_count = GameList_rowCount();

	if (!home && PAD_tappedMenu(now) && !ContextMenu_isOpen()) {
		// Open contextual menu based on current page
		Entry* entry = (total > 0) ? top->entries->items[selected] : NULL;
		int idx = 0;
		ContextMenuItem items[CONTEXTMENU_MAX_ITEMS];

		if (Shortcuts_isInToolsFolder(top->path)) {
			// Tools tab or Tools listing. Simple mode's Tools tab (Settings
			// only) gets no menu: kids can't unpin the curated shortcuts.
			if (entry && !gl_simple_mode)
				toolItems(entry, items, &idx);
		} else if (stack->count == 1) {
			// Root menu (Consoles or Collections tab; Home handles its own MENU)
			// A collection renames or deletes. Pinned rows unpin in place; hidden
			// in simple mode so kids can't remove the curated shortcuts.
			if (isCollectionRow(entry)) {
				addItem(items, &idx, "Rename", 40);
				addItem(items, &idx, "Delete", 41);
			}
			if (!gl_simple_mode && entry &&
				Shortcuts_exists(entry->path + strlen(SDCARD_PATH)))
				addItem(items, &idx, "Unpin", 3);
			rootItems(items, &idx);
		} else if (entry) {
			// ROM listing (console directory or subfolder)
			romItems(entry, true, items, &idx);
		}

		if (idx > 0) {
			GameList_contextMenuClosed(); // a list menu never acts on a stale Home target
			ContextMenu_open(items, idx);
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
	} else if (HAS_FN_KEYS && !gl_simple_mode &&
			   (PAD_justPressed(BTN_FN1) || PAD_justPressed(BTN_FN2))) {
		// F1/F2 tool shortcuts (Brick family): launch the assigned tool pak
		// from anywhere in normal browsing. Unassigned key or a tool that got
		// uninstalled since it was assigned = no-op.
		const char* rel = PAD_justPressed(BTN_FN1) ? CFG_getFn1Tool() : CFG_getFn2Tool();
		if (rel[0]) {
			char pak_path[MAX_PATH];
			char launch_path[MAX_PATH];
			snprintf(pak_path, sizeof(pak_path), "%s%s", SDCARD_PATH, rel);
			snprintf(launch_path, sizeof(launch_path), "%s/launch.sh", pak_path);
			if (exists(launch_path)) {
				startgame = true;
				openPakInPlace(pak_path);
			}
		}
		return result;
	} else if (stack->count == 1 && (PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT))) {
		// Root, List style: LEFT/RIGHT switch tabs like L1/R1 (fresh press only, so holding doesn't cycle)
		switchTab(PAD_justPressed(BTN_LEFT) ? -1 : 1, &result, dirty);
		return result;
	} else if (stack->count == 1 && PAD_justRepeated(BTN_UP) && (total == 0 || selected == 0)) {
		// Root, List style: UP from the first row focuses the tab row (a fresh press; a held one stops, no wrap)
		if (PAD_justPressed(BTN_UP)) {
			MenuTabs_setFocused(true);
			*dirty = true;
		}
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
		// page jump in game lists only (the root's LEFT/RIGHT switch tabs above; a held one does nothing)
		if (stack->count > 1 && PAD_justRepeated(BTN_LEFT)) {
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
		} else if (stack->count > 1 && PAD_justRepeated(BTN_RIGHT)) {
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

	// L1/R1 at the root switch tabs (no repeat); in lists they jump by letter
	int shoulder = stack->count == 1 ? shoulderTabDelta(show_setting) : 0;
	if (shoulder) {
		switchTab(shoulder, &result, dirty);
		return result;
	}

	if (stack->count > 1 && total > 0 && PAD_justRepeated(BTN_L1) &&
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
	} else if (stack->count > 1 && total > 0 && PAD_justRepeated(BTN_R1) &&
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

	if (total > 0 && resume.can_resume && PAD_justReleased(BTN_RESUME) && !(row_released & BTN_RESUME) &&
		!PAD_isPressed(BTN_L2) && !PAD_isPressed(BTN_R2)) {
		resume.should_resume = true;
		Entry_open(entry);
		*dirty = true;
	}
	// Y launches netplay-capable ROMs with netplay (works at root for
	// pinned games too — root Search moved to START for this)
	else if (total > 0 && PAD_justReleased(BTN_Y) && !(row_released & BTN_Y) && entryNetplayCapable(entry)) {
		putFile(NETPLAY_LAUNCH_PATH, "1\n");
		Entry_open(entry);
		// disarm if no launch was queued, whatever the entry type (see case 35)
		if (!quit)
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

// True when the selected entry is a game (a ROM or a folder game) that gets info lines. The folder-game
// check stats the disk, so it is remembered for the selected path of the current list (a reload or another
// list starts over) instead of rerun every frame.
static bool selectedIsGame(Entry* entry) {
	static char last_path[MAX_PATH];
	static const void* last_list;
	static unsigned last_gen;
	static bool last_is_game;
	if (!entry)
		return false;
	if (entry->type == ENTRY_ROM)
		return true;
	// the generation catches a tab switch/reload whose new Directory landed at the old address
	if ((const void*)top != last_list || MenuTabs_generation() != last_gen ||
		strcmp(last_path, entry->path) != 0) {
		char game_file[MAX_PATH];
		last_list = top;
		last_gen = MenuTabs_generation();
		snprintf(last_path, sizeof(last_path), "%s", entry->path);
		last_is_game = entryFolderGame(entry, game_file);
	}
	return last_is_game;
}

// Top of the first row: right under the top strip at every level (no arrow gutter; the arrows live in the
// info band).
static int listTop(void) {
	return SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2);
}

static InfoBandLayout listLayout(void) {
	int screen_h = screen ? screen->h : FIXED_HEIGHT;
	int text_h = font.small ? TTF_FontHeight(font.small) : SCALE1(FONT_SMALL);
	return InfoBand_layout(screen_h, SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2), listTop(), SCALE1(PILL_SIZE), text_h,
						   NX_DP(2), NX_DP(12));
}

int GameList_textX(void) {
	return stack && stack->count == 1 ? NX_DP(NX_MENU_GUTTER_DP) : UI_listTextX();
}

int GameList_rowCount(void) {
	return listLayout().rows;
}

int GameList_currentStyle(void) {
	int style;
	if (stack->count == 1) {
		int cat = MenuTabs_styleCategory(MenuTabs_current());
		if (cat < 0)
			return MENU_STYLE_LIST; // Home draws itself
		style = CFG_getMenuStyle(cat);
	} else {
		style = CFG_getGameListStyle();
	}
	switch (style) {
	case MENU_STYLE_GRID:
	case MENU_STYLE_CAROUSEL:
	case MENU_STYLE_BACKDROP:
		return style;
	default:
		return MENU_STYLE_LIST;
	}
}

void GameList_renderInfoLayer(void) {
	// Home, Grid, Carousel and Backdrop have no info band (Grid's game info sits in the lit tile, the rows'
	// in the caption under the row)
	if (ContextMenu_isOpen() || Home_active() || GridView_active() || RowView_active())
		return;
	int total = top->entries->count;
	bool up = total > 0 && top->start > 0;
	bool down = total > 0 && top->end < total;
	InfoBandLayout layout = listLayout(); // the rows' own geometry
	layout.arrow_x = GameList_textX();	  // the arrows sit at the rows' text start (§3.2)
	// On LAYER_OVERLAY, above the thumbnail layer (which would otherwise cover it on 4:3 screens).
	// nextui.c clears that layer at the start of every dirty pass, so this is redrawn with the list.
	// The band is always there (fade + arrows), with or without text, so rows never move.
	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;
	MenuTabId tab = MenuTabs_current();
	bool at_root = stack->count == 1;
	// game rows: game lists and the Home tab (same rule as GameList_render's game_art)
	if (entry && (!at_root || tab == MENU_TAB_HOME) && selectedIsGame(entry)) {
		GameInfo info;
		InfoSeg segs[3];
		int n = 0;
		if (GameInfo_get(entry->path, &info) && (info.has_time || info.has_ra))
			n = GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0,
								  info.has_time ? info.seconds : -1, info.has_ra ? info.unlocked : 0,
								  info.has_ra ? info.total : 0, info.has_ra ? info.next : NULL, true, segs);
		InfoBand_render(&layout, segs, n, up, down, LAYER_OVERLAY);
		return;
	}
	// Consoles and Collections rows: "N games" (nothing while unknown); Tools and folders show no text.
	char games[32] = "";
	if (entry && at_root && tab == MENU_TAB_CONSOLES)
		GameInfo_gamesLabel(Content_consoleGameCount(entry), games, sizeof(games));
	else if (entry && at_root && tab == MENU_TAB_COLLECTIONS)
		GameInfo_gamesLabel(CollCount_get(entry->path), games, sizeof(games));
	InfoBand_renderText(&layout, games[0] ? games : NULL, up, down, LAYER_OVERLAY);
}

// The hint bar of a List or Grid screen (main-menu tabs and game lists): one copy of the pairs, the four-pair cap
// and the START drop rule.
static void renderHints(SDL_Surface* screen, IndicatorType show_setting) {
	int total = top->entries->count;
	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;
	char* right_pairs[16] = {NULL};
	int p = 0;

	// the tab row has focus: exactly SELECT RECENT and A OPEN (A returns to the content)
	if (stack->count == 1 && MenuTabs_focused()) {
		UI_renderButtonHintBar(screen, (char*[]){"SELECT", "RECENT", "A", "OPEN", NULL});
		return;
	}

	// main menu tabs: the Game Switcher first
	if (stack->count == 1) {
		right_pairs[p++] = "SELECT";
		right_pairs[p++] = "RECENT";
	}

	// search hint at root (hint only — START still opens search when the
	// "Show search hint" Appearance setting hides it)
	if (CFG_getShowSearchHint() && !(show_setting && !GetHDMI()) && !GetHDMI() &&
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
		if (isCollectionRow(entry)) { // Rename and Delete
			right_pairs[p++] = "MENU";
			right_pairs[p++] = "OPTIONS";
		}
		right_pairs[p++] = "A";
		right_pairs[p++] = "OPEN";
	}

	// The hint bar holds four: with SELECT added, START SEARCH goes first.
	if (p / 2 > 4) {
		for (int i = 0; i < p; i += 2) {
			if (strcmp(right_pairs[i], "START") == 0) {
				for (int k = i; k + 2 < p; k++)
					right_pairs[k] = right_pairs[k + 2];
				p -= 2;
				right_pairs[p] = NULL;
				right_pairs[p + 1] = NULL;
				break;
			}
		}
	}

	if (right_pairs[0])
		UI_renderButtonHintBar(screen, right_pairs);
}

// A Grid, Carousel or Backdrop screen draws on plain black (or the Backdrop picture, on `screen`): no bg.png,
// folder background or game art. Drops whatever is loaded, and forgets the resolve so a List screen reloads its
// background when it comes back.
static void clearArtForGrid(SDL_Surface* screen, int lastScreen) {
	folderBgPath[0] = '\0';
	bgLastType = -1;
	bgResolveValid = false;
	onBackgroundLoaded(NULL); // a no-op while nothing is loaded
	had_thumb = false;
	ox = screen->w;
	if (!list_art_cleared || lastScreen != SCREEN_GAMELIST) {
		startLoadThumb("");
		list_art_cleared = true;
	}
	// no marquee on a Grid screen: a List's leftover state would keep ticking on the scroll-text layer
	if (GameList_scrollBusy())
		ScrollText_clear(&list_scroll);
}

void GameList_render(SDL_Surface* screen, int lastScreen,
					 IndicatorType show_setting, SDL_Surface* blackBG) {
	if (Home_active()) {
		// the global bg.png, as on every main-menu tab, and no thumbnail (Home draws its own pictures)
		bool names = true;
		resolveAndLoadBackground(NULL, NULL, &names);
		had_thumb = false;
		ox = screen->w;
		if (!list_art_cleared || lastScreen != SCREEN_GAMELIST) {
			startLoadThumb("");
			list_art_cleared = true;
		}
		Home_render(screen, lastScreen);
		if (lastScreen == SCREEN_OFF)
			GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255, 0,
									  CFG_getMenuTransitions() ? 200 : 20, LAYER_THUMBNAIL);
		return;
	}

	bool grid = GridView_active();
	if (grid || RowView_active()) {
		clearArtForGrid(screen, lastScreen);
		if (grid)
			GridView_render(screen, lastScreen);
		else
			RowView_render(screen, lastScreen);
		renderHints(screen, show_setting); // over the black body (or the picture), as on List
		if (lastScreen == SCREEN_OFF)
			GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255, 0,
									  CFG_getMenuTransitions() ? 200 : 20, LAYER_THUMBNAIL);
		return;
	}

	int total = top->entries->count;

	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;
	char path_copy[1024];
	char* rompath = NULL;

	if (entry) {
		strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
		path_copy[sizeof(path_copy) - 1] = '\0';

		rompath = dirname(path_copy);
	}

	// Always show names: the tabbed root is a plain list, so hiding them (the old "Show folder names
	// at root" = Off behind a global bg.png) would blank the whole main menu.
	bool list_show_entry_names = true;

	bool at_root = stack->count == 1;
	MenuTabId tab = MenuTabs_current();
	// Home holds games; Consoles, Collections and Tools draw no game art (spec: no console
	// background on the main menu, plain lists for Collections and Tools)
	bool game_art = !at_root || tab == MENU_TAB_HOME;

	// load folder background. Every main-menu tab shows the global bg.png (no
	// entry): pinned shortcuts and tools would otherwise clear it or show a
	// tool's own background. Game lists keep the per-entry background.
	resolveAndLoadBackground(at_root ? NULL : entry, rompath, &list_show_entry_names);

	// load game thumbnails
	if (!game_art) {
		// Clear the art the way a game without any does: an empty path loads
		// nothing, so the loader drops thumbbmp and the thumbnail layer (or the
		// background style's art) empties once the miss comes back. Once per
		// switch to a tab without art (or return from another screen, which may
		// have loaded art), not every frame: each clear re-uploads the layer.
		had_thumb = false;
		ox = screen->w;
		if (!list_art_cleared || lastScreen != SCREEN_GAMELIST) {
			startLoadThumb("");
			list_art_cleared = true;
		}
	} else if (total > 0) {
		list_art_cleared = false;
		if (CFG_getShowGameArt()) {
			char thumbpath[1024];
			// The background style shows the screenshot or nothing: a mix
			// composite behind the list is the look it exists to replace.
			ROM_displayArtPath(entry->path, CFG_getEffectiveArtType(),
							   CFG_getGameArtStyle() != ART_STYLE_BACKGROUND,
							   thumbpath, sizeof(thumbpath));
			had_thumb = startLoadThumb(thumbpath);
			// "Game art width" reserves a column for the thumbnail style only.
			// The background style paints the art behind the list, so the title
			// runs the full screen width (matching an art-less row); a long
			// title may reach over the image's bright side.
			int max_w = (int)(screen->w - (screen->w * CFG_getGameArtWidth()));
			if (!had_thumb)
				ox = screen->w;
			else if (CFG_getGameArtStyle() == ART_STYLE_BACKGROUND)
				// The consumers add SCALE1(BUTTON_MARGIN) back to ox, so this
				// yields the same available width as the art-less full-width row.
				ox = screen->w - SCALE1(BUTTON_MARGIN * 2);
			else
				ox = (int)(max_w)-SCALE1(BUTTON_MARGIN * 5);
		}
	}

	renderHints(screen, show_setting);

	// The info band (a GPU layer), drawn even for an empty list. Drawn before the rows: the marquee below
	// may present mid-render, and the band must already be back on its layer by then (no blink).
	GameList_renderInfoLayer();

	if (total > 0) {
		int selected_row = top->selected - top->start;
		// Row text start (LIST-LAYOUT §1): the main menu's List rows on the tab row's 24 dp gutter, game lists on
		// the 14 dp list inset; the pill keeps its 14 dp (SCALE1(BUTTON_PADDING)) round the text either way.
		int row_text_x = GameList_textX();
		int row_pill_x = UI_listPillXFor(row_text_x);

		// Consoles tab: the selected console's logo, dimmed, on the right behind the rows
		if (at_root && tab == MENU_TAB_CONSOLES) {
			const char* slash = strrchr(entry->path, '/');
			const char* logo_id = MenuLogo_idForFolder(slash ? slash + 1 : entry->path);
			if (logo_id) {
				char file[64];
				snprintf(file, sizeof(file), "menu_logo_%s.png", logo_id);
				int band_y = listTop();
				int band_h = GameList_rowCount() * SCALE1(PILL_SIZE);
				SDL_Surface* logo = MenuArt_get(file, screen->w * 40 / 100, band_h * 60 / 100);
				if (logo) {
					SDL_SetSurfaceAlphaMod(logo, 41); // white at 16%
					SDL_BlitSurface(logo, NULL, screen,
									&(SDL_Rect){screen->w - SCALE1(23) - logo->w, band_y + (band_h - logo->h) / 2});
					SDL_SetSurfaceAlphaMod(logo, 255);
				}
			}
		}

		// Glide the selection pill to the selected row. Drawn here, decoupled from
		// the per-row loop, so it can sit between rows mid-slide; the rows below
		// then draw text only (no per-row pill background).
		bool pill_animating = false;
		int pill_y = -1; // current pill top-y; used by the row loop to tint the
						 // row the pill is over (see color-tracking note below)
		if (list_show_entry_names) {
			Entry* sel = top->entries->items[top->selected];
			char* sel_name = sel->name;
			char* sel_unique = sel->unique;
			trimSortingMeta(&sel_name);
			if (sel_unique)
				trimSortingMeta(&sel_unique);
			char* sel_text = sel_unique ? sel_unique : sel_name;
			int sel_avail = MAX(0, (had_thumb ? ox + SCALE1(BUTTON_MARGIN)
											  : screen->w - SCALE1(BUTTON_MARGIN)) -
									   SCALE1(PADDING) - row_pill_x);
			char sel_trunc[256];
			int sel_pill_w = UI_calcListPillWidth(font.large, sel_text, sel_trunc, sel_avail, 0);
			int target_y = listTop() + SCALE1(selected_row * PILL_SIZE);
			// A page/list change (folder enter/exit — the `top` directory pointer
			// changes) snaps the pill to the new selection instead of gliding in
			// from the previous list's row, which briefly flashed the old position.
			// The tab generation also counts: a tab switch frees stack[0] and malloc often puts the
			// new Directory at the same address.
			bool list_changed = (const void*)top != list_pill_prev_top ||
								MenuTabs_generation() != list_pill_prev_gen;
			list_pill_prev_top = (const void*)top;
			list_pill_prev_gen = MenuTabs_generation();
			// On a wrap (last<->first), enter from the near edge in the direction of
			// travel instead of sliding the whole list: forward wrap (last->first)
			// drops in from just above the first row; backward wrap (first->last)
			// rises up from just below the last row. Done by seeding the glide's
			// start position (current_y) one row off the target edge.
			int cur_sel = top->selected;
			bool wrap_fwd = !list_changed && list_pill_prev_sel == total - 1 && cur_sel == 0 && total > 1;
			bool wrap_bwd = !list_changed && list_pill_prev_sel == 0 && cur_sel == total - 1 && total > 1;
			list_pill_prev_sel = cur_sel;
			if (target_y != list_pill_target || list_changed) {
				bool animate = list_pill_target >= 0 && !ContextMenu_isOpen() && !list_changed;
				if (wrap_fwd)
					list_pill_anim.current_y = target_y - SCALE1(PILL_SIZE);
				else if (wrap_bwd)
					list_pill_anim.current_y = target_y + SCALE1(PILL_SIZE);
				UI_pillAnimSetTarget(&list_pill_anim, target_y, sel_pill_w, animate);
				list_pill_target = target_y;
			}
			pill_y = UI_pillAnimTick(&list_pill_anim);
			pill_animating = UI_pillAnimIsActive(&list_pill_anim);
			if (!pill_animating) {
				// Width changes outside a glide (e.g. a thumbnail arriving
				// re-truncates the selected title) snap directly — only
				// selection moves are animated.
				list_pill_anim.current_w = sel_pill_w;
				list_pill_anim.target_w = sel_pill_w;
			}
			// Clip the pill to the list band: a wrap glide seeds the pill one
			// row beyond the list edge, and unclipped it would slide over the
			// menu bar (top) or button hint bar (bottom). Clipped, the incoming
			// pill is revealed only through the first/last row.
			int band_rows = top->end - top->start;
			SDL_Rect band = {0, listTop(), screen->w, SCALE1(band_rows * PILL_SIZE)};
			SDL_Rect prev_clip;
			SDL_GetClipRect(screen, &prev_clip);
			SDL_SetClipRect(screen, &band);
			SDL_Rect pill = {row_pill_x, pill_y, list_pill_anim.current_w, SCALE1(PILL_SIZE)};
			if (at_root && MenuTabs_focused()) {
				// the tab row has focus: the pill at 40% (its text keeps the on-pill colour, as the mockup's
				// whole-row 40% gives over the black ground)
				Uint8 r, g, b;
				SDL_GetRGB(THEME_COLOR1, screen->format, &r, &g, &b);
				GFX_blitPillColor(ASSET_WHITE_PILL, screen, &pill,
								  SDL_MapRGBA(screen->format, r, g, b, (Uint8)(255 * MenuTabs_contentLit() + 0.5f)),
								  RGB_WHITE);
			} else {
				UI_drawListItemBg(screen, &pill, true);
			}
			SDL_SetClipRect(screen, &prev_clip);
		}

		for (int i = top->start, j = 0; i < top->end; i++, j++) {
			Entry* entry = top->entries->items[i];
			char* entry_name = entry->name;
			char* entry_unique = entry->unique;
			bool row_is_selected = (j == selected_row);

			// Calculate per-item available width (thumbnail-aware)
			int available_width =
				MAX(0, (had_thumb ? ox + SCALE1(BUTTON_MARGIN)
								  : screen->w - SCALE1(BUTTON_MARGIN)) -
						   SCALE1(PADDING) - row_pill_x);

			// Prepare display text: prefer unique name, fall back to entry name
			trimSortingMeta(&entry_name);
			if (entry_unique)
				trimSortingMeta(&entry_unique);
			char* display_text = entry_unique ? entry_unique : entry_name;

			int y = listTop() + SCALE1(j * PILL_SIZE);

			if (list_show_entry_names) {
				char truncated[256];
				ListLayout item_layout = {
					.item_h = SCALE1(PILL_SIZE),
					.max_width = available_width,
				};
				// selected=false: the selection background is the moving pill drawn
				// above, not a per-row static pill.
				ListItemPos pos = UI_renderListItemPill(
					screen, &item_layout, font.large,
					display_text, truncated, y, false, 0);
				pos.text_x = row_text_x; // the shared renderer's x is the 14 dp inset; the main menu sits on 24 dp
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
				// Tint the row the pill is physically over, so the selected colour
				// (picked to sit on the pill) swaps in lockstep with the glide: the
				// old row stays lit until the pill leaves it and the new row lights
				// the moment the pill arrives, rather than the colour flipping only
				// when the animation ends. "Over" = the pill covers a majority of
				// the row, which also guarantees the colour never shows on bare
				// background. The marquee still waits until the pill has fully
				// settled on the actual selection (and the context menu is closed).
				bool pill_over = pill_y >= 0 &&
								 abs(pill_y - y) * 2 < SCALE1(PILL_SIZE);
				bool use_marquee = row_is_selected && !pill_animating &&
								   !ContextMenu_isOpen();
				if (entry_unique && !use_marquee &&
					strncmp(entry_unique, entry_name, strlen(entry_name)) == 0) {
					// Duplicate-name row: name in the list colour, disambiguating
					// suffix ("(EMU)" or the filename remainder) dimmed, as in
					// upstream NextUI. The marquee row stays single-colour: the
					// scroll texture is one surface.
					UI_renderListItemTextDimSuffix(screen, entry_name,
												   entry_unique + strlen(entry_name),
												   font.large, pos.text_x, pos.text_y,
												   text_width, pill_over);
				} else {
					UI_renderListItemText(screen,
										  use_marquee ? &list_scroll : NULL,
										  display_text, font.large,
										  pos.text_x, pos.text_y, text_width, pill_over);
				}
			}
		}
		if (lastScreen == SCREEN_OFF) {
			GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255,
									  0, CFG_getMenuTransitions() ? 200 : 20,
									  LAYER_THUMBNAIL);
		}

	} else {
		UI_renderCenteredMessage(screen, "Empty folder");
	}
}
