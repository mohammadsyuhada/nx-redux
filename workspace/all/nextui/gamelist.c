// Game list screen: the ROM/console browser that nextui.c boots into.
// Input handling, background/thumbnail resolution and rendering for this
// screen live here; nextui.c only dispatches to it. (split from nextui.c)

#include "gamelist.h"
#include "homeart.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "display_helper.h"
#include "shortcuts.h"
#include "ui_buttonhintbar.h"
#include "ui_draw.h"
#include "ui_fade.h"
#include "ui_font.h"
#include "ui_confirmdialog.h"
#include "ui_loadingoverlay.h"
#include "ui_message.h"
#include "ui_contextmenu.h"
#include "ui_keyboard.h"
#include "ui_accent.h"
#include "ui_list.h"
#include "ui_listdialog.h"
#include "ui_pindialog.h"
#include "ui_text_sizes.h"
#include "utils.h"
#include "wifi.h"

#include "collcount.h"
#include "contentdim.h"
#include "collname.h"
#include "romkey.h"
#include "content.h"
#include "gameswitcher.h"
#include "imgloader.h"
#include "artbg.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gridview.h"
#include "rowview.h"
#include "stackview.h"
#include "home.h"
#include "infoband.h"
#include "launcher.h"
#include "list_window.h"
#include "menuart.h"
#include "controller_art.h"
#include "controller_art_model.h"
#include "menulogo.h"
#include "menutabs.h"
#include "tabbg_model.h"
#include "recents.h"
#include "search.h"
#include "types.h"

#include <dirent.h>
#include <msettings.h>
#include <libgen.h>
#include <math.h>
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
static unsigned list_pill_prev_top = 0; // the Directory serial (0 = none)
static unsigned list_pill_prev_gen = 0;

static bool had_thumb = false;
static int ox;
// Set once a main-menu tab without game art has cleared the thumbnail
static bool list_art_cleared = false;
// the List's Consoles controller art (or logo) was still on the art loader's thread in the last render
static bool list_art_waiting = false;
// the last render was a List whose rows all went to the GPU: nothing drawn into the screen between the header's fade
// and the hint bar but, at most, the selected row's marquee in list_body_row (h 0: none) (GameList_listBodyClear)
static bool list_body_clear = false;
static SDL_Rect list_body_row;
// the selected row's held marquee title as a sprite over the sprite pill (UI_listItemMarqueeHoldSprite), from the last
// List render: dropped when its GPU scroll starts between full frames, or the static and the scrolling title would both
// show (NULL: none)
static SDL_Texture* list_hold_tex = NULL;
#define LIST_ART_AHEAD 2	 // neighbours each way whose controller art is decoded ahead
#define LIST_ART_UPLOAD_MS 6 // the slot a pad's texture upload needs, at least
#define LIST_INFO_AHEAD 2	 // neighbours each way whose play time and achievements are looked up ahead
#define LIST_PIC_AHEAD 1	 // neighbours each way whose List picture is decoded ahead (the thumbnail cache keeps 8)
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

// The List's art for its selected game (ROM_findListArt's 2-6 stats), kept while the row, the list and the List art
// setting stay; bumped where a game's art changes on the card (a fetch, a delete, a rename, Refresh Roms).
static unsigned list_art_epoch = 0;

static void listArtForget(void) {
	list_art_epoch++;
}

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

bool GameList_artWaiting(void) {
	return list_art_waiting && !GridView_active() && !RowView_active() && (!Home_active() || Home_isList());
}

bool GameList_scrollIsScrolling(void) {
	return ScrollText_isScrolling(&list_scroll);
}

// The List on the Consoles tab: its neighbours' controller art decoded ahead (nearest first) and given a texture while
// the frame has room, so a held D-pad lands on art that is ready. True while some is still decoding.
static bool prefetchListArt(Uint32 deadline) {
	int look = GameList_lookTab();
	if (look != MENU_TAB_CONSOLES || !CFG_getMenuControllerArt() || !top || top->entries->count == 0 ||
		GameSwitcher_isList()) // the Game Switcher's List over the tab: not the tab's art
		return false;
	int n = top->entries->count, sel = top->selected;
	bool busy = false;
	for (int d = 1; d <= LIST_ART_AHEAD; d++) {
		for (int sign = 1; sign >= -1; sign -= 2) {
			int i = ((sel + sign * d) % n + n) % n; // the List wraps
			Entry* e = top->entries->items[i];
			const char* slash = strrchr(e->path, '/');
			const char* pad_id = Pad_idForFolder(slash ? slash + 1 : e->path);
			if (!pad_id)
				continue;
			bool pending = false;
			SDL_Surface* pad = ControllerArt_listPrefetch(pad_id, screen->w, screen->h, d, &pending);
			busy = busy || pending;
			// a texture is a whole upload (a few ms): only with that much of the slot left
			if (pad && !pad->userdata && SDL_GetTicks() + LIST_ART_UPLOAD_MS <= deadline)
				PLAT_textureForSurface(pad);
		}
	}
	return busy;
}

static Directory* shownList(void);
static bool atRoot(void);

// A List that shows a game's info line (Layouts > Button hints: Hide, with Extra info shown): its neighbours' play time
// and achievements looked up ahead, nearest first, one at a time (GameInfo keeps one prefetch slot). Without it a step
// onto a game seen for the first time drew the band without the line, and the line's arrival rebuilt the band mid-glide:
// a frame over its vsync. True while one is still on its way.
static bool prefetchListInfo(void) {
	if (CFG_getButtonHints() || !CFG_getExtraInfo() || GameList_pillAnimating()) // not under a glide, as the pictures'
		return false;
	Directory* list = shownList();
	if (!list || list->entries->count < 2)
		return false;
	int n = list->entries->count, sel = list->selected;
	GameInfo info;
	for (int d = 1; d <= LIST_INFO_AHEAD; d++) {
		for (int sign = 1; sign >= -1; sign -= 2) {
			Entry* e = list->entries->items[((sel + sign * d) % n + n) % n]; // the List wraps
			if (e->type == ENTRY_ROM && !GameInfo_prefetch(e->path, &info))
				return true; // asked (or on its way): the next idle slot asks for the one after
		}
	}
	return false;
}

// A game list's List with game art, or the Game Switcher's List: the neighbours' pictures decoded into the thumbnail
// cache ahead (thumbPrefetch), nearest first, one at a time, so a step lands on a decoded picture. A decode on the
// loader's thread while the pill glides slowed the glide's GPU flips (a frame over its vsync on most steps onto a new
// game, Brick Pro): this runs only while nothing glides, and gives way to the selection's own picture. Once a
// selection's neighbours are in, it rests until the selection moves (the paths cost file checks).
static bool prefetchListPictures(void) {
	static unsigned done_serial, done_gen;
	static int done_sel = -1, done_art = -1;
	if (GameList_pillAnimating())
		return false;
	bool sw = GameSwitcher_isList();
	Directory* list = shownList();
	if (!list || list->entries->count < 2 || (!sw && (Home_isList() || atRoot() || GameList_currentStyle() != MENU_STYLE_LIST)))
		return false;
	int art = CFG_getGameListArt();
	if (art == GAME_LIST_ART_NONE)
		return false;
	if (done_sel == list->selected && done_serial == list->serial && done_art == art &&
		done_gen == MenuTabs_generation())
		return false;
	int n = list->entries->count, sel = list->selected;
	for (int d = 1; d <= LIST_PIC_AHEAD; d++) {
		for (int sign = 1; sign >= -1; sign -= 2) {
			int i = ((sel + sign * d) % n + n) % n; // the List wraps
			Entry* e = list->entries->items[i];
			if (e->type != ENTRY_ROM)
				continue;
			char path[MAX_PATH];
			bool fit = false;
			if (sw) {
				if (!GameSwitcher_listArtPathAt(i, path, sizeof(path)))
					continue;
			} else {
				fit = ROM_findListArt(e->path, art, path, sizeof(path));
			}
			if (path[0] && thumbPrefetch(path, fit ? THUMB_COMPOSE_FIT : THUMB_COMPOSE_BG))
				return true; // queued, or the loader busy: the next idle slot looks again
		}
	}
	done_sel = sel;
	done_serial = list->serial;
	done_art = art;
	done_gen = MenuTabs_generation();
	return false;
}

bool GameList_prefetchIdle(Uint32 deadline) {
	// Home builds nothing ahead; the Grid, Carousel and Backdrop (either orientation) compose the items their next
	// move draws first; the List looks up its neighbours' info and decodes their pictures (the Consoles tab: their
	// controller art)
	if (Home_active())
		return false;
	if (GridView_active())
		return GridView_prefetchStep(deadline);
	if (RowView_active())
		return RowView_prefetchStep(deadline);
	bool info = prefetchListInfo();
	bool pics = prefetchListPictures();
	return prefetchListArt(deadline) || info || pics;
}

void GameList_scrollTickIdle(void) {
	ScrollText_activateAfterDelay(&list_scroll);
	if (ScrollText_isScrolling(&list_scroll)) {
		// the scroll layer draws the title from here: the held one's sprite leaves before this first present
		if (list_hold_tex) {
			PLAT_spriteRemove(list_hold_tex);
			list_hold_tex = NULL;
		}
		ScrollText_animateOnly(&list_scroll);
	}
}

void GameList_clearScroll(void) {
	ScrollText_clear(&list_scroll);
}

// tab_bg: the user's background for the selected main-menu row (tabBackground), shown instead of the global
// bg.png; NULL for none.
static void resolveAndLoadBackground(Entry* entry, const char* rompath, const char* tab_bg,
									 bool* list_show_entry_names) {
	// Persists across calls to avoid redundant background reloads
	// (file-scope bgLastType so GameList_invalidateBackground can reset it)
	int* lastType = &bgLastType;

	// Same entry as the last call (every frame of a glide/marquee redraw):
	// skip the resolve — and with it the per-frame SD-card stats below.
	// No entry (an empty list, or a main-menu tab without game art) is keyed
	// as "", which no entry path can be.
	const char* key = entry ? entry->path : "";
	if (tab_bg)
		key = tab_bg;
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

	if (tab_bg) {
		cmpPath = tab_bg;
		strncpy(bgPath, tab_bg, sizeof(bgPath) - 1);
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

static bool tabBgExists(const char* path) {
	return exists((char*)path);
}

// The user's background for the selected row of the Consoles or Collections List tab (tabbg_model.h), or NULL.
// The answer is kept per row, tab and controller setting: the List redraws every frame of a glide or marquee and
// this would otherwise stat the SD card each time.
static const char* tabBackground(MenuTabId tab, const Entry* entry) {
	static char last_entry[1024], last_bg[1024];
	static int last_tab = -1, last_art = -1;
	static bool last_found = false;
	if (!entry || (tab != MENU_TAB_CONSOLES && tab != MENU_TAB_COLLECTIONS))
		return NULL;
	int art = CFG_getMenuControllerArt() ? 1 : 0;
	if (last_tab != (int)tab || last_art != art || !exactMatch(last_entry, entry->path)) {
		last_tab = tab;
		last_art = art;
		snprintf(last_entry, sizeof(last_entry), "%s", entry->path);
		last_found = TabBg_path(tab == MENU_TAB_CONSOLES ? TABBG_CONSOLES : TABBG_COLLECTIONS, entry->path, art,
								tabBgExists, last_bg, sizeof(last_bg));
	}
	return last_found ? last_bg : NULL;
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
	fresh->selected = ListWindow_reload(fresh->entries->count, GameList_rowCountAt(idx == 0), keep_selected,
										&fresh->start, &fresh->end);
	// the same path keeps its name: a hidden tab's list pushed over a tab is named for the tab (pushTabOverTab)
	char* name = fresh->name;
	fresh->name = old->name;
	old->name = name;

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

// A game has art: its screenshot (scraped, or the root .media picture a Port ships) or its box art.
static bool entryHasArt(const char* path) {
	char p[MAX_PATH];
	if (ROM_findScreenshot(path, p, sizeof(p)))
		return true;
	ROM_displayArtPath(path, ART_TYPE_BOXART, false, p, sizeof(p));
	return exists(p);
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

		if (changed) {
			rename(tmp_path, coll_path);
			CollCount_invalidate(coll_path); // its count and cached stamps are stale
		} else
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
	snprintf(settings_path, sizeof(settings_path), "%s/Tools/Settings.pak", PAKS_PATH); // as getSimpleTools lists it
	if (!exists(settings_path))
		snprintf(settings_path, sizeof(settings_path), "%s/Settings.pak", TOOLS_PATH);
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
	// a fresh press, or a held UP/DOWN's repeat, may have moved the selection
	return PAD_anyJustPressed() || PAD_justRepeated(BTN_UP) || PAD_justRepeated(BTN_DOWN) ? UI_MODAL_DIRTY
																						  : UI_MODAL_CONTINUE;
}

// Copy a name into a dialog row, cut short on a UTF-8 character boundary (never half a character), terminated.
static void copyRowText(char* dst, size_t size, const char* src) {
	size_t n = strlen(src);
	if (n >= size) {
		n = size - 1;
		while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
			n--; // src[n] continues the character before it: leave that character out
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

// Full-screen blocking collection picker. Returns the chosen collection index
// (0..count-1), COLLECTION_PICK_NEW for "New Collection…", or -1 on cancel.
#define COLLECTION_PICK_NEW (-2)
static int pickCollectionModal(Array* collections) {
	// every collection, then "New Collection..." (on the heap: the dialog takes any number of rows)
	int n = collections->count;
	ListDialogItem* items = calloc((size_t)n + 1, sizeof(ListDialogItem));
	if (!items)
		return -1;
	for (int i = 0; i < n; i++) {
		Entry* c = collections->items[i];
		copyRowText(items[i].text, LISTDIALOG_MAX_TEXT, c->name);
		items[i].prepend_icons[0] = -1;
		items[i].append_icons[0] = -1;
	}
	copyRowText(items[n].text, LISTDIALOG_MAX_TEXT, "New Collection...");
	items[n].prepend_icons[0] = -1;
	items[n].append_icons[0] = -1;
	int count = n + 1;

	ListDialog_init("Add to Collection");
	ListDialog_setItems(items, count);
	free(items); // the dialog keeps its own copy

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
//
// The alias goes where the game's console list reads it (RomKey_aliasSource): <rom dir>/map.txt keyed by the file
// name, or for a folder game (its row, or its cue/m3u from a collection, Search or Recents) the console folder's
// map.txt keyed by the folder name. Collections name their games from that same map.txt, so nothing else is written.
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

	char game_file[MAX_PATH];
	bool folder_game = entryFolderGame(entry, game_file);
	char alias_dir[MAX_PATH], alias_key[MAX_PATH], home_map[MAX_PATH];
	RomKey_aliasSource(entry->path, ROMS_PATH, alias_dir, sizeof(alias_dir), alias_key, sizeof(alias_key));
	snprintf(home_map, sizeof(home_map), "%s/map.txt", alias_dir);
	setMapAlias(home_map, alias_key, newname);

	// Older builds also wrote the name to Collections/map.txt, keyed by the bare file name (the folder game's cue/m3u):
	// collections still honour such an entry when the console map.txt has none, so it goes, or it could outlive
	// a later rename. Its other keys (a collection's own "<name>.txt") are untouched.
	char coll_map[MAX_PATH];
	snprintf(coll_map, sizeof(coll_map), "%s/map.txt", COLLECTIONS_PATH);
	const char* legacy_file = folder_game ? game_file : entry->path;
	const char* lslash = strrchr(legacy_file, '/');
	dropMapKey(coll_map, lslash ? lslash + 1 : legacy_file);
	Content_sourcesChanged(); // map.txt is a fingerprint source: Search's index must be checked again

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

	// the root (Home's pins) is the caller's MenuTabs_reload: rebuilding it here too would list it twice
	if (stack->count > 1)
		reloadDirectoryAt(stack->count - 1, sel);
	listArtForget();
	free(newname);
	return true;
}

///////////////////////////////////////
// Collections: Rename and Delete (spec 2026-10-01-menu-sp7-page-titles-design.md, "Collections")

// A row of the Collections list: a Collections/<name>.txt on the Collections tab, or in its list pushed over a tab
// while the tab is hidden. Simple mode never renames or deletes one (kids can't remove the curated collections), so
// its menu items, hint and actions all key off this.
static bool isCollectionRow(Entry* entry) {
	size_t n = strlen(COLLECTIONS_PATH);
	return !gl_simple_mode && entry && GameList_lookTab() == MENU_TAB_COLLECTIONS && exactMatch(top->path, COLLECTIONS_PATH) && entry->type == ENTRY_DIR &&
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

// The row of `list` showing `path` (after a reload), or -1.
static int rowFor(Directory* list, const char* path) {
	for (int i = 0; i < list->entries->count; i++)
		if (exactMatch(((Entry*)list->entries->items[i])->path, path))
			return i;
	return -1;
}

// After a collection changed: the tabs (MenuTabs_reload), and the Collections list when it is pushed over a tab
// (the hidden tab's), keeping its row `sel`. Returns the Collections list.
static Directory* reloadCollections(int root_sel, int sel) {
	MenuTabs_reload(root_sel, MENU_RELOAD_PINS | MENU_RELOAD_COLLECTIONS);
	if (stack->count > 1)
		reloadDirectoryAt(stack->count - 1, sel);
	return stack->items[stack->count - 1];
}

// Every collection's name, for CollName_validate: names[i] points into stems. Returns the count (0 when memory ran
// out); the caller frees stems and names.
static int collectionNames(char (**stems_out)[MAX_PATH], const char*** names_out) {
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
	*stems_out = stems;
	*names_out = names;
	return n;
}

// Rename: the keyboard prefilled with the name; a rejected name says why and reopens the keyboard with it. The
// games and their order stay (the file is moved, not rewritten). Returns true when the collection was renamed.
static bool doRenameCollection(Entry* entry, int root_sel) {
	int sel = top->selected;
	char old_path[MAX_PATH], old_stem[MAX_PATH];
	snprintf(old_path, sizeof(old_path), "%s", entry->path);
	collectionStem(old_path, old_stem);

	char (*stems)[MAX_PATH];
	const char** names;
	int n = collectionNames(&stems, &names);

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
	Directory* list = reloadCollections(root_sel, sel);
	int row = rowFor(list, new_path);
	if (row >= 0 && row != list->selected) { // windowed as reloadDirectoryAt
		list->selected = row;
		ListWindow_reveal(list->entries->count, GameList_rowCountAt(stack->count == 1), row, &list->start,
						  &list->end);
	}
	free(newname);
	return true;
}

// Delete: only Collections/<name>.txt goes (the games stay), with its alias, pins and count. The highlight lands on
// the next row (the same index), else the previous one; deleting the last collection drops the tab.
static void doDeleteCollection(Entry* entry, int root_sel) {
	int sel = top->selected;
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
	reloadCollections(root_sel, sel); // entry is freed from here on
}

// Add to Collection: the picker, or "New Collection..." and its name on the keyboard (a rejected name says why and
// reopens the keyboard with it, as Rename does). A new collection rebuilds the tabs: the Collections tab (or the
// root menu's Collections item) may appear with it.
static void doAddToCollection(const char* rom_path, int root_sel, int sel) {
	Array* collections = getCollections();
	int pick = pickCollectionModal(collections);

	if (pick == COLLECTION_PICK_NEW) {
		char (*stems)[MAX_PATH];
		const char** names;
		int n = collectionNames(&stems, &names);
		char* attempt = NULL; // the keyboard opens empty
		char* name = NULL;
		for (;;) {
			name = UIKeyboard_openWith("New collection name", attempt, 0);
			requestBackgroundReupload(); // keyboard cleared the layers, same row stays
			free(attempt);
			attempt = NULL;
			if (!name) // B, or an emptied line
				break;
			CollNameResult r = CollName_validate(NULL, name, names, n);
			if (r == COLLNAME_OK)
				break;
			collectionNotice("Can't use that name", CollName_reason(r));
			attempt = name; // back to the keyboard with what was typed
			name = NULL;
		}
		free(stems);
		free(names);
		if (name) {
			mkdir_p(COLLECTIONS_PATH);
			char coll_path[MAX_PATH];
			snprintf(coll_path, sizeof(coll_path), "%s/%s.txt", COLLECTIONS_PATH, name);
			addRomToCollectionFile(coll_path, rom_path);
			CollCount_invalidate(coll_path);
			// the first collection shows the Collections tab (or its root menu item); a list pushed over the root may
			// be the Collections one
			MenuTabs_reload(root_sel, MENU_RELOAD_COLLECTIONS);
			if (stack->count > 1)
				reloadDirectoryAt(stack->count - 1, sel);
			free(name);
		}
	} else if (pick >= 0 && pick < collections->count) {
		Entry* coll = collections->items[pick];
		addRomToCollectionFile(coll->path, rom_path);
		CollCount_invalidate(coll->path); // the stamps would catch it too; FAT mtime is 2 s coarse
	}

	EntryArray_free(collections);
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

// A pak opts into Launch Options (per-game cores / CPU / GPU, then launch) by
// shipping launch_options.sh beside its launch.sh.
static char launchopts_cap_path[MAX_PATH] = {0};
static bool launchopts_cap = false;
static bool entryLaunchOptionsCapable(Entry* entry) {
	return entryEmuMarker(entry, "launch_options.sh", launchopts_cap_path, &launchopts_cap);
}

#define ARTFETCH_STATUS_PATH "/tmp/nextui_artfetch.status"
#define ARTFETCH_TIMEOUT_MS 120000 // up to 4 downloads plus composing the mix
#define ARTFETCH_RESULT_MS 1500
#define ARTFETCH_PENDING 4					 // cancelled fetches still watched for their art
#define ARTFETCH_PENDING_MS (10 * 60 * 1000) // how long one is watched before it is given up
#define ARTFETCH_POLL_MS 1000

// Each fetch's own status file (this nextui's pid and a count): a cancelled or timed-out scraper is left running and
// still writes its file, which must not be read as a later fetch's.
static void artFetchStatusPath(char* out, size_t size) {
	static unsigned run = 0;
	// an earlier nextui's parked fetches are watched by no one (it quit on a launch): their files go
	char mine[32];
	snprintf(mine, sizeof(mine), "nextui_artfetch.status.%d.", (int)getpid());
	DIR* dh = opendir("/tmp");
	if (dh) {
		struct dirent* dp;
		while ((dp = readdir(dh))) {
			if (prefixMatch("nextui_artfetch.status.", dp->d_name) && !prefixMatch(mine, dp->d_name)) {
				char stale[MAX_PATH];
				snprintf(stale, sizeof(stale), "/tmp/%s", dp->d_name);
				unlink(stale);
			}
		}
		closedir(dh);
	}
	snprintf(out, size, "%s.%d.%u", ARTFETCH_STATUS_PATH, (int)getpid(), ++run);
}

// A fetch's art landed: drop every cached copy of the game's pictures.
static void artFetchLanded(const char* rom, const char* out_png) {
	// the List's thumbnails, each in both compose modes (the root .media picture, the screenshot, box art, and List
	// art's mix, 2D box art, wheel and 3D box art) and the Carousel, Grid and Backdrop pictures (HomeArt, which may
	// hold a placeholder or a miss for it)
	char variant[MAX_PATH];
	thumbCacheInvalidate(out_png);
	ROM_displayArtPath(rom, ART_TYPE_SCREENSHOT, false, variant, sizeof(variant));
	thumbCacheInvalidate(variant);
	ROM_displayArtPath(rom, ART_TYPE_BOXART, false, variant, sizeof(variant));
	thumbCacheInvalidate(variant);
	const int fitted[] = {GAME_LIST_ART_MIX, GAME_LIST_ART_BOXART2D, GAME_LIST_ART_WHEEL, GAME_LIST_ART_BOXART3D};
	for (int i = 0; i < (int)(sizeof(fitted) / sizeof(fitted[0])); i++) {
		ROM_findListArt(rom, fitted[i], variant, sizeof(variant)); // a miss names the screenshot
		thumbCacheInvalidate(variant);
	}
	HomeArt_forget(rom);
	listArtForget();
}

// Delete Rom: remove the deleted game's art, every picture the scraper writes and the lists read
// (<dir>/.media/<variant>/<stem>.png and the root <dir>/.media/<stem>.png, and a .jpg/.jpeg of each). art_rom is the ROM file, or a folder game's
// folder (its art is named after the folder, beside it). Exact paths only: another game whose name starts the same keeps
// its art, and the console's own background (.media/bg.png) is never a game's. Saves and states are the player's: kept.
static void deleteGameArt(const char* art_rom) {
	static const char* variants[] = {NULL, "screenshot", "boxart", "boxart2d", "mix", "wheel"};
	for (int i = 0; i < (int)(sizeof(variants) / sizeof(variants[0])); i++) {
		char png[MAX_PATH];
		ROM_mediaArtVariantPath(art_rom, variants[i], png, sizeof(png));
		if (RomKey_artReserved(png))
			continue;
		unlink(png);
		thumbCacheInvalidate(png); // a game later added under the name must not show the cached picture
		// the same picture as a .jpg/.jpeg (the companion app's art): ROM_artExists finds each in turn once the
		// .png is gone
		char alt[MAX_PATH];
		for (;;) {
			snprintf(alt, sizeof(alt), "%s", png);
			if (!ROM_artExists(alt, sizeof(alt)) || unlink(alt) != 0)
				break;
			thumbCacheInvalidate(alt);
		}
	}
}

// Fetches cancelled (B) or timed out while their scraper runs on: watched from the game list (artFetchPoll) so their
// art shows once it lands. The oldest gives way to a new one.
static struct {
	bool on;
	unsigned long since;
	char status[MAX_PATH], rom[MAX_PATH], out_png[MAX_PATH];
} artfetch_pending[ARTFETCH_PENDING];

static void artFetchPark(const char* status, const char* rom, const char* out_png, unsigned long now) {
	int slot = 0;
	for (int i = 0; i < ARTFETCH_PENDING; i++) {
		if (!artfetch_pending[i].on) {
			slot = i;
			break;
		}
		if (artfetch_pending[i].since < artfetch_pending[slot].since)
			slot = i;
	}
	if (artfetch_pending[slot].on)
		unlink(artfetch_pending[slot].status);
	artfetch_pending[slot].on = true;
	artfetch_pending[slot].since = now;
	snprintf(artfetch_pending[slot].status, MAX_PATH, "%s", status);
	snprintf(artfetch_pending[slot].rom, MAX_PATH, "%s", rom);
	snprintf(artfetch_pending[slot].out_png, MAX_PATH, "%s", out_png);
}

// Once a second: a parked fetch that finished drops its game's cached art (and redraws), one that failed or ran too
// long is let go; either way its status file goes.
static void artFetchPoll(unsigned long now, bool* dirty) {
	static unsigned long last = 0;
	if (now - last < ARTFETCH_POLL_MS)
		return;
	last = now;
	for (int i = 0; i < ARTFETCH_PENDING; i++) {
		if (!artfetch_pending[i].on)
			continue;
		char status[32] = "";
		getFile(artfetch_pending[i].status, status, sizeof(status));
		char* nl = strchr(status, '\n');
		if (nl)
			*nl = '\0';
		bool done = strcmp(status, "done") == 0;
		if (done) {
			artFetchLanded(artfetch_pending[i].rom, artfetch_pending[i].out_png);
			*dirty = true;
		}
		if (done || strcmp(status, "notfound") == 0 || strcmp(status, "error") == 0 ||
			now - artfetch_pending[i].since > ARTFETCH_PENDING_MS) {
			unlink(artfetch_pending[i].status);
			artfetch_pending[i].on = false;
		}
	}
}

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
	const char* rom;	// the entry's path: HomeArt's key for its pictures
	const char* status; // this fetch's status file (artFetchStatusPath)
	bool landed;		// "done" was read: the art is in
} ArtFetchModalCtx;

static void artFetchModal_render(SDL_Surface* screen, void* vctx) {
	ArtFetchModalCtx* ctx = vctx;
	const char* sub = ctx->result;
	if (!sub) {
		if (strcmp(ctx->stage, "searching") == 0)
			sub = "Searching...";
		else if (strcmp(ctx->stage, "downloading") == 0)
			sub = "Downloading...";
		else if (strcmp(ctx->stage, "saving") == 0)
			sub = "Adding art...";
		else if (strcmp(ctx->stage, "compositing") == 0)
			sub = "Building mix...";
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
			getFile((char*)ctx->status, status, sizeof(status));
			char* nl = strchr(status, '\n');
			if (nl)
				*nl = '\0';

			if (strcmp(status, "done") == 0) {
				artFetchLanded(ctx->rom, ctx->out_png);
				ctx->landed = true;
				ctx->result = "Artwork added";
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
// left to finish on its own, watched by artFetchPoll so its art shows if it lands).
// On "done" the thumbnail cache is invalidated so the game list reloads the new
// art when the modal closes (nextui.c marks the list dirty after runContextAction).
// Mirrors the blocking-modal idiom used by Delete/Rename in this same dispatcher.
static void artFetchModal(const char* game_name, const char* rom, const char* out_png, const char* status) {
	char title[256];
	artFetchTitle(game_name, title, sizeof(title));
	ArtFetchModalCtx ctx = {
		.start = SDL_GetTicks(),
		.stage = "starting",
		.title = title,
		.out_png = out_png,
		.rom = rom,
		.status = status,
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

	// a fetch that ended on its own result is over; a cancelled or timed-out one may still land its art
	char last[32] = "";
	getFile((char*)status, last, sizeof(last));
	if (ctx.landed || strncmp(last, "notfound", 8) == 0 || strncmp(last, "error", 5) == 0)
		unlink(status);
	else
		artFetchPark(status, rom, out_png, SDL_GetTicks());
	GFX_clearLayers(LAYER_ALL);
}

static Entry* ctx_target = NULL; // Home's entry while its menu is open (an owned copy)

void GameList_contextMenuClosed(void) {
	if (ctx_target)
		Entry_free(ctx_target);
	ctx_target = NULL;
}

// Refresh Roms: rescan the emulator list and rebuild every tab
static void ctxRefresh(int root_sel) {
	Content_invalidateEmulist();
	// the marker probes and the List's art are per path: a pak or art added on the card since must show
	netplay_cap_path[0] = '\0';
	emuopts_cap_path[0] = '\0';
	listArtForget();
	thumbCacheForgetMisses();
	HomeArt_forgetAll();
	MenuTabs_reload(root_sel, MENU_RELOAD_ALL);
	// a hidden tab's list pushed over the root (its menu offers Refresh Roms too)
	for (int i = 1; i < stack->count; i++)
		reloadDirectoryAt(i, ((Directory*)stack->items[i])->selected);
}

// Pin Tool / Pin Item: add a shortcut and refresh the pins
static void ctxPin(Entry* entry, int root_sel, int sel) {
	if (entry) {
		Shortcuts_add(entry);
		GameList_invalidateBackground();
		MenuTabs_reload(root_sel, MENU_RELOAD_PINS);
		if (stack->count > 1)
			reloadDirectoryAt(stack->count - 1, sel);
	}
}

// Unpin (root row, Tool or Item): drop the shortcut and refresh the pins
static void ctxUnpin(Entry* entry, int root_sel, int sel) {
	if (entry) {
		Shortcuts_remove(entry);
		GameList_invalidateBackground();
		MenuTabs_reload(root_sel, MENU_RELOAD_PINS);
		if (stack->count > 1)
			reloadDirectoryAt(stack->count - 1, sel);
	}
}

// Delete Rom: confirm, remove the file/folder, prune collections, aliases and the rom index
static void ctxDeleteRom(Entry* entry, int root_sel, int sel) {
	if (entry) {
		char game_file[MAX_PATH];
		char parent_dir[MAX_PATH];
		bool folder_game = entryFolderGame(entry, game_file);
		if (entry->type != ENTRY_ROM && !folder_game)
			return;
		if (confirmModal("Delete ROM?", entry->name)) {
			Content_sourcesChanged(); // a card edit made over SFTP since the start must not be stamped fresh
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
			// the index, collection and alias drops only follow a delete that happened (a read-only
			// card or a busy file leaves the game listed, so it must stay counted and named)
			if (!exists((char*)removed)) {
				// prune the deleted game from any collection that lists it
				// (folder games are stored as their resolved cue/m3u path)
				const char* del_line = folder_game ? game_file : entry->path;
				if (prefixMatch(SDCARD_PATH, del_line))
					removeCollectionLines(del_line + strlen(SDCARD_PATH));
				// drop its display alias too, so a future file that reuses the
				// name doesn't inherit the dead entry's alias (mirrors rename).
				// Keyed by what went: a folder game's folder (also when it went
				// through its cue/m3u) in the console's map.txt
				{
					char adir[MAX_PATH];
					strncpy(adir, removed, sizeof(adir) - 1);
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
				// its art (kept by the folder game's name for a folder game) and its cached and generated pictures
				deleteGameArt(removed);
				HomeArt_forget(entry->path);
				HomeArt_forget(del_line);
				HomeArt_dropPlaceholders(del_line);
				HomeArt_dropPlaceholders(removed);
				// drop it from the rom index so console counts and Search follow (after the
				// map.txt clean-up: both feed the index fingerprint)
				Content_forgetRom(removed, caches_fresh);
			}
			// root too: a pinned copy of the deleted rom must not linger
			// as a dead shortcut (mirrors pin/unpin)
			MenuTabs_reload(root_sel, MENU_RELOAD_ALL);
			listArtForget();
			// every list pushed over the root (e.g. Home > Consoles > GBA: the
			// Consoles list counts GBA's games), not only the top one
			for (int i = 1; i < stack->count; i++)
				reloadDirectoryAt(i, i == stack->count - 1 ? sel : ((Directory*)stack->items[i])->selected);
		}
	}
}

// Rename Rom: rename a rom or folder game, then refresh pins and recents
static void ctxRenameRom(Entry* entry, int root_sel, int sel) {
	if (entry) {
		char game_file[MAX_PATH];
		if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file))
			if (doRename(entry, sel))
				// root too: refresh any pinned copy (mirrors pin/unpin); the recents carry the new alias
				MenuTabs_reload(root_sel, MENU_RELOAD_PINS | MENU_RELOAD_RECENTS);
	}
}

// Add to Collection: add the rom (or a folder game's resolved cue/m3u)
static void ctxAddToCollection(Entry* entry, int root_sel, int sel) {
	if (entry) {
		char game_file[MAX_PATH];
		if (entry->type == ENTRY_ROM)
			doAddToCollection(entry->path, root_sel, sel);
		else if (entryFolderGame(entry, game_file))
			// collections hold file paths: write the resolved cue/m3u
			doAddToCollection(game_file, root_sel, sel);
	}
}

// Launch with Netplay: arm the netplay flag and launch, disarming it if nothing was queued
static void ctxNetplay(Entry* entry, bool from_home) {
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
}

// Emulator Options: run the pak's pre-launch options.sh for this rom
static void ctxEmuOptions(Entry* entry, bool from_home) {
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
}

// Launch Options: run the pak's launch_options.sh for this rom. Its Launch row leaves
// PENDING_LAUNCH_PATH, and the restarted nextui launches the rom (pendingLaunch).
static void ctxLaunchOptions(Entry* entry, bool from_home) {
	if (entry && entryLaunchOptionsCapable(entry)) {
		// folder games: the resolved cue/m3u, as for Emulator Options
		char game_file[MAX_PATH];
		char* rom_arg = entry->path;
		if (entry->type == ENTRY_DIR && entryFolderGame(entry, game_file))
			rom_arg = game_file;
		char pak_path[MAX_PATH];
		if (entryEmuMarkerPath(entry, "launch_options.sh", pak_path)) {
			// from Home the return (B, or the game Launch starts) must open Home; see ctxEmuOptions
			if (from_home)
				MenuTabs_markHomeLaunch();
			openScript(pak_path, rom_arg, entry->path);
			MenuTabs_clearHomeLaunch();
		}
	}
}

// Fetch Artwork: spawn the scraper for this rom behind a blocking progress modal
static void ctxFetchArt(Entry* entry) {
	if (entry) {
		char af_rom[MAX_PATH], af_out[MAX_PATH], af_tag[MAX_PATH];
		if (!entryArtInfo(entry, af_rom, af_out, af_tag))
			return;
		if (!Wifi_isConnected()) {
			artFetchNotice(entry->name, "Connect to WiFi to fetch art");
			return;
		}
		char status[MAX_PATH];
		artFetchStatusPath(status, sizeof(status));
		putFile(status, "starting");
		openArtFetch(af_rom, af_out, af_tag, status);
		artFetchModal(entry->name, entry->path, af_out, status); // blocking modal until done/cancel/timeout
	}
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
		ctxRefresh(root_sel);
		break;
	case 2: // Tools (root, offered while the Tools tab is hidden)
	case 4: // Consoles (same, Consoles tab)
	case 5: // Collections (same, Collections tab)
		// Push the tab's list over the current tab so B comes back to it.
		// It is a direct child of the Home tab only; from any other tab
		// openDirectory would rebuild the stack on Home.
		{
			MenuTabId pushed = MENU_TAB_TOOLS;
			if (id == 4)
				pushed = MENU_TAB_CONSOLES;
			else if (id == 5)
				pushed = MENU_TAB_COLLECTIONS;
			pushTabOverTab(pushed, NULL);
		}
		break;
	case 20: // Pin Tool
	case 30: // Pin Item
		ctxPin(entry, root_sel, sel);
		break;
	case 3:	 // Unpin (root pinned row)
	case 21: // Unpin Tool
	case 31: // Unpin Item
		ctxUnpin(entry, root_sel, sel);
		break;
	case 32: // Delete Rom
		ctxDeleteRom(entry, root_sel, sel);
		break;
	case 33: // Rename Rom
		ctxRenameRom(entry, root_sel, sel);
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
		ctxAddToCollection(entry, root_sel, sel);
		break;
	case 39: // Resume (offered while button hints are hidden), as X
		readyResume(entry);
		if (entry && resume.can_resume) {
			resume.should_resume = true;
			Entry_open(entry);
		}
		break;
	case 35: // Launch with Netplay
		ctxNetplay(entry, from_home);
		break;
	case 36: // Emulator Options (pre-launch editor)
		ctxEmuOptions(entry, from_home);
		break;
	case 38: // Launch Options (per-game cores / CPU / GPU, then launch)
		ctxLaunchOptions(entry, from_home);
		break;
	case 37: // Fetch Artwork
		ctxFetchArt(entry);
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

// readyResume for the list's selected row, skipped when nothing changed since this list's last probe: the
// render pass asks on every dirty frame, and each async completion (thumbnail, GameInfo, CollCount, HomeArt,
// HomeStats, status bar) dirties one, while the probe stats the card several times (save slot, .m3u,
// preview/box art). Keyed on (Directory serial, selected row, tab generation, entry path) plus
// readyResumeCount(), so a probe by anything else (Search, the switcher, Home, a context action) since
// makes the next call probe again. Home keeps its own per-focus probe (home.c readyFocus).
static void readySelectedResume(Entry* entry) {
	static unsigned last_list, last_gen, last_count;
	static int last_selected = -1;
	static char last_path[MAX_PATH];
	if (!entry)
		return;
	if (last_selected == top->selected && last_list == top->serial && last_gen == MenuTabs_generation() &&
		last_count == readyResumeCount() && strcmp(last_path, entry->path) == 0)
		return;
	readyResume(entry);
	last_list = top->serial;
	last_gen = MenuTabs_generation();
	last_selected = top->selected;
	last_count = readyResumeCount();
	snprintf(last_path, sizeof(last_path), "%s", entry->path);
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
	char label[CONTEXTMENU_MAX_TEXT];
	// Button hints hidden (Layouts > Button hints): the bar's game actions move here, with their button. Not A/B, and
	// not Resume on Home (its A already resumes). Netplay regardless of Appearance > Show netplay hint.
	if (!CFG_getButtonHints()) {
		if (!Home_active() && resume.can_resume) { // the selected row's, as the X handler reads it
			snprintf(label, sizeof(label), "Resume (%s)", PAD_buttonLabel("X"));
			addItem(items, idx, label, 39);
		}
		if (entryNetplayCapable(entry)) {
			snprintf(label, sizeof(label), "Netplay (%s)", PAD_buttonLabel("Y"));
			addItem(items, idx, label, 35);
		}
	}
	if (allow_pin && canPinEntry(entry)) {
		if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH)))
			addItem(items, idx, "Unpin Item", 31);
		else if (Shortcuts_canAdd(entry))
			addItem(items, idx, "Pin Item", 30);
	}
	char game_file[MAX_PATH];
	if (entry->type == ENTRY_ROM || entryFolderGame(entry, game_file)) {
		addItem(items, idx, "Delete Rom", 32);
		addItem(items, idx, "Rename Rom", 33);
		addItem(items, idx, "Add to Collection", 34);
		if (entryEmuOptionsCapable(entry))
			addItem(items, idx, "Emulator Options", 36);
		if (entryLaunchOptionsCapable(entry))
			addItem(items, idx, "Launch Options", 38);
		// offered (in any style, never on Home) while the game has neither a screenshot nor a box art: the scraper
		// saves the screenshot and box art, plus the 2D box art, wheel and mix when Artwork Manager's settings ask for them
		char af_rom[MAX_PATH], af_out[MAX_PATH], af_tag[MAX_PATH];
		if (!Home_active() && entryArtInfo(entry, af_rom, af_out, af_tag) && !entryHasArt(entry->path))
			addItem(items, idx, "Fetch Artwork", 37);
	}
}

// A tool's item (Tools tab, Tools listing, a pinned tool on Home).
static void toolItems(Entry* entry, ContextMenuItem* items, int* idx) {
	if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH)))
		addItem(items, idx, "Unpin Tool", 21);
	else if (Shortcuts_canAdd(entry)) // full (MAX_SHORTCUTS, or the tool cap): unpin something first
		addItem(items, idx, "Pin Tool", 20);
}

// The main menu's own items.
static void rootItems(ContextMenuItem* items, int* idx) {
	addItem(items, idx, "Refresh Roms", 1);
	// A hidden tab's list stays reachable here (Layouts > Consoles, Collections and Tools tab). Tools must:
	// Settings.pak lives inside Tools, so hiding Tools would otherwise lock the user out of re-enabling it.
	// A visible tab is one R1 away, so its item is left out. Pushed only over a tab (the root), never over a
	// hidden tab's list already pushed.
	if (stack && stack->count > 1)
		return;
	if (MenuTabs_pushable(MENU_TAB_CONSOLES))
		addItem(items, idx, "Consoles", 4);
	if (MenuTabs_pushable(MENU_TAB_COLLECTIONS))
		addItem(items, idx, "Collections", 5);
	if (MenuTabs_pushable(MENU_TAB_TOOLS))
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
	MenuTabs_leaveFocus(); // the menu opens over lit content
	ContextMenu_open(items, idx);
}

// UP on the tab row: focus returns to the bottom of the content (List's last row, Grid's bottom row in the same
// column, Home's last pin, a vertical Carousel's last item; a horizontal Carousel is one row, so the item stays).
static void contentToBottom(void) {
	if (Home_active()) {
		Home_focusBottom();
		return;
	}
	bool vertical = StackView_active();
	if (RowView_active() && !vertical)
		return;
	int total = top->entries->count;
	if (total <= 0)
		return;
	if (vertical) {
		StackView_focusBottom();
	} else if (GridView_active()) {
		GridView_focusBottom();
	} else { // List: the last row, windowed as the List's own wrap to the bottom
		top->selected = total - 1;
		ListWindow_toBottom(total, GameList_rowCount(), &top->start, &top->end);
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

// This frame's MENU, SELECT and START taps, read once at the top of GameList_handleInput: a tap tracker sees the press
// only on its justPressed frame, so one skipped by a view that returns early (Home, Grid, a row) would lose the tap.
static bool tap_menu, tap_select, tap_start;

// The tab row has focus (spec 2026-10-01-menu-sp6-tab-focus-design.md). It owns the D-pad, A, B, X, Y and L1/R1 until
// the content takes focus back: LEFT/RIGHT and L1/R1 switch tabs (fresh presses; focus stays), DOWN, A and B return to
// the content on the tab's item, UP wraps to its bottom, the rest does nothing. SELECT, START, MENU and the F keys
// leave the menu: they hand focus back and keep their meaning (false: the root's handlers run).
static bool tabRowInput(IndicatorType show_setting, GameListResult* result, bool* dirty) {
	if (tap_select || tap_start || tap_menu ||
		(HAS_FN_KEYS && (PAD_justPressed(BTN_FN1) || PAD_justPressed(BTN_FN2)))) {
		MenuTabs_leaveFocus(); // not a d-pad move: the content snaps lit (a screen slides in or a menu opens over it)
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

// B: close the list (back to its parent, or the root tab in its layout). The page slides right unless a Backdrop
// game list is involved (nextui.c drops the slide: its picture fades from and to black instead).
static void leaveList(GameListResult* result, bool* dirty) {
	closeDirectory();
	result->animdir = SLIDE_RIGHT;
	*dirty = true;
	if (top->entries->count > 0)
		readyResume(top->entries->items[top->selected]);
}

GameListResult GameList_handleInput(unsigned long now, int currentScreen,
									IndicatorType show_setting, bool* dirty) {
	GameListResult result = {
		.screen = currentScreen,
		.animdir = ANIM_NONE,
		.folderbgchanged = false,
	};

	// every frame, whatever returns early below (each tracker ticked once: a tap is spent here, not misread later)
	tap_menu = PAD_tappedMenu(now);
	tap_select = PAD_tappedSelect(now);
	tap_start = PAD_tappedStart(now);
	artFetchPoll(now, dirty); // a cancelled fetch's art may have landed since

	// B's fade out of a Backdrop game list (RowView_beginExit) takes no input while it runs. When its time is up the
	// list closes; a key pressed during it closes the list at once and is then handled below, on the screen B
	// returned to (the tab in its layout, or the parent list): pressed once, handled once.
	if (RowView_exiting()) {
		if (!RowView_exitStep(PAD_anyJustPressed())) {
			// the tap trackers ticked above; the long-press one keeps seeing the edges too (as nextui.c does under the
			// context menu): a MENU held into the fade and released during it is spent here, not misread later
			PAD_longPressedMenu(now);
			return result;
		}
		leaveList(&result, dirty);
	}

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

	if (row_focused && tabRowInput(show_setting, &result, dirty))
		return result;

	// Home owns the D-pad, A and MENU (Y is its netplay launch, below); SELECT, START, the F keys and L1/R1 fall
	// through to the root's handlers below, and the list's own row actions (X, Y, A, the row moves) stay off.
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
		// Y on a pinned game or the Continue card launches it with netplay, as the list's Y does
		if (!MenuTabs_focused() && PAD_justReleased(BTN_Y) && !(row_released & BTN_Y) && !PAD_isPressed(BTN_L2) &&
			!PAD_isPressed(BTN_R2)) {
			ctxNetplay(Home_focusedEntry(), true);
			*dirty = true;
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
				readySelectedResume(top->entries->items[top->selected]);
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
				readySelectedResume(top->entries->items[top->selected]);
			return result;
		}
	}

	int selected = top->selected;
	int total = home ? 0 : top->entries->count;
	int row_count = GameList_rowCount();

	if (!home && tap_menu && !ContextMenu_isOpen()) {
		// Open contextual menu based on current page
		Entry* entry = (total > 0) ? top->entries->items[selected] : NULL;
		int idx = 0;
		ContextMenuItem items[CONTEXTMENU_MAX_ITEMS];

		if (Shortcuts_isInToolsFolder(top->path)) {
			// Tools tab or Tools listing. Simple mode's Tools tab (Settings
			// only) gets no menu: kids can't unpin the curated shortcuts.
			if (entry && !gl_simple_mode)
				toolItems(entry, items, &idx);
		} else if (GameList_lookTab() >= 0) {
			// Root menu (Consoles or Collections tab, or its list pushed over a tab
			// while the tab is hidden; Home handles its own MENU)
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
			MenuTabs_leaveFocus();		  // the menu opens over lit content
			ContextMenu_open(items, idx);
			*dirty = true;
		}

		return result;
	} else if (tap_select) {
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
	} else if (stack->count == 1 && PAD_justRepeated(BTN_UP) && total == 0) {
		// Root, List style, an empty tab: UP focuses the tab row (a fresh press). With rows, UP on the first one wraps
		// to the last below, as in a game list (it never goes to the tab row)
		if (PAD_justPressed(BTN_UP)) {
			MenuTabs_setFocused(true);
			*dirty = true;
		}
		return result;
	} else if (total > 0) {
		// one row; a held key stops at the edge, a fresh press wraps
		if (PAD_justRepeated(BTN_UP))
			ListWindow_step(total, row_count, -1, PAD_justPressed(BTN_UP), &selected, &top->start, &top->end);
		else if (PAD_justRepeated(BTN_DOWN))
			ListWindow_step(total, row_count, 1, PAD_justPressed(BTN_DOWN), &selected, &top->start, &top->end);
		// page jump in game lists only (the root's LEFT/RIGHT switch tabs above; a held one does nothing)
		if (stack->count > 1 && PAD_justRepeated(BTN_LEFT))
			ListWindow_page(total, row_count, -1, &selected, &top->start, &top->end);
		else if (stack->count > 1 && PAD_justRepeated(BTN_RIGHT))
			ListWindow_page(total, row_count, 1, &selected, &top->start, &top->end);
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
			if (total > row_count)
				ListWindow_selectAtTop(total, row_count, selected, &top->start, &top->end);
		}
	} else if (stack->count > 1 && total > 0 && PAD_justRepeated(BTN_R1) &&
			   !PAD_isPressed(BTN_L1) &&
			   !PWR_ignoreSettingInput(BTN_R1, show_setting)) { // next alpha
		Entry* entry = top->entries->items[selected];
		int i = entry->alpha + 1;
		if (i < top->alphas.count) {
			selected = top->alphas.items[i];
			if (total > row_count)
				ListWindow_selectAtTop(total, row_count, selected, &top->start, &top->end);
		}
	}

	if (selected != top->selected) {
		top->selected = selected;
		*dirty = true;
	}

	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;

	if (*dirty && total > 0)
		readySelectedResume(entry); // once per selection change, not once per dirty frame

	if (total > 0 && resume.can_resume && PAD_justReleased(BTN_RESUME) && !(row_released & BTN_RESUME) &&
		!PAD_isPressed(BTN_L2) && !PAD_isPressed(BTN_R2)) {
		resume.should_resume = true;
		Entry_open(entry);
		*dirty = true;
	}
	// Y launches netplay-capable ROMs with netplay (works at root for
	// pinned games too — root Search moved to START for this)
	else if (total > 0 && PAD_justReleased(BTN_Y) && !(row_released & BTN_Y) && !PAD_isPressed(BTN_L2) &&
			 !PAD_isPressed(BTN_R2) && entryNetplayCapable(entry)) {
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
	else if (stack->count == 1 && tap_start) {
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
		*dirty = true;
		if (!RowView_beginExit()) // a Backdrop list with a picture closes when its fade-out ends
			leaveList(&result, dirty);
	}

	return result;
}

// True when the selected entry is a game (a ROM or a folder game) that gets info lines. The folder-game
// check stats the disk, so it is remembered for the selected path of the current list (a reload or another
// list starts over) instead of rerun every frame.
static bool selectedIsGame(Entry* entry) {
	static char last_path[MAX_PATH];
	static unsigned last_list; // the Directory serial
	static unsigned last_gen;
	static bool last_is_game;
	if (!entry)
		return false;
	if (entry->type == ENTRY_ROM)
		return true;
	// keyed on the Directory serial (a new Directory can land at a freed one's address)
	if (top->serial != last_list || MenuTabs_generation() != last_gen ||
		strcmp(last_path, entry->path) != 0) {
		char game_file[MAX_PATH];
		last_list = top->serial;
		last_gen = MenuTabs_generation();
		snprintf(last_path, sizeof(last_path), "%s", entry->path);
		last_is_game = entryFolderGame(entry, game_file);
	}
	return last_is_game;
}

// The main menu (the root: a tab's own list) or a game list (anything pushed over it). Before the stack exists
// (boot), the first list built is a root.
static bool atRoot(void) {
	return !stack || stack->count <= 1;
}

// Top of the first row (no arrow gutter; the arrows live in the info band): game lists right under their header;
// the main menu 12 dp below the tab row.
static int listTopAt(bool root) {
	int strip = BAR_HEIGHT; // the header / tab row (MenuTabs_renderRow's bar_h)
	return root ? strip + NX_DP(12) : strip;
}

static int listTop(void) {
	return listTopAt(atRoot());
}

// The rows' and the band's geometry (infoband_layout.c, host-tested), main menu and game lists alike: a fixed 26 dp
// band (18 dp line, 4 dp each side) with its bottom min(12 dp, the bar's ink top) inside the hint bar, and whole px
// rows (nxListFit at the PILL_SIZE pitch) filling the slot from the list top down to the band's top. Only the list top
// differs (listTopAt).
// The rows' pitch and text: the main menu's per device (ui_text_sizes.h), the game lists' the UI scale's.
static int listPitch(bool root) {
	return root ? (int)TextPx_for(MENU_LIST_PITCH, UIScale_deviceIndex(UI_DEVICE_NAME)) : SCALE1(PILL_SIZE);
}
static TTF_Font* listFont(bool root) {
	return root ? UI_mainListFont() : font.large; // font.large where the sizes match
}

static InfoBandLayout listLayoutFrom(int list_top, bool root) {
	int screen_h = screen ? screen->h : FIXED_HEIGHT;
	int bar_h = BAR_HEIGHT; // the hint bar (UI_buttonHintBarTop)
	// the bar's ink top: the padding over its centred BUTTON_SIZE icons, plus the glyphs' margin (host-tested)
	int overlap = InfoBand_overlap(NX_DP(12), InfoBand_hintInkTop(bar_h, SCALE1(BUTTON_SIZE)));
	InfoBandLayout l =
		InfoBand_fixedLayout(screen_h, bar_h, list_top, listPitch(root), NX_DP(18), NX_DP(4), overlap);
	// The band paints no fade of its own, only its arrows (and text): with the hints shown the bar's scrim is under it
	// and a game's info is off; hidden (Layouts > Button hints), the same rows and band, a game list keeping the bar's
	// scrim alone (renderHintScrim) with the info line moved into its row
	l.fill_bottom = l.band_top;
	return l;
}

static InfoBandLayout listLayoutAt(bool root) {
	return listLayoutFrom(listTopAt(root), root);
}

// The List shown: the Game Switcher's (Layouts > Game Switcher layout: List, as a game list's), Home's (Layouts > Home
// layout: List; its rows under the stats strip) or the top Directory's.
static Directory* shownList(void) {
	Directory* sw = GameSwitcher_listDir();
	if (sw)
		return sw;
	Directory* home = Home_listDir();
	return home ? home : top;
}

static int shownListTop(void) {
	if (GameSwitcher_isList())
		return listTopAt(false);
	return Home_isList() ? Home_listTop() : listTop();
}

static InfoBandLayout listLayout(void) {
	if (GameSwitcher_isList())
		return listLayoutAt(false);
	return Home_isList() ? listLayoutFrom(Home_listTop(), true) : listLayoutAt(atRoot());
}

int GameList_rowCountFrom(int list_top) {
	return listLayoutFrom(list_top, true).rows;
}

int GameList_textX(void) {
	return stack && stack->count == 1 && !GameSwitcher_isList() ? NX_DP(NX_MENU_GUTTER_DP) : UI_listTextX();
}

int GameList_rowCount(void) {
	return listLayout().rows;
}

int GameList_rowCountAt(bool root) {
	return listLayoutAt(root).rows;
}

int GameList_lookTab(void) {
	if (!stack || !top)
		return -1;
	if (stack->count == 1)
		return MenuTabs_current();
	if (stack->count == 2) // a hidden tab's list pushed over a tab
		for (int id = MENU_TAB_CONSOLES; id < MENU_TAB_COUNT; id++)
			if (exactMatch(top->path, (char*)MenuTabs_path((MenuTabId)id)))
				return id;
	return -1;
}

int GameList_currentOrientation(void) {
	int tab = GameList_lookTab();
	if (tab >= 0) {
		int cat = MenuTabs_styleCategory((MenuTabId)tab);
		if (cat < 0)
			return MENU_ORIENT_HORIZONTAL; // Home draws itself
		return CFG_getMenuOrientEffective(cat);
	}
	return CFG_getGameListOrientEffective();
}

int GameList_currentStyle(void) {
	if (GameSwitcher_isList())
		return MENU_STYLE_LIST; // the Game Switcher's List over whatever the tab or game list draws
	int style;
	int tab = GameList_lookTab();
	if (tab >= 0) {
		int cat = MenuTabs_styleCategory((MenuTabId)tab);
		if (cat < 0)
			return MENU_STYLE_LIST;	   // Home draws itself
		style = CFG_getMenuStyle(cat); // List, Grid or Carousel (a stored Backdrop reads as Carousel)
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

// What the info band last put on LAYER_OVERLAY (the band's block and, with the page title hidden, the up arrow), and
// the layer's serial right after: while neither changed, the layer still holds it and nothing is redrawn (a redraw
// is a texture made and uploaded, a few ms a frame). nextui.c leaves the layer to this while a List frame keeps it
// (GameList_keepsOverlay); anyone else's draw or clear changes the serial, and the next frame redraws.
static struct {
	bool valid;
	unsigned serial, band_gen;
	int band_top;
	bool arrow;
	int arrow_x, arrow_y;
} overlay;

static void renderInfoBand(void) {
	// Home, Grid, Carousel and Backdrop have no info band (Grid's game info sits in the lit tile, the rows'
	// in the caption under the row)
	if (ContextMenu_isOpen() || (Home_active() && !Home_isList()) || GridView_active() || RowView_active()) {
		overlay.valid = false;
		return;
	}
	Directory* list = shownList(); // Home's List, or the top Directory
	int total = list->entries->count;
	bool up = total > 0 && list->start > 0;
	bool down = total > 0 && list->end < total;
	InfoBandLayout layout = listLayout(); // the rows' own geometry
	layout.arrow_x = GameList_textX();	  // the arrows sit at the rows' text start (§3.2)
	// On LAYER_OVERLAY, above the thumbnail layer (which would otherwise cover it on 4:3 screens), kept there while it
	// is unchanged (overlay above). While the tab-focus dim is layered, onto the screen instead: it dims with the rows
	// (contentdim.h), and the layer's copy goes.
	SDL_Surface* dst = ContentDim_layered() ? screen : NULL;
	// Layouts > Page title: Hide: the up arrow moves above the first row, as far above it as the band's down arrow sits
	// below the last (at the rows' text start), so the List has one at each end, mirrored; the band keeps the down one
	// (Home's List keeps both in the band, packed, whatever the title: its stats strip sits above the first row)
	bool arrow = false;
	int arrow_y = 0;
	if (!CFG_getPageTitle() && list == top) {
		if (up) {
			// the down arrow's centre under the last row's bottom (the rows may leave a few px above the band)
			int below = layout.text_top + layout.text_h / 2 - INFOBAND_ARROW_RAISE -
						(layout.list_top + layout.rows * layout.row_h);
			arrow = true;
			arrow_y = layout.list_top - below;
		}
		up = false;
	}
	// The band is always there (fade + arrows), with or without text, so rows never move.
	Entry* entry = total > 0 ? list->entries->items[list->selected] : NULL;
	MenuTabId tab = MenuTabs_current();
	bool at_root = stack->count == 1 && !GameSwitcher_isList(); // the Game Switcher's List reads as a game list
	unsigned gen;
	// game rows: game lists and the Home tab (same rule as GameList_render's game_art). Their play time and
	// achievements show only while the button hints are hidden (Layouts > Button hints) and the extra info is shown
	// (Layouts > Extra info); otherwise the band is the fade and arrows alone.
	if (entry && (!at_root || tab == MENU_TAB_HOME) && selectedIsGame(entry)) {
		GameInfo info;
		InfoSeg segs[3];
		int n = 0;
		if (!CFG_getButtonHints() && CFG_getExtraInfo() && GameInfo_get(entry->path, &info) && (info.has_time || info.has_ra))
			n = GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0,
								  info.has_time ? info.seconds : -1, info.has_ra ? info.unlocked : 0,
								  info.has_ra ? info.total : 0, info.has_ra ? info.next : NULL, true, segs);
		// the hint bar's row is empty (only its scrim): the info line moves down into it, centred, the arrows stay
		if (n > 0) {
			int bar_top = UI_buttonHintBarTop(screen->h);
			layout.info_top = bar_top + (screen->h - bar_top - layout.text_h) / 2;
		}
		gen = InfoBand_prepare(&layout, segs, n, up, down);
	} else {
		// Collections, Consoles, Tools and folders: the arrows alone (no "N games" line under a collection)
		gen = InfoBand_prepare(&layout, NULL, 0, up, down);
	}

	if (dst) {
		if (overlay.valid) // the layer's copy would sit lit over the dimmed rows
			GFX_clearLayers(LAYER_OVERLAY);
		overlay.valid = false;
		if (arrow)
			InfoBand_renderUpArrow(layout.arrow_x, arrow_y, LAYER_OVERLAY, dst);
		InfoBand_draw(LAYER_OVERLAY, dst);
		return;
	}
	if (overlay.valid && overlay.serial == PLAT_layerSerial(LAYER_OVERLAY) && overlay.band_gen == gen &&
		overlay.band_top == layout.band_top && overlay.arrow == arrow &&
		(!arrow || (overlay.arrow_x == layout.arrow_x && overlay.arrow_y == arrow_y)))
		return; // still on the layer as drawn
	GFX_clearLayers(LAYER_OVERLAY);
	if (arrow)
		InfoBand_renderUpArrow(layout.arrow_x, arrow_y, LAYER_OVERLAY, NULL);
	InfoBand_draw(LAYER_OVERLAY, NULL);
	overlay.valid = true;
	overlay.serial = PLAT_layerSerial(LAYER_OVERLAY);
	overlay.band_gen = gen;
	overlay.band_top = layout.band_top;
	overlay.arrow = arrow;
	overlay.arrow_x = layout.arrow_x;
	overlay.arrow_y = arrow_y;
}

bool GameList_listBodyClear(SDL_Rect* row) {
	if (row)
		*row = list_body_row;
	return list_body_clear;
}

bool GameList_keepsOverlay(void) {
	return !ContextMenu_isOpen() && (!Home_active() || Home_isList()) && !GridView_active() && !RowView_active();
}

// nextui.c's re-add after a slide, outside GameList_render's content layer. While the content is dimmed the band is
// already in the screen, dimmed with the rows (GameList_render drew it inside the layer): a lit copy on the GPU layer
// would sit over them, so nothing is drawn.
void GameList_renderInfoLayer(void) {
	if (ContentDim_dimmed())
		return;
	renderInfoBand();
}

// Button hints hidden, on a game list's List: the hint bar's 80% scrim alone, no hints (a game's info line sits in it)
static void renderHintScrim(SDL_Surface* screen) {
	static SDL_Surface* scrim = NULL;
	SDL_Surface* s = UI_getScrimAlpha(&scrim, screen->w, BAR_HEIGHT, 204);
	if (s) // black at one alpha: the row darkening, not SDL's per-pixel blend (~2 ms a frame on the Brick)
		UI_blitFade(s, NULL, screen, 0, UI_buttonHintBarTop(screen->h));
}

// The hint bar of a List or Grid screen (main-menu tabs and game lists): one copy of the pairs, the four-pair cap
// and the START drop rule.
static void renderHints(SDL_Surface* screen, IndicatorType show_setting) {
	// Button hints hidden (Layouts > Button hints): no bar at all, a volume or brightness change included
	if (!CFG_getButtonHints())
		return;
	int total = top->entries->count;
	Entry* entry = total > 0 ? top->entries->items[top->selected] : NULL;
	char* right_pairs[16] = {NULL};
	int p = 0;

	// the tab row has focus: exactly SELECT RECENT and A OPEN (A returns to the content); SELECT RECENT only with
	// Appearance > Show recent hint (SELECT still opens the Game Switcher without it)
	if (stack->count == 1 && MenuTabs_focused()) {
		UI_renderButtonHintBar(screen, CFG_getShowRecentHint() ? (char*[]){"SELECT", "RECENT", "A", "OPEN", NULL}
															   : (char*[]){"A", "OPEN", NULL});
		return;
	}

	// main menu tabs: the Game Switcher first
	if (stack->count == 1 && CFG_getShowRecentHint()) {
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
		bool netplay_hint = CFG_getShowNetplayHint() && entryNetplayCapable(entry); // Y still starts netplay when hidden
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

// No background at all (no bg.png or folder background): drops whatever is loaded, and forgets the resolve so a
// screen with a background reloads it when it comes back.
static void clearBackground(void) {
	folderBgPath[0] = '\0';
	bgLastType = -1;
	bgResolveValid = false;
	onBackgroundLoaded(NULL); // a no-op while nothing is loaded
}

// A Grid, Carousel or Backdrop screen draws on plain black (or the Backdrop picture, on `screen`): no bg.png,
// folder background or game art.
static void clearArtForGrid(SDL_Surface* screen, int lastScreen) {
	clearBackground();
	had_thumb = false;
	ox = screen->w;
	if (!list_art_cleared || lastScreen != SCREEN_GAMELIST) {
		startLoadThumb("", THUMB_COMPOSE_BG);
		list_art_cleared = true;
	}
	// no marquee on a Grid screen: a List's leftover state would keep ticking on the scroll-text layer
	if (GameList_scrollBusy())
		ScrollText_clear(&list_scroll);
}

// A console's logo (or Home's List's tool icon) dimmed on the right behind the rows, centred on them (band_y, band_h): a
// GPU sprite under the screen, decoded on the art loader's thread. True while it is still decoding.
static bool listLogoSprite(SDL_Surface* screen, const char* file, int band_y, int band_h) {
	bool pending = false;
	SDL_Surface* logo = MenuArt_peek(file, screen->w * 40 / 100, band_h * 60 / 100, &pending);
	SDL_Texture* lt = logo ? PLAT_textureForSurface(logo) : NULL;
	if (lt) // white at 16%: the PNG's off-white (0xE0) at 18.4%
		PLAT_spriteAddUnder(lt, NULL,
							&(SDL_Rect){screen->w - SCALE1(23) - logo->w, band_y + (band_h - logo->h) / 2, logo->w,
										logo->h},
							47, NULL);
	return pending;
}

// ROM_findListArt for the selected game of `list` (2-6 stats), remembered while the row, the list (its serial and the
// tab generation), the List art setting and list_art_epoch stay: the List redraws every frame of a glide or marquee.
static bool listArtFor(Directory* list, Entry* entry, char* out, size_t size) {
	static char last_path[MAX_PATH], last_art_path[MAX_PATH];
	static unsigned last_list, last_gen, last_epoch;
	static int last_art = -1;
	static bool last_fit;
	int art = CFG_getGameListArt();
	if (last_art != art || last_list != list->serial || last_gen != MenuTabs_generation() ||
		last_epoch != list_art_epoch || strcmp(last_path, entry->path) != 0) {
		last_fit = ROM_findListArt(entry->path, art, last_art_path, sizeof(last_art_path));
		last_art = art;
		last_list = list->serial;
		last_gen = MenuTabs_generation();
		last_epoch = list_art_epoch;
		snprintf(last_path, sizeof(last_path), "%s", entry->path);
	}
	snprintf(out, size, "%s", last_art_path);
	return last_fit;
}

void GameList_render(SDL_Surface* screen, int lastScreen,
					 IndicatorType show_setting, SDL_Surface* blackBG) {
	list_body_clear = false; // only a List render with all its rows on the GPU sets it
	list_body_row = (SDL_Rect){0, 0, 0, 0};
	list_hold_tex = NULL; // the frame's sprites start over (nextui.c's PLAT_spritesClear)
	if (Home_active() && !Home_isList()) {
		// the global bg.png, as on every main-menu tab, and no thumbnail (Home draws its own pictures)
		bool names = true;
		resolveAndLoadBackground(NULL, NULL, NULL, &names);
		had_thumb = false;
		ox = screen->w;
		if (!list_art_cleared || lastScreen != SCREEN_GAMELIST) {
			startLoadThumb("", THUMB_COMPOSE_BG);
			list_art_cleared = true;
		}
		Home_render(screen, lastScreen);
		if (lastScreen == SCREEN_OFF)
			GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255, 0,
									  CFG_getMenuTransitions() ? 200 : 20, LAYER_THUMBNAIL);
		return;
	}

	// the content below the tab row: one layer for the tab-focus dim (contentdim.h); the tab row, the hint bar and the
	// page background (the List's art and console logo, a Backdrop picture) are drawn outside it and stay lit
	int bar_h = BAR_HEIGHT;
	// Grid and rows: the body between the tab row and the hint bar (a List's reaches into the bar with its band)
	SDL_Rect content = {0, bar_h, screen->w, screen->h - 2 * bar_h};

	bool grid = GridView_active();
	if (grid || RowView_active()) {
		clearArtForGrid(screen, lastScreen);
		// a Grid or a row with items, outside a context menu, draws them as GPU sprites at the dim's opacity
		// (MenuTabs_contentAlpha) over a body it leaves as it was: the layer would change nothing, so it is skipped
		bool sprites = !ContextMenu_isOpen() && top->entries->count > 0;
		if (!sprites)
			ContentDim_begin(screen, content);
		if (grid)
			GridView_render(screen, lastScreen);
		else
			RowView_render(screen, lastScreen);
		if (!sprites)
			ContentDim_end(screen);
		renderHints(screen, show_setting); // over the black body (or the picture), as on List
		if (lastScreen == SCREEN_OFF)
			GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255, 0,
									  CFG_getMenuTransitions() ? 200 : 20, LAYER_THUMBNAIL);
		return;
	}

	// Home's List (Layouts > Home layout: List) is this List over Home's own rows: Continue (its tag before the name),
	// the pinned games, the pinned tools; its stats strip above them, its art and hints its own
	bool home_list = Home_isList();
	Directory* list = shownList();
	int total = list->entries->count;

	Entry* entry = total > 0 ? list->entries->items[list->selected] : NULL;
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

	// the root tab, or a hidden tab's list pushed over it (its own tab's backgrounds and art); the Game Switcher's List
	// is a game list's
	bool sw_list = GameSwitcher_isList();
	int look = sw_list ? -1 : GameList_lookTab();
	bool at_root = look >= 0;
	MenuTabId tab = at_root ? (MenuTabId)look : MenuTabs_current();
	// None (a game list's Layouts > List art, or the Consoles tab's Layouts > Controller): a plain list on black, no
	// game art, controller or background picture
	bool plain = at_root ? tab == MENU_TAB_CONSOLES && CFG_getConsoleArt() == CONSOLE_ART_NONE
						 : CFG_getGameListArt() == GAME_LIST_ART_NONE;
	// Home holds games; Consoles, Collections and Tools draw no game art (only a user's own
	// .media background, below; plain lists for Collections and Tools)
	bool game_art = (!at_root || tab == MENU_TAB_HOME) && !plain;

	// load folder background. Every main-menu tab shows the global bg.png (no
	// entry): pinned shortcuts and tools would otherwise clear it or show a
	// tool's own background. Game lists keep the per-entry background. The
	// Consoles and Collections tabs show the selected row's own .media
	// background instead when the card has one (tabBackground).
	if (plain)
		clearBackground();
	else
		resolveAndLoadBackground(at_root ? NULL : entry, rompath, at_root ? tabBackground(tab, entry) : NULL,
								 &list_show_entry_names);

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
			startLoadThumb("", THUMB_COMPOSE_BG);
			list_art_cleared = true;
		}
	} else if (total > 0) {
		list_art_cleared = false;
		// The game's screenshot as the background art behind the list (LIST-LAYOUT §6), or with Layouts > List art =
		// Mix, 3D box art, 2D box art or Wheel and that picture on the card, it fitted into a hard-edged box on the right; or
		// nothing. The screenshot paints behind the rows, so the title runs the full screen width (matching an
		// art-less row) and a long title may reach over the image's bright side; the box is solid, so the titles
		// stop short of it.
		// Home's List: Continue's resume frame first, a game without a screenshot its abstract picture, a tool none
		// (always behind the rows)
		char thumbpath[1024];
		bool fit = false;
		if (home_list)
			Home_listArtPath(thumbpath, MAX_PATH); // a key too long for the loader's MAX_PATH falls back
		else if (sw_list)
			GameSwitcher_listArtPath(thumbpath, MAX_PATH); // the resume frame, as Home's List's Continue
		else											   // else the screenshot: scraped, else the root .media picture
			fit = listArtFor(list, entry, thumbpath, sizeof(thumbpath));
		had_thumb = startLoadThumb(thumbpath, fit ? THUMB_COMPOSE_FIT : THUMB_COMPOSE_BG);
		// The consumers add SCALE1(BUTTON_MARGIN) back to ox, so this yields the art-less full-width row's available
		// width, or for the box, titles ending BUTTON_MARGIN before it as they end BUTTON_MARGIN before the
		// screen's edge.
		if (!had_thumb)
			ox = screen->w;
		else if (fit)
			ox = ArtBg_fitOriginX(screen->w, screen->h, SCALE1(BUTTON_MARGIN)) - SCALE1(BUTTON_MARGIN * 2);
		else
			ox = screen->w - SCALE1(BUTTON_MARGIN * 2);
	}

	// the rows' geometry: whole px rows of row_h (nxListFit), the band below them
	InfoBandLayout rows_layout = listLayout();
	int row_h = rows_layout.row_h;
	TTF_Font* list_font = listFont(atRoot()); // the rows' text (the main menu's per device), as listLayout()

	// Consoles tab: the selected console's controller (Layouts > Controller), else its logo, dimmed, on the right
	// behind the rows (the page background: it stays lit under the tab-focus dim, cropped only by the screen edges).
	// Background or None, the right side stays empty for every console.
	if (total > 0 && at_root && tab == MENU_TAB_CONSOLES && CFG_getMenuControllerArt()) {
		const char* slash = strrchr(entry->path, '/');
		const char* folder = slash ? slash + 1 : entry->path;
		const char* pad_id = Pad_idForFolder(folder);
		const char* logo_id = MenuLogo_idForFolder(folder);
		// Both are GPU sprites under the screen (PLAT_spriteAddUnder), seen through its clear body under the rows and
		// dimmed by the hint bar's scrim where they reach into it, and both decode on the art loader's thread: a step
		// never waits on a PNG, and a pad still decoding draws nothing (not the logo) until it lands
		// (GameList_artWaiting keeps the redraws coming; GameList_prefetchIdle asks for the neighbours' ahead).
		int pad_x, pad_y;
		SDL_Surface* pad = pad_id ? ControllerArt_list(pad_id, screen->w, screen->h, &pad_x, &pad_y) : NULL;
		bool pad_pending = pad_id && ControllerArt_listPending();
		list_art_waiting = pad_pending;
		SDL_Texture* tex = pad ? PLAT_textureForSurface(pad) : NULL;
		if (tex) // baked at its 35%, placed and rotated for this screen
			PLAT_spriteAddUnder(tex, NULL, &(SDL_Rect){pad_x, pad_y, pad->w, pad->h}, 255, NULL);
		else if (!pad_pending && logo_id) {
			char file[64];
			snprintf(file, sizeof(file), "menu_logo_%s.png", logo_id);
			list_art_waiting = listLogoSprite(screen, file, shownListTop(), rows_layout.rows * row_h);
		}
	} else if (home_list && Home_listToolIcon()) {
		// Home's List on a tool: its icon large and dim on the right, as a console's logo
		list_art_waiting = listLogoSprite(screen, Home_listToolIcon(), shownListTop(), rows_layout.rows * row_h);
	} else {
		list_art_waiting = false;
	}

	// after the controller art: its bottom reaches into the hint bar, and blitted over the bar's 80% scrim it showed
	// undimmed there, a hard edge under the band's fade (which ends at the bar's 80%)
	if (sw_list && CFG_getButtonHints())
		GameSwitcher_renderListHints(screen); // the switcher's own (B BACK, Y REMOVE, A RESUME), as on Full screen
	else if (sw_list)
		renderHintScrim(screen); // a game's info line sits in the bar's row, as a game list's
	else if (CFG_getButtonHints() && home_list)
		Home_renderListHints(screen); // Home's own (A RESUME / PLAY / the tool's name), as its Grid's and Carousel's
	else if (CFG_getButtonHints())
		renderHints(screen, show_setting);
	else if (stack->count > 1 || home_list) // a game list and Home's List: a game's info line sits in the bar's row
		renderHintScrim(screen);			// (over its art); the main menu's other tabs: no shade

	// the List's content: the rows and the band, down to the band's bottom (inside the hint bar)
	content.h = rows_layout.band_bottom - content.y;
	ContentDim_begin(screen, content);
	// layered, the content draws in software: no marquee (it presents its own lit frames on the scroll-text layer);
	// the selected row shows its truncated title until the dim lifts
	bool layered = ContentDim_layered();
	if (layered && GameList_scrollBusy())
		ScrollText_clear(&list_scroll);
	// The rows and the selection pill go to the GPU as sprites over the screen (their text from the text cache's
	// surfaces, the pill from a tinted atlas), so a glide that only moves the pill leaves the screen as it was and
	// little is uploaded. Not under the tab-focus dim (the content draws in software, to be dimmed) or a context menu
	// (it draws over the screen, under the sprites); the selected row's marquee stays in the screen (its own layer).
	bool row_sprites = !layered && !ContextMenu_isOpen();
	list_body_clear = row_sprites;
	if (home_list && Home_listHasStrip()) { // Home's stats strip, in the screen over the rows' top
		Home_renderListStrip(screen);
		list_body_clear = false;
	}
	// Home's List: Continue's row (always the first) wears its tag before the name, the name moved after it
	int tag_w = home_list ? Home_listTagWidth() : 0;

	// The info band (a GPU layer), drawn even for an empty list. Drawn before the rows: the marquee below
	// may present mid-render, and the band must already be back on its layer by then (no blink).
	renderInfoBand();
	if (total > 0) {
		int selected_row = list->selected - list->start;
		// Row text start (LIST-LAYOUT §1): the main menu's List rows on the tab row's 24 dp gutter, game lists on
		// the 14 dp list inset; the pill keeps its 14 dp (SCALE1(BUTTON_PADDING)) round the text either way.
		int row_text_x = GameList_textX();
		int row_pill_x = UI_listPillXFor(row_text_x);

		// Glide the selection pill to the selected row. Drawn here, decoupled from
		// the per-row loop, so it can sit between rows mid-slide; the rows below
		// then draw text only (no per-row pill background).
		bool pill_animating = false;
		int pill_y = -1;		  // current pill top-y; used by the row loop to tint the
								  // row the pill is over (see color-tracking note below)
		bool pill_sprite = false; // the pill went to the GPU (over the screen: it hides what the screen has under it)
		if (list_show_entry_names) {
			Entry* sel = list->entries->items[list->selected];
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
			int sel_pill_w =
				UI_calcListPillWidth(list_font, sel_text, sel_trunc, sel_avail, list->selected == 0 ? tag_w : 0);
			int target_y = shownListTop() + selected_row * row_h;
			// A page/list change (folder enter/exit, a tab switch: the `top` Directory's serial
			// changes) snaps the pill to the new selection instead of gliding in
			// from the previous list's row, which briefly flashed the old position.
			// Keyed on the serial, not the pointer: a freed Directory's address is often reused.
			bool list_changed = list->serial != list_pill_prev_top ||
								MenuTabs_generation() != list_pill_prev_gen;
			list_pill_prev_top = list->serial;
			list_pill_prev_gen = MenuTabs_generation();
			// On a wrap (last<->first), enter from the near edge in the direction of
			// travel instead of sliding the whole list: forward wrap (last->first)
			// drops in from just above the first row; backward wrap (first->last)
			// rises up from just below the last row. Done by seeding the glide's
			// start position (current_y) one row off the target edge.
			int cur_sel = list->selected;
			bool wrap_fwd = !list_changed && list_pill_prev_sel == total - 1 && cur_sel == 0 && total > 1;
			bool wrap_bwd = !list_changed && list_pill_prev_sel == 0 && cur_sel == total - 1 && total > 1;
			list_pill_prev_sel = cur_sel;
			if (target_y != list_pill_target || list_changed) {
				bool animate = list_pill_target >= 0 && !ContextMenu_isOpen() && !list_changed;
				if (wrap_fwd)
					list_pill_anim.current_y = target_y - row_h;
				else if (wrap_bwd)
					list_pill_anim.current_y = target_y + row_h;
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
			// The pill keeps its PILL_SIZE (the pill art's height: its end caps can't stretch), centred on the row like
			// the row's text, the difference split evenly (rounded). nxListFit lets a row shrink by up to 5% (Brick
			// main menu 89 px, Smart Pro S game lists 58 px), so the pill may overhang its row by a px; it is clipped to
			// the list area: never above the list top (the header or the tab row's gap) nor below the band's top.
			int pill_h = row_h < SCALE1(PILL_SIZE) * 19 / 20 ? row_h : SCALE1(PILL_SIZE); // a shorter pitch: the row's
			int pill_off = (int)floorf((row_h - pill_h) / 2.0f + 0.5f);
			int below = pill_h + pill_off - row_h; // the overhang under the last row, when the pill is taller
			int band_rows = list->end - list->start;
			int clip_bottom = shownListTop() + band_rows * row_h + (below > 0 ? below : 0);
			if (clip_bottom > rows_layout.band_top)
				clip_bottom = rows_layout.band_top;
			SDL_Rect band = {0, shownListTop(), screen->w, clip_bottom - shownListTop()};
			SDL_Rect prev_clip;
			SDL_GetClipRect(screen, &prev_clip);
			SDL_SetClipRect(screen, &band);
			SDL_Rect pill = {row_pill_x, pill_y + pill_off, list_pill_anim.current_w, pill_h};
			pill_sprite = row_sprites && UI_listItemBgSprites(&pill, &band);
			if (!pill_sprite) {
				UI_drawListItemBg(screen, &pill, true); // dimmed with the whole content while the tab row has focus
				list_body_clear = false;
			}
			SDL_SetClipRect(screen, &prev_clip);
		}

		// the selected row's marquee, drawn after the loop when the rows are sprites: its GPU scroll presents a frame
		// itself (ScrollText -> PLAT_GPU_Flip), and by then every row's sprite must be in (no rows missing below it)
		struct {
			bool on;
			char* text;
			int x, y, w;
			bool over;
		} marquee = {false};
		for (int i = list->start, j = 0; i < list->end; i++, j++) {
			Entry* entry = list->entries->items[i];
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

			int y = shownListTop() + j * row_h;

			if (list_show_entry_names) {
				char truncated[256];
				ListLayout item_layout = {
					.item_h = row_h,
					.max_width = available_width,
				};
				// selected=false: the selection background is the moving pill drawn
				// above, not a per-row static pill.
				int row_tag_w = i == 0 ? tag_w : 0; // Home's Continue tag, before the name
				ListItemPos pos = UI_renderListItemPill(
					screen, &item_layout, list_font,
					display_text, truncated, y, false, row_tag_w);
				pos.text_x = row_text_x; // the shared renderer's x is the 14 dp inset; the main menu sits on 24 dp
				int text_width = pos.pill_width - SCALE1(BUTTON_PADDING * 2) - row_tag_w;
				// the tag tints with the row (pill_over, below): drawn with it, a sprite when the rows are
				bool tag_over = row_tag_w > 0 && pill_y >= 0 && abs(pill_y - y) * 2 < row_h;
				if (row_tag_w > 0) {
					Home_listDrawTag(screen, pos.text_x, pos.text_y, TTF_FontHeight(list_font), tag_over, row_sprites);
					pos.text_x += row_tag_w;
				}
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
								 abs(pill_y - y) * 2 < row_h;
				bool use_marquee = row_is_selected && !pill_animating &&
								   !ContextMenu_isOpen() && !layered;
				// a selected title that fits has nothing to scroll: a sprite like the rest (its marquee state
				// dropped, so the idle tick has nothing stale to draw)
				if (use_marquee && row_sprites && !entry_unique &&
					UI_listItemTextFits(display_text, list_font, text_width, pill_over)) {
					use_marquee = false;
					if (list_scroll.text[0])
						ScrollText_clear(&list_scroll);
				}
				if (entry_unique && !use_marquee &&
					strncmp(entry_unique, entry_name, strlen(entry_name)) == 0) {
					// Duplicate-name row: name in the list colour, disambiguating
					// suffix ("(EMU)" or the filename remainder) dimmed, as in
					// upstream NextUI. The marquee row stays single-colour: the
					// scroll texture is one surface.
					if (!row_sprites ||
						!UI_listItemTextDimSuffixSprite(entry_name, entry_unique + strlen(entry_name), list_font,
														pos.text_x, pos.text_y, text_width, pill_over)) {
						list_body_clear = false; // this row is in the screen
						UI_renderListItemTextDimSuffix(screen, entry_name,
													   entry_unique + strlen(entry_name),
													   list_font, pos.text_x, pos.text_y,
													   text_width, pill_over);
					}
				} else if (use_marquee || !row_sprites ||
						   !UI_listItemTextSprite(display_text, list_font, pos.text_x, pos.text_y, text_width,
												  pill_over)) {
					// this row is in the screen: the selected one's marquee is the one row the body may hold
					if (use_marquee && row_sprites) {
						list_body_row = (SDL_Rect){0, pos.text_y, screen->w, TTF_FontHeight(list_font)};
						marquee.on = true, marquee.text = display_text, marquee.x = pos.text_x;
						marquee.y = pos.text_y, marquee.w = text_width, marquee.over = pill_over;
					} else {
						list_body_clear = false;
						UI_renderListItemText(screen,
											  use_marquee ? &list_scroll : NULL,
											  display_text, list_font,
											  pos.text_x, pos.text_y, text_width, pill_over);
					}
				}
			}
		}
		if (marquee.on) {
			UI_renderListItemText(screen, &list_scroll, marquee.text, list_font, marquee.x, marquee.y, marquee.w,
								  marquee.over);
			// held still (the pre-scroll delay, or nothing to scroll), the title it blitted into the screen sits under
			// the sprite pill: the same title as a sprite over it. Scrolling, the scroll layer (over the sprites) has it.
			// (A row clipped away entirely leaves the state as it was.)
			if (pill_sprite && strcmp(list_scroll.text, marquee.text) == 0)
				list_hold_tex =
					UI_listItemMarqueeHoldSprite(&list_scroll, list_font, marquee.x, marquee.y, marquee.over);
		}
	} else {
		UI_renderCenteredMessage(screen, "Empty folder");
		list_body_clear = false;
	}
	ContentDim_end(screen); // before the fade below: it presents frames itself

	// waking from sleep fades in from black, an empty list ("Empty folder") too, as Home, the Grid and the rows do
	if (lastScreen == SCREEN_OFF)
		GFX_animateSurfaceOpacity(blackBG, 0, 0, screen->w, screen->h, 255, 0, CFG_getMenuTransitions() ? 200 : 20,
								  LAYER_THUMBNAIL);
}
