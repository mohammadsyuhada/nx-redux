/*
 * extras.elf — the Xtras catalog: browse curated games/tools, install
 * on-device. Entries live in ./catalog/<id>/{meta.txt,install.sh,files/}.
 * Games install into "Roms/Xtra Games (EXTRAS)"; tools into Tools/ - except
 * where an entry's install.sh targets somewhere else entirely (psp installs
 * an emulator pak into Emus/), flagged per-entry via meta.txt's done_msg.
 */

#include <ctype.h>
#include <dirent.h>
#include <netdb.h>
#include <pthread.h> // background latest-release check thread
#include <signal.h>	 // sig_atomic_t for the thread's done flag
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // strcasecmp
#include <sys/stat.h> // mkdir (state dir), stat (latest-cache TTL)
#include <sys/statvfs.h>
#include <sys/wait.h> // WIFEXITED/WEXITSTATUS on run_install's pclose() status
#include <time.h>	  // latest-cache TTL comparison
#include <unistd.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "wget_fetch.h" // latest-release API queries (settings_updater's helper)
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_downloadprogress.h"
#include "ui_draw.h" // UI_renderCenteredButtons (draw_result_dialog)
#include "ui_list.h"
#include "ui_listview.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "ui_splash.h"
#include "utils.h" // escapeSingleQuotes

#define MAX_ENTRIES 32
#define META_STR 256

// Moved up from the detail-screen section (still defined there originally
// for wrap_text() below) so both the description block AND Task 13's result
// dialog can size against them - the dialog needs a forward declaration of
// wrap_text() ahead of its own first use, and that declaration's signature
// needs these two visible.
#define DESC_LINE_MAX 160
#define DESC_MAX_LINES 16

// Display name of the Roms subfolder installed GAME entries land in - the
// single source of truth for EXTRAS_ROMS_DIR/EXTRAS_DATA_DIR (env passed to
// install.sh/uninstall.sh below) and for locating each entry's version
// marker/launcher directly. Adjacent-string-literal use (e.g. "%s/Roms/"
// EXTRAS_ROMS_DIRNAME "/...") relies on this expanding to a bare string
// literal, not an expression. Display name renamed per user request
// 2026-08-08 (see spec doc). Task 13b renamed the platform tag itself too
// (ADDON -> EXTRAS: this macro, the env names above, Emus/<plat>/EXTRAS.pak,
// extras_games_launch.sh) as a full-coherence sweep alongside the pak/app
// rename (Add-ons.pak -> Xtras.pak, this app addons.c/addons.elf ->
// extras.c/extras.elf) - the on-disk marker filename (.nx_addon_version,
// see read_installed()/run_entry_script() below) is the one deliberate
// exception, left unrenamed so already-installed markers on a user's SD
// card keep reading as installed across the upgrade (no migration path for
// that one exists, unlike the Roms folder/Emus pak, which do get an
// explicit one-time `mv`/reinstall migration note).
#define EXTRAS_ROMS_DIRNAME "Xtra Games (EXTRAS)"

// Per-entry version state lives OUTSIDE the catalog (update-tracking spec,
// 2026-08-10): install.sh records the release tag it actually installed in
// the xtras state dir's <id>.version, and the background check thread below
// caches each repo's latest release tag in <id>.latest. "Update available"
// is simply installed != latest - the catalog itself no longer pins
// versions.
//
// The state dir is SHARED_USERDATA_PATH "/xtras", but SHARED_USERDATA_PATH
// is a runtime array (not a string literal) on desktop builds, so it's no
// longer adjacent-string-literal-concatenable -- resolved via snprintf
// instead (byte-identical to the device value; same technique as ratools'
// rat_badge_path()).
static void xtras_state_dir(char* buf, size_t n) {
	snprintf(buf, n, "%s/xtras", SHARED_USERDATA_PATH);
}
// Latest-release cache freshness window: within it the check thread skips
// the GitHub API entirely (rate limit is 60/hr/IP unauthenticated - ample,
// but re-hitting it on every app open is still pointless).
#define LATEST_CACHE_TTL_S 3600

typedef struct {
	char id[64]; // catalog folder name
	char name[META_STR];
	char category[16]; // "GAME" | "TOOL"
	char desc[1024];
	char repo[128];	 // GitHub owner/name whose releases drive the update check ("" = untracked)
	char latest[64]; // latest upstream release tag, from the <id>.latest cache ("" = unknown)
	int size_mb;
	char installed[64]; // "" = not installed, else the installed release tag
	char done_msg[128]; // optional: install-success subtitle override, for
						// entries whose payload doesn't land in the default
						// category folder (e.g. psp installs to Emus/, not
						// Tools/, so "Find it in Tools." would mislead)
} AddonEntry;

static AddonEntry entries[MAX_ENTRIES];
static int entry_count = 0;
static SDL_Surface* screen = NULL;
static char pak_dir[MAX_PATH];

// --- meta.txt: flat key=value, unknown keys ignored ---------------------
static void meta_set(AddonEntry* e, const char* key, const char* val) {
	if (!strcmp(key, "name"))
		snprintf(e->name, sizeof(e->name), "%s", val);
	else if (!strcmp(key, "category"))
		snprintf(e->category, sizeof(e->category), "%s", val);
	else if (!strcmp(key, "desc"))
		snprintf(e->desc, sizeof(e->desc), "%s", val);
	else if (!strcmp(key, "repo"))
		snprintf(e->repo, sizeof(e->repo), "%s", val);
	else if (!strcmp(key, "size_mb"))
		e->size_mb = atoi(val);
	else if (!strcmp(key, "done_msg"))
		snprintf(e->done_msg, sizeof(e->done_msg), "%s", val);
	// "version" (pre-update-tracking pin) and "asset" (consumed only by the
	// entry's own install.sh) fall through to the unknown-key ignore.
}

static bool meta_parse(const char* path, AddonEntry* e) {
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	char line[1200];
	while (fgets(line, sizeof(line), f)) {
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		char* eq = strchr(line, '=');
		if (!eq || eq == line)
			continue;
		*eq = '\0';
		meta_set(e, line, eq + 1);
	}
	fclose(f);
	return e->name[0] && e->category[0];
}

// First line of a small state file -> out (newline stripped). false (and an
// empty out) when the file is missing/unreadable/empty.
static bool read_line_file(const char* path, char* out, int out_size) {
	out[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(out, out_size, f) != NULL;
	fclose(f);
	if (!ok) {
		out[0] = '\0';
		return false;
	}
	char* nl = strchr(out, '\n');
	if (nl)
		*nl = '\0';
	return out[0] != '\0';
}

// Installed-version record, written by install.sh to the xtras state dir.
// Falls back to the legacy pre-update-tracking marker (.nx_addon_version inside
// the entry's data dir) and migrates the value forward so cards installed
// before the switch keep reading as installed; the legacy file itself is
// left for uninstall.sh to clear (and for an older Xtras.pak to read, if
// the system is ever downgraded).
static void read_installed(AddonEntry* e) {
	char state_dir[MAX_PATH];
	xtras_state_dir(state_dir, sizeof(state_dir));
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s.version", state_dir, e->id);
	if (read_line_file(path, e->installed, sizeof(e->installed)))
		return;

	char legacy[MAX_PATH];
	snprintf(legacy, sizeof(legacy),
			 "%s/Roms/" EXTRAS_ROMS_DIRNAME "/.data/%s/.nx_addon_version",
			 SDCARD_PATH, e->id);
	if (!read_line_file(legacy, e->installed, sizeof(e->installed)))
		return;

	// One-time forward migration; best-effort (a read-only card just retries
	// next launch). SHARED_USERDATA_PATH itself always exists on a booted card.
	mkdir(state_dir, 0755);
	FILE* out = fopen(path, "w");
	if (out) {
		fprintf(out, "%s\n", e->installed);
		fclose(out);
	}
}

// Latest upstream release tag, cached by the background check thread.
static void read_latest(AddonEntry* e) {
	char state_dir[MAX_PATH];
	xtras_state_dir(state_dir, sizeof(state_dir));
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s.latest", state_dir, e->id);
	read_line_file(path, e->latest, sizeof(e->latest));
}

// Tag comparison ignores a leading v/V: upstream tagging drifts between
// "v0.2.4" and "0.2.4" (the v is a convention some releases forget), and a
// spelling-only difference must not read as an update. The guard on [1]
// keeps a literal "v" tag intact.
static const char* tag_norm(const char* tag) {
	return (tag[0] == 'v' || tag[0] == 'V') && tag[1] ? tag + 1 : tag;
}

// The one predicate behind the "Update Available" group, the row badge and
// the detail page's UPDATE action: a tracked entry is updatable once both
// tags are known and differ (plain inequality - no semver parsing, matching
// the spec - but v-prefix-insensitive, see tag_norm).
static bool entry_update_available(const AddonEntry* e) {
	return e->installed[0] && e->latest[0] &&
		   strcmp(tag_norm(e->installed), tag_norm(e->latest)) != 0;
}

// --- background latest-release check --------------------------------------
// One sweep per app open (same worker-thread shape as settings_updater.c's
// download thread): for every repo-tracked entry whose <id>.latest cache is
// stale, fetch the repo's latest-release JSON and rewrite the cache with its
// tag_name. The thread only ever touches the cache FILES - never entries[]
// - and raises latest_check_done when the sweep ends; run_list's loop sees
// the flag, re-reads the caches into entries[] and rebuilds its rows. That
// file-based handoff is the whole synchronization story: no locks, no
// shared mutable state beyond one sig_atomic_t flag.
//
// Offline behaviour: a single getaddrinfo() gate skips the entire sweep
// (and each wget_fetch failure just leaves that entry's cache alone), so
// stale-or-missing info degrades to "no update shown" - it never blocks the
// UI or surfaces an error. The flag is raised on every exit path so nothing
// waits forever.
static volatile sig_atomic_t latest_check_done = 0;

// The thread's private snapshot of each tracked entry's id+repo, filled by
// main() BEFORE the thread starts. entries[] itself is off-limits to the
// thread: run_detail's post-install path calls catalog_load(), which
// rewrites entries[] while a slow sweep may still be running.
static struct {
	char id[64];
	char repo[128];
} check_items[MAX_ENTRIES];
static int check_count = 0;

static void* latest_check_thread(void* arg) {
	(void)arg;
	struct addrinfo* res = NULL;
	struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
	if (getaddrinfo("api.github.com", "443", &hints, &res) != 0 || !res) {
		latest_check_done = 1;
		return NULL;
	}
	freeaddrinfo(res);

	char state_dir[MAX_PATH];
	xtras_state_dir(state_dir, sizeof(state_dir));
	mkdir(state_dir, 0755); // best-effort; parent always exists on a booted card

	time_t now = time(NULL);
	for (int i = 0; i < check_count; i++) {
		char cache[MAX_PATH];
		snprintf(cache, sizeof(cache), "%s/%s.latest", state_dir, check_items[i].id);
		struct stat st;
		if (stat(cache, &st) == 0 && now - st.st_mtime < LATEST_CACHE_TTL_S)
			continue;

		char url[256];
		snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest",
				 check_items[i].repo);
		// 64 KB holds any realistic release JSON (asset lists included);
		// wget_fetch truncates beyond that, which at worst loses trailing
		// assets - tag_name sits at the top of the document.
		static uint8_t buf[64 * 1024];
		int n = wget_fetch(url, buf, sizeof(buf) - 1);
		if (n <= 0)
			continue;
		buf[n] = '\0';

		char* tag = strstr((char*)buf, "\"tag_name\"");
		if (!tag)
			continue;
		tag = strchr(tag + 10, '"'); // opening quote of the value
		if (!tag)
			continue;
		tag++;
		char* end = strchr(tag, '"');
		if (!end || end == tag || end - tag >= 64)
			continue;

		FILE* f = fopen(cache, "w");
		if (f) {
			fprintf(f, "%.*s\n", (int)(end - tag), tag);
			fclose(f);
		}
	}
	latest_check_done = 1;
	return NULL;
}

static int entry_cmp(const void* a, const void* b) {
	const AddonEntry *ea = a, *eb = b;
	int c = strcmp(ea->category, eb->category); // GAME before TOOL
	return c ? c : strcasecmp(ea->name, eb->name);
}

static void catalog_load(void) {
	entry_count = 0;
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s/catalog", pak_dir);
	DIR* d = opendir(dir);
	if (!d)
		return;
	struct dirent* de;
	while ((de = readdir(d)) && entry_count < MAX_ENTRIES) {
		if (de->d_name[0] == '.')
			continue;
		char meta[MAX_PATH];
		snprintf(meta, sizeof(meta), "%s/%s/meta.txt", dir, de->d_name);
		AddonEntry* e = &entries[entry_count];
		memset(e, 0, sizeof(*e));
		snprintf(e->id, sizeof(e->id), "%s", de->d_name);
		if (!meta_parse(meta, e))
			continue;
		read_installed(e);
		read_latest(e);
		entry_count++;
	}
	closedir(d);
	qsort(entries, entry_count, sizeof(entries[0]), entry_cmp);
}

// --- list screen --------------------------------------------------------

// Task 12: GAMES/TOOLS tabs (L1/R1) replace Task 8's inline per-category
// headers as the top-level split. TAB_CATEGORY ties each tab back to
// AddonEntry.category ("GAME"/"TOOL" - the only two values the catalog
// contract defines; see AddonEntry's own comment above), TAB_LABEL is what
// gets drawn on the tab pill itself.
typedef enum { TAB_GAMES,
			   TAB_TOOLS,
			   TAB_COUNT } AddonTab;
static const char* const TAB_CATEGORY[TAB_COUNT] = {"GAME", "TOOL"};
static const char* const TAB_LABEL[TAB_COUNT] = {"GAMES", "TOOLS"};

// One tab's rows, in display order: every not-installed entry first (own
// group, no header), then installed entries with a newer upstream release
// (the "Update Available" header), then the rest of the installed entries
// (the "Installed" header) - headers are drawn separately by the renderer
// when it reaches each group's start index. Rebuilt on tab switch, after
// any install/uninstall, and when the background latest-check completes, so
// it always reflects current installed/latest state - entries[] itself is
// never reordered by this, only entry_cmp() (via catalog_load's qsort,
// unchanged) still governs category+name order within each group.
typedef struct {
	int indices[MAX_ENTRIES]; // entries[] index for each row, in display order
	int count;				  // total rows in this tab
	int update_start;		  // indices[update_start..installed_start) is the "Update Available" group
	int installed_start;	  // indices[installed_start..count) is the "Installed" group; == count if none installed
} TabRows;

static void build_tab_rows(AddonTab tab, TabRows* rows) {
	rows->count = 0;
	for (int i = 0; i < entry_count; i++)
		if (!strcmp(entries[i].category, TAB_CATEGORY[tab]) && !entries[i].installed[0])
			rows->indices[rows->count++] = i;
	rows->update_start = rows->count;
	for (int i = 0; i < entry_count; i++)
		if (!strcmp(entries[i].category, TAB_CATEGORY[tab]) && entry_update_available(&entries[i]))
			rows->indices[rows->count++] = i;
	rows->installed_start = rows->count;
	for (int i = 0; i < entry_count; i++)
		if (!strcmp(entries[i].category, TAB_CATEGORY[tab]) && entries[i].installed[0] && !entry_update_available(&entries[i]))
			rows->indices[rows->count++] = i;
}

// iOS-style segmented-control GAMES/TOOLS switcher (Task 13c; supersedes the
// original separate-pill-buttons look), drawn directly under the menu bar
// and switched via L1/R1 in run_list() below. One rounded THEME_COLOR2
// "container" strip holds both segments, left-aligned and only as wide as
// the segments themselves (not full screen width); the active segment is a
// smaller THEME_COLOR1 pill inset within the strip, same "bright selection"
// family + text color (UI_getListTextColor) an entry row's own selected
// pill uses; the inactive segment is plain text in the dim gray the shared
// ListView's section-header rows use for "Installed", sitting directly on
// the strip with no background of its own.
//
// Both the strip and the active pill are drawn with UI_renderRoundedRectBg
// (a vector-drawn rounded rect, precise at any size) rather than the
// bitmap ASSET_WHITE_PILL an entry row's pill uses (via UI_drawListItemBg) -
// the active segment's inset height is well under a full item_h pill, and
// mixing a stretched bitmap pill inside a vector-drawn strip risks visibly
// mismatched corner curvature between the two; one consistent primitive for
// both keeps the container and the segment reading as one shape family.
//
// Local to extras.c - promote to common/ui if a second pak ever needs a
// segmented control. The reserved vertical band (and so the returned y) is
// deliberately kept at the ORIGINAL item_h-based height even though the
// strip itself only draws at ~0.75x that - same contract as the pre-13c
// pill-button version, so nothing below this call has to move.
static int render_tab_bar(SDL_Surface* screen, ListLayout* layout, AddonTab active_tab) {
	int y = layout->list_y;
	int strip_h = (layout->item_h * 3) / 4;
	int inset = SCALE1(3);
	int x = SCALE1(PADDING);

	// Pass 1: measure every segment's cell width up front, so the
	// container strip (drawn before any segment) can be sized to their sum
	// - "hugs the tabs", not full list width. UI_calcListPillWidth's own
	// truncated-text buffer must stay sized 256 regardless of TAB_LABEL's
	// short length (its internal strncpy(...,255) always writes the full
	// 255+NUL - this exact gotcha once caused a stack overflow in the old
	// bespoke entry-row renderer's badge-reservation path).
	int cell_w[TAB_COUNT];
	char cell_text[TAB_COUNT][256];
	int total_w = 0;
	for (int t = 0; t < (int)TAB_COUNT; t++) {
		cell_w[t] = UI_calcListPillWidth(font.small, TAB_LABEL[t], cell_text[t], layout->max_width, 0);
		total_w += cell_w[t];
	}

	UI_renderRoundedRectBg(screen, x, y, total_w, strip_h, THEME_COLOR2);

	int cx = x;
	for (int t = 0; t < (int)TAB_COUNT; t++) {
		bool selected = (t == (int)active_tab);
		if (selected) {
			UI_renderRoundedRectBg(screen, cx + inset, y + inset,
								   cell_w[t] - inset * 2, strip_h - inset * 2,
								   THEME_COLOR1);
		}

		// Active label: same bright "selected" text color an entry row's own
		// pill uses. Inactive label: the dim gray a section-header row uses
		// for "Installed" (COLOR_GRAY) - NOT UI_getListTextColor(false),
		// which returns THEME_COLOR4 (the ordinary unselected list-row
		// color, not this dim one) and made the inactive tab read as bright
		// as the active one.
		SDL_Color color = selected ? UI_getListTextColor(true) : COLOR_GRAY;
		SDL_Surface* surf = GFX_renderText(font.small, cell_text[t], color);
		if (surf) {
			int tx = cx + (cell_w[t] - surf->w) / 2;
			int ty = y + (strip_h - surf->h) / 2;
			SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){tx, ty, 0, 0});
			SDL_FreeSurface(surf);
		}
		cx += cell_w[t];
	}

	return y + layout->item_h + SCALE1(PADDING);
}

// Shared-ListView row model (Task 17): the widget's rows are the TabRows
// entries PLUS synthesized section-header rows, interleaved in display
// order:
//   [not-installed...] ["Update Available"] [update...] ["Installed"] [installed...]
// A header only exists when its group is non-empty (update_start ==
// installed_start when nothing is updatable; installed_start == count when
// nothing is current). The widget's `selected` and every ListViewAction
// index are WIDGET-row indices - convert with extras_widget_to_entry /
// extras_entry_to_widget at the A-press and every relocate-after-regroup
// site; headers themselves are never selectable (the widget skips them).
static int extras_h1(const TabRows* r) { // header before the update group?
	return r->update_start < r->installed_start ? 1 : 0;
}
static int extras_h2(const TabRows* r) { // header before the installed group?
	return r->installed_start < r->count ? 1 : 0;
}
static int extras_widget_count(const TabRows* r) {
	return r->count + extras_h1(r) + extras_h2(r);
}
static int extras_h1_pos(const TabRows* r) {
	return r->update_start;
}
static int extras_h2_pos(const TabRows* r) {
	return r->installed_start + extras_h1(r);
}
// widget row -> TabRows entry index (-1 for a header row)
static int extras_widget_to_entry(const TabRows* r, int wi) {
	if (extras_h1(r) && wi == extras_h1_pos(r))
		return -1;
	if (extras_h2(r) && wi == extras_h2_pos(r))
		return -1;
	int ei = wi;
	if (extras_h1(r) && wi > extras_h1_pos(r))
		ei--;
	if (extras_h2(r) && wi > extras_h2_pos(r))
		ei--;
	return ei;
}
// TabRows entry index -> widget row
static int extras_entry_to_widget(const TabRows* r, int ei) {
	int wi = ei;
	if (extras_h1(r) && ei >= r->update_start)
		wi++;
	if (extras_h2(r) && ei >= r->installed_start)
		wi++;
	return wi;
}

typedef struct {
	const TabRows* rows;
} ExtrasListCtx;
static ExtrasListCtx extras_ctx;
static ListView extras_view;

// No name suffixes or badges: the group an entry sits in ("Update
// Available"/"Installed" section headers) already says everything the old
// right-aligned "update" marker said (user feedback 2026-08-10 - the marker
// was redundant once the group existed).
static void extras_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	(void)selected;
	const TabRows* r = ((ExtrasListCtx*)ctx)->rows;
	if (extras_h1(r) && i == extras_h1_pos(r)) {
		out->is_header = true;
		out->label = "Update Available";
		return;
	}
	if (extras_h2(r) && i == extras_h2_pos(r)) {
		out->is_header = true;
		out->label = "Installed";
		return;
	}
	out->label = entries[r->indices[extras_widget_to_entry(r, i)]].name;
}

// Tab bar/menu bar chrome stays app-drawn (title=NULL keeps the widget out
// of the menu bar); the list band starts where the tab bar ends, passed via
// list_y_override. list_id = TAB_LABEL[active_tab] gives each tab a static
// identity, so a tab switch snaps the glide/marquee inside the widget - the
// old glide_last_tab hack is gone, as is the bespoke adjacent-step-across-
// header pre-target (the widget's internal pre-target handles it, including
// the index-distance-2 adjacency headers-as-rows creates, via its
// adjacent_selectable walk).
static void render_extras_list(SDL_Surface* screen, AddonTab active_tab, const TabRows* rows) {
	UI_renderMenuBar(screen, "Xtras");
	ListLayout layout = UI_calcListLayout(screen);
	// Function-scope arrays: hint_pairs must outlive the UI_listViewRender
	// call below, so a branch-scoped compound literal would be UB (see
	// ui_listview.h).
	char* hints_full[] = {"LEFT/RIGHT", "TAB", "B", "EXIT", "A", "DETAILS", NULL};
	char* hints_empty[] = {"LEFT/RIGHT", "TAB", "B", "EXIT", NULL};

	extras_ctx.rows = rows;
	extras_view.title = NULL;
	extras_view.font = font.large;
	extras_view.count = extras_widget_count(rows);
	extras_view.get_row = extras_get_row;
	extras_view.ctx = &extras_ctx;
	extras_view.list_id = TAB_LABEL[active_tab];
	extras_view.empty_title = "Nothing here yet";
	extras_view.list_y_override = render_tab_bar(screen, &layout, active_tab);
	// L1/R1 leads the bar (user preference 2026-08-08), then B, then A.
	// Always shown, even on an empty tab (v1 ships with TOOLS empty) -
	// otherwise the tab switcher's only remaining discoverability hint (see
	// run_list's own comment below) disappears exactly when a user most
	// needs it, on the one screen with nothing else to look at. A/DETAILS
	// only makes sense once there's a row to select.
	if (rows->count)
		extras_view.hint_pairs = hints_full;
	else
		extras_view.hint_pairs = hints_empty;
	UI_listViewRender(&extras_view, screen);
}

static int run_detail(AddonEntry* e); // Task 4; returns 1 if install state changed

// GFX_startFrame/PWR_update/UI_statusBarChanged and the shared ListView's
// UI_listViewHandleInput (rather than hand-rolled BTN_UP/BTN_DOWN math)
// match every other migrated pak-tool loop in this tree — PWR_update still
// has to run every frame so the power button and battery/clock status bar
// work here same as anywhere else, even though sleep/autosleep/power-off
// themselves are held off for the whole session.
//
// The widget owns the selection (extras_view.selected), and it is a
// WIDGET-row index scoped to the ACTIVE tab's rows-plus-headers - not a
// global entries[] index - so every read/write below goes through
// extras_widget_to_entry/extras_entry_to_widget. Switching tabs (L1/R1)
// resets it via UI_listViewReset (selected=0 may land on a header; the
// widget clamps to the first selectable row, i.e. the first entry - same
// as the old selected=0-of-entries) rather than trying to carry a position
// across two differently-ordered/differently-sized row lists; no other
// nx-redux pak has an established L1/R1 tab-switch convention to follow
// (checked gametime/scraper/musicplayer - their L1/R1 uses are alpha-jump
// and value-step, not tab switching), so plain "either shoulder button
// flips the tab" is used here - exactly two tabs makes wrap and clamp the
// same thing anyway. The switch moved shoulder buttons -> d-pad LEFT/RIGHT
// (user request 2026-08-18: first added alongside L1/R1, then L1/R1
// retired the same day) - the ListView's LEFT/RIGHT page-jump is opted out
// via no_lr_paging so one press never both flips the tab and pages the
// fresh list; both tabs are short and UP/DOWN wraps, so page-jump loses
// nothing. Task 13 added the tab hint (user feedback: the switch wasn't
// discoverable) - one combined button-pill hint ("LEFT/RIGHT" as the
// button label, GFX_blitButton renders any >1-char button string as a text
// pill rather than two separate circular buttons; same label the bootlogo/
// musicplayer settings bars use for d-pad hints), since
// UI_renderButtonHintBar caps at 4 total pairs and doesn't clip pixel
// overflow past dst->w on its own. The Task 13 width math measured the
// three-pair bar at ~577px on the Brick's 1024px bar / ~387px on tg5050's
// 1280px with the "L1/R1" label; "LEFT/RIGHT" adds ~5 glyphs' width -
// still comfortable margin on both.
static void run_list(void) {
	AddonTab active_tab = TAB_GAMES;
	TabRows rows;
	build_tab_rows(active_tab, &rows);
	// Wire the provider before the first HandleInput (which runs before the
	// first render this frame) so header-skip probes see real rows.
	extras_ctx.rows = &rows;
	extras_view.get_row = extras_get_row;
	extras_view.ctx = &extras_ctx;
	// D-pad LEFT/RIGHT is claimed for the tab switch below (user request
	// 2026-08-18: shoulder buttons and d-pad both flip the tab), so the
	// widget's LEFT/RIGHT page-jump must not also fire on the same press.
	// Both tabs are short lists; UP/DOWN wrap covers traversal.
	extras_view.no_lr_paging = true;
	UI_listViewReset(&extras_view, extras_widget_count(&rows), TAB_LABEL[active_tab]);
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT)) {
			active_tab = (active_tab == TAB_GAMES) ? TAB_TOOLS : TAB_GAMES;
			build_tab_rows(active_tab, &rows);
			UI_listViewReset(&extras_view, extras_widget_count(&rows), TAB_LABEL[active_tab]);
			dirty = true;
		}
		// Background latest-check finished: pull the fresh caches into
		// entries[] and regroup - rows may shuffle into/out of the "Update
		// Available" group, so keep the selection at the same (clamped)
		// entry position rather than letting it dangle past the
		// (unchanged-length, but regrouped) list. Reset-then-assign: the
		// widget count may change even when rows.count doesn't (headers
		// appear/disappear with their groups).
		if (latest_check_done) {
			latest_check_done = 0;
			int sel_entry = extras_widget_to_entry(&rows, extras_view.selected);
			for (int i = 0; i < entry_count; i++)
				read_latest(&entries[i]);
			build_tab_rows(active_tab, &rows);
			if (sel_entry < 0)
				sel_entry = 0;
			if (sel_entry >= rows.count)
				sel_entry = rows.count ? rows.count - 1 : 0;
			UI_listViewReset(&extras_view, extras_widget_count(&rows), TAB_LABEL[active_tab]);
			if (rows.count)
				extras_view.selected = extras_entry_to_widget(&rows, sel_entry);
			dirty = true;
		}
		ListViewAction act = UI_listViewHandleInput(&extras_view);
		if (act.type == LISTVIEW_BACK)
			break;
		if (act.type == LISTVIEW_ACTIVATED) {
			// act.index is a widget row; ACTIVATED is never a header row.
			AddonEntry* e = &entries[rows.indices[extras_widget_to_entry(&rows, act.index)]];
			// A long entry name's marquee band (LAYER_SCROLLTEXT) must not
			// bleed over the detail screen.
			GFX_clearLayers(LAYER_SCROLLTEXT);
			if (run_detail(e)) {
				// The entry just acted on may move from the not-installed
				// group to Installed (or back) once rows are rebuilt below
				// - re-locate it by id so the selection stays on the same
				// entry instead of landing on whatever unrelated row now
				// occupies the same index in the regrouped list.
				// Reset-then-assign, as everywhere a regroup moves rows.
				char id[64];
				snprintf(id, sizeof(id), "%s", e->id);
				catalog_load();
				build_tab_rows(active_tab, &rows);
				int found = 0;
				for (int i = 0; i < rows.count; i++) {
					if (!strcmp(entries[rows.indices[i]].id, id)) {
						found = i;
						break;
					}
				}
				UI_listViewReset(&extras_view, extras_widget_count(&rows), TAB_LABEL[active_tab]);
				if (rows.count)
					extras_view.selected = extras_entry_to_widget(&rows, found);
			}
			dirty = true;
		}
		if (UI_statusBarChanged())
			dirty = true;
		if (UI_listViewBusy(&extras_view))
			dirty = true;
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			GFX_clear(screen);
			render_extras_list(screen, active_tab, &rows);
			GFX_flip(screen);
			dirty = false;
		} else {
			UI_listViewTickIdle(&extras_view);
			GFX_sync();
		}
	}
}

// --- pre-flight -----------------------------------------------------------
// Checked right before A on the detail screen: install.sh assumes both a
// working internet connection and enough free space, and fails ugly (partway
// through a download, or mid-extract) when either is missing. Catch the
// common cases up front with a friendly message instead.
static const char* preflight(AddonEntry* e) {
	struct addrinfo* res = NULL;
	struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
	if (getaddrinfo("github.com", "443", &hints, &res) != 0 || !res)
		return "No network - connect WiFi first";
	freeaddrinfo(res);

	struct statvfs vfs;
	if (statvfs(SDCARD_PATH, &vfs) == 0) {
		long free_mb = (long)((unsigned long long)vfs.f_bavail * vfs.f_frsize / (1024 * 1024));
		if (free_mb < e->size_mb * 2)
			return "Not enough free space on SD card";
	}
	return NULL;
}

// Terminal message: same shape as emu-options/options.c's file-local
// show_message (GFX_clear + UI_renderCenteredMessage + GFX_flip + a fixed
// hold via SDL_Delay) - copied rather than shared since that one is static
// to its own TU too.
#define EXTRAS_MESSAGE_MS 2000

// --- entry-script runner: popen install.sh/uninstall.sh, stream lines -----
// Task 11: the in-flight screen is the shared UIDownloadProgress bar
// (ui_downloadprogress.h - same component portmaster.c/settings_updater.c
// use for their own downloads) instead of a scrolling log tail. Task 13
// dropped the terminal log-tail-list screen too (see draw_result_dialog
// below) - the tail buffer shrinks from a TAIL_LINES ring to a single
// `error_line` slot (display text only, hints stripped): the LAST line seen
// whose text starts with "ERROR: " (install.sh/uninstall.sh's `fail()`
// convention - see its own comment there).
//
// Round-1 review, two findings on the naive "just keep the last line seen"
// version of this:
// - Critical: `fail()` can print a line AFTER its own "ERROR: ..." (e.g.
//   "install broken, launcher removed - re-run install" once TARGET_DIRTY +
//   a registered launcher - scripts/tests/test-extras-gen1recomp-install.sh's
//   scenario 4 exercises exactly this), so "the last line" is sometimes that
//   retry notice, not the actual reason. Fixed by matching the "ERROR: "
//   PREFIX specifically (whichever line last had it), independent of
//   whatever prints after.
// - Important: a script killed by signal, or one that exits non-zero
//   without ever printing an "ERROR: " line, has no such line to show at
//   all - falling back to "whatever the last line happened to be" would
//   then show an ordinary progress/status line (e.g. "Extracting game...")
//   as if it explained the failure, which it doesn't. So there is
//   deliberately NO "last line of any kind" fallback buffer here anymore -
//   see draw_result_dialog's caller below, which uses a fixed generic
//   sentence instead whenever `error_line` is empty.
// Full fidelity (every line, hints included) still lands in
// $LOGS_PATH/Xtras.txt regardless, via the unconditional stderr tee below
// - this buffer is purely for the on-screen failure subtitle.
#define TAIL_LINE_MAX 96
// Convention shared with install.sh/uninstall.sh's fail() helper (their own
// header comment): a failure's real explanation is always its own line
// starting with exactly this prefix, even if fail() goes on to print more
// after it.
#define ERROR_LINE_PREFIX "ERROR: "

// wrap_text() is defined later, in the detail-screen section below - this
// forward declaration (same pattern as run_detail's own above) lets
// draw_result_dialog() reuse it here rather than duplicating a third
// pixel-measured wrap implementation next to this one and
// ui_confirmdialog.c's file-local confirm_wrap_lines(). DESC_LINE_MAX/
// DESC_MAX_LINES (this signature's array dimensions) now live in the top
// macros section for the same reason.
static int wrap_text(TTF_Font* f, const char* text, int max_width,
					 char lines[DESC_MAX_LINES][DESC_LINE_MAX], int max_lines);

#define RESULT_SUB_MAX_LINES 3

// Terminal result dialog (Task 13, user feedback round 3): replaces the old
// log-tail list screen with the same centered-title/subtitle/hint-buttons
// visual pattern ui_confirmdialog.h's UI_renderConfirmDialog uses - but not
// that component itself, since it hardcodes a CANCEL/CONFIRM button pair
// with no single-button "OK" mode, and adding one for this single caller
// wasn't worth a shared-component change. `subtitle` is centered, word-
// wrapped (via wrap_text, font.small - same treatment
// UI_renderConfirmDialog gives its own subtitle, just reusing this file's
// own wrapper instead of that file's private one); `detail` (optional -
// NULL/empty to omit) renders as a second, dimmer, smaller single-line
// block below it - used for the "full log is at ..." pointer on failure.
static void draw_result_dialog(const char* title, const char* subtitle, const char* detail) {
	GFX_clear(screen);
	GFX_clearLayers(LAYER_SCROLLTEXT);

	int padding_x = SCALE1(PADDING * 4);
	int content_w = screen->w - padding_x * 2;

	char sub_lines[RESULT_SUB_MAX_LINES][DESC_LINE_MAX];
	int sub_line_count = 0;
	int sub_line_h = TTF_FontHeight(font.small);
	if (subtitle && subtitle[0])
		sub_line_count = wrap_text(font.small, subtitle, content_w, sub_lines, RESULT_SUB_MAX_LINES);

	int detail_h = (detail && detail[0]) ? TTF_FontHeight(font.tiny) : 0;
	char detail_truncated[256];
	if (detail_h) {
		GFX_truncateText(font.tiny, detail, detail_truncated, content_w, 0);
		detail = detail_truncated;
	}

	int title_h = TTF_FontHeight(font.large);
	int btn_sz = SCALE1(BUTTON_SIZE);

	int total_h = title_h;
	if (sub_line_count)
		total_h += SCALE1(BUTTON_MARGIN) + sub_line_count * sub_line_h;
	if (detail_h)
		total_h += SCALE1(BUTTON_MARGIN) + detail_h;
	total_h += SCALE1(BUTTON_MARGIN) + btn_sz;

	int y = (screen->h - total_h) / 2;

	SDL_Rect title_rect = {padding_x, y, content_w, title_h};
	GFX_blitMessage(font.large, (char*)title, screen, &title_rect);
	y += title_h;

	if (sub_line_count) {
		y += SCALE1(BUTTON_MARGIN);
		for (int i = 0; i < sub_line_count; i++) {
			SDL_Surface* s = GFX_renderText(font.small, sub_lines[i], COLOR_WHITE);
			if (s) {
				SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){(screen->w - s->w) / 2, y});
				SDL_FreeSurface(s);
			}
			y += sub_line_h;
		}
	}

	if (detail_h) {
		y += SCALE1(BUTTON_MARGIN);
		SDL_Surface* s = GFX_renderText(font.tiny, detail, COLOR_GRAY);
		if (s) {
			SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){(screen->w - s->w) / 2, y});
			SDL_FreeSurface(s);
		}
		y += detail_h;
	}

	y += SCALE1(BUTTON_MARGIN);
	UI_renderCenteredButtons(screen, y, (char*[]){"B", "OK", NULL});

	GFX_flip(screen);
}

// In-flight progress screen: menu bar carries the entry's name (continuity
// with the detail page this was launched from), UIDownloadProgress carries
// the moving parts. show_bar is always true here - install.sh/uninstall.sh
// always emit at least one @NN hint (see parse_hint below) before this is
// first called, per their own contract comment.
static void draw_progress_screen(const char* menu_title, const char* status,
								 const char* detail, int progress) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, menu_title);
	UI_renderDownloadProgress(screen, &(UIDownloadProgress){
										  .status = status,
										  .detail = detail,
										  .progress = progress,
										  .show_bar = true,
									  });
	GFX_flip(screen);
}

// Catalog progress-hint convention (Task 11): a line of the form "@NN status
// text" - NN a bare decimal (no sign), then a space, then non-empty free
// text - sets the progress bar to NN% (clamped to 100, in case a script
// ever sends more) and shows "status text" as the big centered status line.
// Any other line is ordinary human-readable output and is shown as the
// smaller detail line below the bar instead (see the caller's read loop).
// Scripts emit hints as their OWN line, never glued onto an existing
// message line, so existing message strings (checksum failures, "kept",
// etc.) stay byte-for-byte intact for anything that greps them - see
// install.sh's header for the full contract text.
//
// Hardening (round-1 review): a line is only ever treated as hint SYNTAX -
// and so never shown to the user verbatim - once it's unambiguously
// digit-leading (line[1] is a decimal digit; this also rejects a '-' right
// after '@', so a negative number is never mistaken for a hint - it just
// reads as ordinary text instead, same as any other line that merely
// starts with '@', e.g. an email address or an "@channel"-style mention).
// Once that gate is passed, strtol() only ever walks digits it was already
// promised were there, so it can't wander onto leading whitespace the way
// it would if called on arbitrary input. A digit-leading line that ISN'T a
// well-formed hint (no space, or a space with nothing after it) is still
// malformed hint syntax, not ordinary text - `*stripped` is set to
// whatever remains after the "@<digits>" token (possibly empty) so the
// caller can route THAT to the detail line instead of the raw "@NN..."
// text; the caller drops the line entirely when `*stripped` comes back
// empty.
//
// Returns true (with *pct/*status_text set) for a well-formed hint. Returns
// false otherwise; *stripped is then NULL for ordinary (non-digit-leading)
// text - use `line` itself unmodified - or a pointer to the hint-token-
// stripped remainder for malformed digit-leading syntax.
static bool parse_hint(const char* line, int* pct, const char** status_text,
					   const char** stripped) {
	*stripped = NULL;
	if (line[0] != '@' || !isdigit((unsigned char)line[1]))
		return false;

	char* end;
	long n = strtol(line + 1, &end, 10);
	if (*end == ' ' && end[1] != '\0') {
		*pct = (n > 100) ? 100 : (int)n;
		*status_text = end + 1;
		return true;
	}
	*stripped = (*end == ' ') ? end + 1 : end;
	return false;
}

// install.sh/uninstall.sh's shared env contract (Task 1, extended Task 9):
// PLATFORM and SDCARD_PATH are already C-level build/platform constants
// (defines.h / -DPLATFORM), so they're used directly. LOGS_PATH has no such
// constant - it only exists as a shell var exported by MinUI.pak's
// launch.sh and inherited down the process tree that eventually execs
// extras.elf - so it's read at runtime via getenv(), with a fallback built
// the same way launch.sh derives it (USERDATA_PATH/logs) in case extras.elf
// is ever launched with a bare environment.
//
// `title` is a verb stem ("Install"/"Uninstall") shared by both callers
// below; every on-screen string is built from it by plain concatenation -
// "Install"+"ing..." == "Installing..." (progress screen), "Install"+"ed"
// == "Installed" (Task 13 result-dialog title on success), "Install"+"
// failed" == "Install failed" (result-dialog title on failure) - so this
// one function produces every wording variant for either script, without
// duplicating the popen/streaming/result-dialog logic itself.
//
// The fgets() loop below blocks on the child's stdout with no watchdog of
// its own; its boundedness is entirely the script's own responsibility per
// its contract comment - every network command it runs must carry a
// timeout, or a stalled/half-open connection hangs this whole UI (no
// redraw, no power button) for as long as the connection stays open.
// uninstall.sh's contract requires no network access at all, so in practice
// this only bites install.sh.
static int run_entry_script(AddonEntry* e, const char* script_name, const char* title) {
	const char* logs_path = getenv("LOGS_PATH");
	// USERDATA_PATH is a runtime array (not a string literal) on desktop
	// builds, so appending "/logs" via adjacent string-literal concatenation
	// no longer compiles -- resolved into a local buffer via snprintf
	// instead (byte-identical to the device value).
	char logs_path_buf[MAX_PATH];
	if (!logs_path) {
		snprintf(logs_path_buf, sizeof(logs_path_buf), "%s/logs", USERDATA_PATH);
		logs_path = logs_path_buf;
	}

	char state_dir[MAX_PATH];
	xtras_state_dir(state_dir, sizeof(state_dir));

	// Escape the catalog id before it goes into the single-quoted CATALOG_DIR
	// and sh-path arguments below (worst case each quote expands to 4 chars).
	char id_esc[sizeof(e->id) * 4];
	snprintf(id_esc, sizeof(id_esc), "%s", e->id);
	escapeSingleQuotes(id_esc, sizeof(id_esc));

	// EXTRAS_DATA_DIR is the per-entry payload root for native standalone games
	// (e.g. gen1recomp's LOVE engine + saves live in .data/<id>/). Named .data,
	// not .ports: native entries own their whole runtime and don't touch
	// PortMaster. A PortMaster-dependent extra is expected to install into the
	// normal Roms/Ports (PORTS) tree from its own install.sh (as the psp TOOL
	// installs to Emus/), so "Xtra Games (EXTRAS)" stays native-only.
	char cmd[MAX_PATH * 4];
	snprintf(cmd, sizeof(cmd),
			 "PLATFORM='%s' SDCARD_PATH='%s' LOGS_PATH='%s' "
			 "EXTRAS_ROMS_DIR='%s/Roms/" EXTRAS_ROMS_DIRNAME "' "
			 "EXTRAS_DATA_DIR='%s/Roms/" EXTRAS_ROMS_DIRNAME "/.data' "
			 "XTRAS_STATE_DIR='%s' "
			 "CATALOG_DIR='%s/catalog/%s' "
			 "sh '%s/catalog/%s/%s' 2>&1",
			 PLATFORM, SDCARD_PATH, logs_path,
			 SDCARD_PATH, SDCARD_PATH, state_dir, pak_dir, id_esc, pak_dir, id_esc, script_name);

	FILE* p = popen(cmd, "r");
	if (!p)
		return -1;

	char progress_title[32];
	snprintf(progress_title, sizeof(progress_title), "%sing...", title);

	// status/detail/progress: fed by parse_hint() below, this is the exact
	// state UI_renderDownloadProgress needs each frame. status starts as
	// progress_title so the bar reads something sane for the brief instant
	// before the script's first @NN hint line arrives.
	char status[64];
	snprintf(status, sizeof(status), "%s", progress_title);
	char detail[TAIL_LINE_MAX] = "";
	int progress = 0;

	char error_line[TAIL_LINE_MAX] = "";
	char buf[512];
	while (fgets(buf, sizeof(buf), p)) {
		char* nl = strchr(buf, '\n');
		if (nl)
			*nl = '\0';
		// The child's stdout is only this popen() pipe (consumed line-by-line
		// below, never touches a file) - the pak's launch.sh redirects
		// extras.elf's own stderr into $LOGS_PATH/Xtras.txt, so mirroring
		// every line there is what actually gives the full run a persistent
		// record on disk, not just the on-screen tail. The RAW line (hint
		// prefix included, if any) is tee'd unchanged - the on-disk log
		// keeps full fidelity even though the on-screen UI below strips it.
		fprintf(stderr, "%s\n", buf);
		if (!buf[0])
			continue;

		int pct;
		const char *status_text, *stripped;
		const char* display; // hint-stripped text, for error_line below
		if (parse_hint(buf, &pct, &status_text, &stripped)) {
			progress = pct;
			snprintf(status, sizeof(status), "%s", status_text);
			display = status_text;
		} else if (stripped) {
			// Malformed but digit-leading "@..." syntax: never show the raw
			// hint token - route whatever's left after stripping it (often
			// nothing) to the detail line like ordinary output, or drop the
			// line entirely if nothing's left.
			if (!stripped[0])
				continue;
			snprintf(detail, sizeof(detail), "%s", stripped);
			display = stripped;
		} else {
			snprintf(detail, sizeof(detail), "%s", buf);
			display = buf;
		}

		// The LAST "ERROR: " line wins if the script ever prints more than
		// one (fail() calls are meant to be terminal, but nothing stops a
		// script from calling it more than once on unrelated paths) - see
		// TAIL_LINE_MAX's own comment for why this can't just be "the last
		// line of any kind".
		if (!strncmp(display, ERROR_LINE_PREFIX, sizeof(ERROR_LINE_PREFIX) - 1))
			snprintf(error_line, sizeof(error_line), "%s", display);
		// Non-interactive: B is disabled while the script runs (nothing reads
		// PAD state here), but PAD_poll() still runs every rendered frame so
		// the input core doesn't go stale across a long popen read - same as
		// every other blocking flow in this tree.
		PAD_poll();
		draw_progress_screen(e->name, status, detail, progress);
	}
	int wstatus = pclose(p);
	int code = (wstatus >= 0 && WIFEXITED(wstatus)) ? WEXITSTATUS(wstatus) : -1;

	// Task 13: single dismiss button now says the whole story (title +
	// subtitle), so "- press B"/" FAILED - press B" no longer belong baked
	// into the title text itself - draw_result_dialog's own "B OK" hint
	// says that part.
	if (code == 0) {
		char ok_title[24];
		snprintf(ok_title, sizeof(ok_title), "%sed", title); // "Installed"/"Uninstalled"
		char subtitle[128];
		if (!strcmp(title, "Install") && e->done_msg[0])
			// Entry-provided override (meta.txt done_msg) for payloads that
			// don't land in the category's default folder - see the struct
			// field's own comment.
			snprintf(subtitle, sizeof(subtitle), "%s", e->done_msg);
		else if (!strcmp(title, "Install"))
			// GAME entries land in EXTRAS_ROMS_DIRNAME ("Xtra Games (EXTRAS)",
			// shown to the user without the "(EXTRAS)" tag suffix - see that
			// macro's own comment); TOOL entries land in Tools/ instead
			// (this file's own header comment). Both are the friendly,
			// user-facing folder name, not a literal path.
			snprintf(subtitle, sizeof(subtitle), "Find it in %s.",
					 !strcmp(e->category, "TOOL") ? "Tools" : "Xtra Games");
		else
			snprintf(subtitle, sizeof(subtitle), "Saves and ROMs kept.");
		draw_result_dialog(ok_title, subtitle, NULL);
	} else {
		char fail_title[24];
		snprintf(fail_title, sizeof(fail_title), "%s failed", title); // "Install failed"/"Uninstall failed"
		char log_detail[128];
		snprintf(log_detail, sizeof(log_detail), "Full log: %s/Xtras.txt", logs_path);
		// error_line (the last "ERROR: "-prefixed line, tracked above) is
		// the real reason whenever the script's own fail()-style convention
		// fired, no matter what fail() went on to print afterward. If no
		// such line was ever seen - the process died to a signal
		// (WIFEXITED false above) or exited non-zero without explaining
		// itself - a generic sentence is shown instead of guessing from
		// whatever ordinary output happened to arrive last (round-1 review
		// Important: that would misrepresent a progress line as the
		// reason). Either way the dim log-path line still points at the
		// full record, so nothing is actually lost, just not guessed at.
		const char* reason;
		if (error_line[0])
			reason = error_line;
		else if (!strcmp(title, "Install"))
			reason = "The installer stopped unexpectedly.";
		else
			reason = "The uninstaller stopped unexpectedly.";
		draw_result_dialog(fail_title, reason, log_detail);
	}
	while (1) {
		PAD_poll();
		if (PAD_justPressed(BTN_B))
			break;
		GFX_sync();
	}
	PAD_poll();
	PAD_reset(); // swallow the B before the caller's back-check
	return code;
}

static int run_install(AddonEntry* e) {
	return run_entry_script(e, "install.sh", "Install");
}

// Uninstall fallback for an entry that ships no uninstall.sh (catalog
// contract: "removes only the launcher + marker (safest possible)").
// Never touches anything under EXTRAS_DATA_DIR/<id>/ beyond the marker
// itself, so any user data such an entry might have written there is left
// completely alone. The installed launcher name(s) are derived from
// install.sh's own convention rather than hardcoded here: install.sh cp's
// each file under catalog/<id>/files/ into EXTRAS_ROMS_DIR under the SAME
// basename (see gen1recomp/install.sh's LAST STEP), so listing that
// directory recovers exactly what got registered without this app needing
// its own separate record of it. Best-effort/idempotent like uninstall.sh
// itself: remove() on an already-missing path is silently ignored, and this
// always reports success (nothing here can meaningfully fail in a way the
// user needs to know about).
static int uninstall_generic(AddonEntry* e) {
	char files_dir[MAX_PATH];
	snprintf(files_dir, sizeof(files_dir), "%s/catalog/%s/files", pak_dir, e->id);
	DIR* d = opendir(files_dir);
	if (d) {
		struct dirent* de;
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.')
				continue;
			char launcher[MAX_PATH];
			snprintf(launcher, sizeof(launcher), "%s/Roms/" EXTRAS_ROMS_DIRNAME "/%s",
					 SDCARD_PATH, de->d_name);
			remove(launcher);
		}
		closedir(d);
	}

	char marker[MAX_PATH];
	snprintf(marker, sizeof(marker),
			 "%s/Roms/" EXTRAS_ROMS_DIRNAME "/.data/%s/.nx_addon_version",
			 SDCARD_PATH, e->id);
	remove(marker);

	// The update-tracking installed-version record (the legacy marker above
	// is its pre-migration location; both must go or the entry reads as
	// still installed).
	char state_dir[MAX_PATH];
	xtras_state_dir(state_dir, sizeof(state_dir));
	snprintf(marker, sizeof(marker), "%s/%s.version", state_dir, e->id);
	remove(marker);

	UI_showMessage(screen, "Saves and ROMs kept.", EXTRAS_MESSAGE_MS);
	return 0;
}

// X on the detail screen (Task 9): run the entry's own uninstall.sh if it
// ships one (streamed the same way install.sh is), else fall back to the
// generic launcher+marker removal above.
static int run_uninstall(AddonEntry* e) {
	char script[MAX_PATH];
	snprintf(script, sizeof(script), "%s/catalog/%s/uninstall.sh", pak_dir, e->id);
	if (access(script, F_OK) == 0)
		return run_entry_script(e, "uninstall.sh", "Uninstall");
	return uninstall_generic(e);
}

// X on an installed entry: confirm before uninstalling. UI_confirmModal's
// trailing PAD_poll()+PAD_reset() (reset_pad=true) keeps the confirming A or
// cancelling B press from leaking into run_detail's own PAD_justPressed
// checks next frame - same precedent/shape as settings.c's run_confirm_dialog
// (a different translation unit, so the identical name doesn't clash).
static bool run_confirm_dialog(const char* title, const char* subtitle) {
	return UI_confirmModal(screen, title, subtitle, NULL, false, true);
}

// --- detail screen ----------------------------------------------------

// Pixel-measured word-wrap for the description block below: each candidate
// line is measured with the real TTF font (TTF_SizeUTF8), never a fixed
// character count, because the two device widths (1024 tg5040 / 1280
// tg5050) fit a different number of glyphs at the same point size. A word
// wider than max_width all by itself is hard-broken character by character
// instead of being left to overflow the screen edge.
//
// Not api.c's GFX_wrapText/GFX_blitWrappedText: neither hard-breaks an
// over-width word, and blitWrappedText only center-aligns - this page is
// left-aligned under the title/metadata lines above it.
//
// Returns the number of lines written to `lines` (0 for an empty desc). If
// there's more text than max_lines holds, the last line is shortened and
// "..." appended so the truncation stays legible and still fits max_width.
static int wrap_text(TTF_Font* f, const char* text, int max_width,
					 char lines[DESC_MAX_LINES][DESC_LINE_MAX], int max_lines) {
	int n = 0;
	const char* p = text;
	while (*p == ' ')
		p++;

	while (*p && n < max_lines) {
		char cur[DESC_LINE_MAX] = "";
		bool has_word = false;
		bool hard_broke = false;

		while (*p) {
			const char* word_start = p;
			const char* q = p;
			while (*q && *q != ' ')
				q++;
			int word_len = (int)(q - word_start);
			if (word_len >= DESC_LINE_MAX)
				word_len = DESC_LINE_MAX - 1;
			char word[DESC_LINE_MAX];
			memcpy(word, word_start, word_len);
			word[word_len] = '\0';

			char trial[DESC_LINE_MAX];
			if (has_word)
				snprintf(trial, sizeof(trial), "%s %s", cur, word);
			else
				snprintf(trial, sizeof(trial), "%s", word);

			int tw, th;
			GFX_measureText(f, trial, &tw, &th);
			if (tw <= max_width) {
				snprintf(cur, sizeof(cur), "%s", trial);
				has_word = true;
				p = q;
				while (*p == ' ')
					p++;
				continue;
			}

			if (has_word) {
				// Doesn't fit after what's already on this line - leave it
				// (word_start unconsumed) for the next line to retry.
				p = word_start;
				break;
			}

			// Empty line and the word alone still doesn't fit: hard-break
			// it across as many lines as it takes.
			int lo = 0;
			while (word[lo] && n < max_lines) {
				int hi = lo + 1, last_fit = lo + 1;
				while (word[hi]) {
					int plen = hi - lo;
					if (plen >= DESC_LINE_MAX - 1)
						break;
					char piece[DESC_LINE_MAX];
					memcpy(piece, word + lo, plen);
					piece[plen] = '\0';
					int pw, ph;
					GFX_measureText(f, piece, &pw, &ph);
					if (pw > max_width)
						break;
					last_fit = hi;
					hi++;
				}
				int plen = last_fit - lo;
				if (plen <= 0)
					plen = 1; // always make progress, even if one glyph alone overflows max_width
				snprintf(lines[n], DESC_LINE_MAX, "%.*s", plen, word + lo);
				n++;
				lo += plen;
			}
			p = q;
			while (*p == ' ')
				p++;
			hard_broke = true;
			break;
		}

		if (!hard_broke && has_word && n < max_lines)
			snprintf(lines[n++], DESC_LINE_MAX, "%s", cur);
	}

	// Overflow: more text remains than max_lines could hold - ellipsize the
	// last line drawn so the cut is visually obvious.
	while (*p == ' ')
		p++;
	if (*p && n > 0) {
		char* last = lines[n - 1];
		int len = (int)strlen(last);
		while (len > 0) {
			char trial[DESC_LINE_MAX];
			snprintf(trial, sizeof(trial), "%.*s...", len, last);
			int tw, th;
			GFX_measureText(f, trial, &tw, &th);
			if (tw <= max_width) {
				// `trial` already holds the winning "<prefix>..." string -
				// copy it in rather than re-running "%.*s..." with `last`
				// as both the snprintf destination and a source argument
				// (undefined behavior; -Wrestrict catches the self-alias).
				snprintf(last, DESC_LINE_MAX, "%s", trial);
				break;
			}
			len--;
		}
		if (len == 0)
			snprintf(last, DESC_LINE_MAX, "...");
	}

	return n;
}

// Same loop idiom as run_list (GFX_startFrame/PAD_poll/PWR_update with a
// persisted show_setting/UI_statusBarChanged/GFX_flip|GFX_sync) rather than a
// bare PAD_poll spin, so the brightness/volume indicator and status bar keep
// working on this screen too.
static int run_detail(AddonEntry* e) {
	bool dirty = true;
	bool changed = false;
	IndicatorType show_setting = INDICATOR_NONE;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_B))
			break;
		if (PAD_justPressed(BTN_A)) {
			const char* err = preflight(e);
			if (err) {
				UI_showMessage(screen, err, EXTRAS_MESSAGE_MS);
			} else {
				if (run_install(e) == 0) {
					changed = true;
					// Refresh e->installed from the marker install.sh just
					// wrote so this still-open detail page's next redraw
					// (metadata line + hint bar) reflects the new state
					// immediately, rather than only after catalog_load()
					// re-scans on return to the list.
					e->installed[0] = '\0';
					read_installed(e);
				}
			}
			dirty = true;
		}
		if (PAD_justPressed(BTN_X) && e->installed[0]) {
			if (run_confirm_dialog("Uninstall?",
								   "Saves and ROMs are kept - reinstall to play again.")) {
				if (run_uninstall(e) == 0) {
					changed = true;
					// Same refresh as the install path above: a successful
					// uninstall leaves no marker, so this reads back as "not
					// installed" and the detail page shows uninstalled right
					// away instead of on the next list visit.
					e->installed[0] = '\0';
					read_installed(e);
				}
			}
			dirty = true;
		}
		if (UI_statusBarChanged())
			dirty = true;
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			GFX_clear(screen);
			// Same menu bar as the list screen (status icons/battery/clock
			// live there); layout.list_y below already reserves its band, the
			// bar just wasn't drawn here (user-reported 2026-08-09).
			UI_renderMenuBar(screen, "Xtras");
			ListLayout layout = UI_calcListLayout(screen);
			int x = SCALE1(PADDING);
			int y = layout.list_y;

			// Title: the same font a selectable list row uses, so the
			// detail page reads as a continuation of the entry just picked
			// rather than a smaller/different "settings row" treatment.
			SDL_Surface* title = GFX_renderText(font.large, e->name, COLOR_WHITE);
			if (title) {
				SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){x, y, 0, 0});
				y += title->h;
				SDL_FreeSurface(title);
			} else {
				y += TTF_FontHeight(font.large);
			}
			y += SCALE1(4);

			// Metadata block, two compact dim lines (user layout 2026-08-10):
			//   GAME  ·  ~40 MB
			//   Installed v0.1.75  ·  Latest v0.1.76
			// Release tags are shown verbatim - upstreams differ on the "v"
			// prefix. The Latest segment appears only once the background
			// check has cached a value.
			char meta_lines[2][192];
			snprintf(meta_lines[0], sizeof(meta_lines[0]), "%s  \xC2\xB7  ~%d MB",
					 e->category, e->size_mb);
			int voff;
			if (e->installed[0])
				voff = snprintf(meta_lines[1], sizeof(meta_lines[1]),
								"Installed %s", e->installed);
			else
				voff = snprintf(meta_lines[1], sizeof(meta_lines[1]),
								"Not installed");
			if (e->latest[0] && voff < (int)sizeof(meta_lines[1]))
				snprintf(meta_lines[1] + voff, sizeof(meta_lines[1]) - voff,
						 "  \xC2\xB7  Latest %s", e->latest);
			for (int i = 0; i < 2; i++) {
				SDL_Surface* meta_surf = GFX_renderText(font.small, meta_lines[i], COLOR_GRAY);
				if (meta_surf) {
					SDL_BlitSurface(meta_surf, NULL, screen, &(SDL_Rect){x, y, 0, 0});
					y += meta_surf->h;
					SDL_FreeSurface(meta_surf);
				} else {
					y += TTF_FontHeight(font.small);
				}
				y += SCALE1(2);
			}
			y += SCALE1(6);

			// Description: font.tiny - one size below the metadata line's
			// font.small, per on-device feedback that the old settings-row
			// description (which was font.small too) read too large.
			// Pixel-wrapped to fill however much room is left above the
			// hint bar (UI_calcListLayout already reserves that gap).
			int desc_bottom = layout.list_y + layout.list_h;
			int line_h = TTF_FontHeight(font.tiny) + SCALE1(2);
			int max_lines = line_h > 0 ? (desc_bottom - y) / line_h : 0;
			if (max_lines > DESC_MAX_LINES)
				max_lines = DESC_MAX_LINES;
			if (max_lines < 0)
				max_lines = 0;

			char desc_lines[DESC_MAX_LINES][DESC_LINE_MAX];
			int n = wrap_text(font.tiny, e->desc, layout.max_width, desc_lines, max_lines);
			for (int i = 0; i < n; i++) {
				SDL_Surface* s = GFX_renderText(font.tiny, desc_lines[i], COLOR_GRAY);
				if (s) {
					SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x, y, 0, 0});
					SDL_FreeSurface(s);
				}
				y += line_h;
			}

			// X/UNINSTALL only once an entry is installed; hint-bar order is
			// the repo convention (B first, middle buttons next, A last -
			// matches every other X-hint call site, e.g. minarch's
			// ma_menu.c "B" "BACK" "X" "CLEAR" "A" "SET"). A's verb tracks
			// the update state - UPDATE, REINSTALL and INSTALL all run the
			// same install.sh (it always installs the latest release); only
			// the wording differs.
			if (e->installed[0])
				UI_renderButtonHintBar(screen, (char*[]){"B", "BACK",
														 "X", "UNINSTALL", "A",
														 entry_update_available(e) ? "UPDATE" : "REINSTALL", NULL});
			else
				UI_renderButtonHintBar(screen, (char*[]){"B", "BACK",
														 "A", "INSTALL", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
	return changed ? 1 : 0;
}

int main(int argc, char* argv[]) {
	PATHS_init(PLATFORM); // no-op on device; resolves SDCARD_PATH et al on desktop
	(void)argc;
	char* slash = strrchr(argv[0], '/');
	if (slash) {
		snprintf(pak_dir, sizeof(pak_dir), "%.*s", (int)(slash - argv[0]), argv[0]);
	} else
		getcwd(pak_dir, sizeof(pak_dir));

	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, "Xtras");

	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	InitSettings();
	PAD_init();
	PWR_init();
	PWR_disableSleep();
	PWR_disableAutosleep();
	PWR_disablePowerOff();

	catalog_load();

	// Snapshot the tracked entries for the release-check thread (its own
	// comment explains why it must not read entries[] directly), then fire
	// it. Detached: it owns no UI and the file-based handoff means nothing
	// joins on it; if it outlives an early quit the process teardown reaps
	// it. Creation failure just raises the done flag so run_list never
	// waits on a sweep that isn't coming.
	check_count = 0;
	for (int i = 0; i < entry_count; i++) {
		if (!entries[i].repo[0])
			continue;
		snprintf(check_items[check_count].id, sizeof(check_items[0].id), "%s", entries[i].id);
		snprintf(check_items[check_count].repo, sizeof(check_items[0].repo), "%s", entries[i].repo);
		check_count++;
	}
	pthread_t latest_thread;
	if (pthread_create(&latest_thread, NULL, latest_check_thread, NULL) == 0)
		pthread_detach(latest_thread);
	else
		latest_check_done = 1;

	run_list();

	PWR_enableSleep();
	PWR_enableAutosleep();
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return 0;
}
