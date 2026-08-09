/*
 * extras.elf — the Xtras catalog: browse curated games/tools, install
 * on-device. Entries live in ./catalog/<id>/{meta.txt,install.sh,files/}.
 * Games install into "Roms/Xtra Games (EXTRAS)"; tools into Tools/.
 */

#include <ctype.h>
#include <dirent.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include <sys/statvfs.h>
#include <sys/wait.h> // WIFEXITED/WEXITSTATUS on run_install's pclose() status
#include <unistd.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_downloadprogress.h"
#include "ui_draw.h" // UI_renderCenteredButtons (draw_result_dialog)
#include "ui_emptystate.h"
#include "ui_list.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "ui_splash.h"

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
// single source of truth for EXTRAS_ROMS_DIR/EXTRAS_PORTS_DIR (env passed to
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

typedef struct {
	char id[64]; // catalog folder name
	char name[META_STR];
	char category[16]; // "GAME" | "TOOL"
	char desc[1024];
	char version[64];
	int size_mb;
	char installed[64]; // "" = not installed, else marker contents
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
	else if (!strcmp(key, "version"))
		snprintf(e->version, sizeof(e->version), "%s", val);
	else if (!strcmp(key, "size_mb"))
		e->size_mb = atoi(val);
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
	return e->name[0] && e->category[0] && e->version[0];
}

// Marker written by install.sh; GAME entries only in v1.
static void read_installed(AddonEntry* e) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path),
			 "%s/Roms/" EXTRAS_ROMS_DIRNAME "/.ports/%s/.nx_addon_version",
			 SDCARD_PATH, e->id);
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	if (fgets(e->installed, sizeof(e->installed), f)) {
		char* nl = strchr(e->installed, '\n');
		if (nl)
			*nl = '\0';
	}
	fclose(f);
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
// group, no header), then every installed entry (the "Installed" header is
// drawn separately by the renderer once it reaches installed_start).
// Rebuilt on tab switch and after any install/uninstall so it always
// reflects current AddonEntry.installed state - entries[] itself is never
// reordered by this, only entry_cmp() (via catalog_load's qsort, unchanged)
// still governs category+name order within each group.
typedef struct {
	int indices[MAX_ENTRIES]; // entries[] index for each row, in display order
	int count;				  // total rows in this tab
	int installed_start;	  // indices[installed_start..count) is the "Installed" group; == count if none installed
} TabRows;

static void build_tab_rows(AddonTab tab, TabRows* rows) {
	rows->count = 0;
	for (int i = 0; i < entry_count; i++)
		if (!strcmp(entries[i].category, TAB_CATEGORY[tab]) && !entries[i].installed[0])
			rows->indices[rows->count++] = i;
	rows->installed_start = rows->count;
	for (int i = 0; i < entry_count; i++)
		if (!strcmp(entries[i].category, TAB_CATEGORY[tab]) && entries[i].installed[0])
			rows->indices[rows->count++] = i;
}

// iOS-style segmented-control GAMES/TOOLS switcher (Task 13c; supersedes the
// original separate-pill-buttons look), drawn directly under the menu bar
// and switched via L1/R1 in run_list() below. One rounded THEME_COLOR2
// "container" strip holds both segments, left-aligned and only as wide as
// the segments themselves (not full screen width); the active segment is a
// smaller THEME_COLOR1 pill inset within the strip, same "bright selection"
// family + text color (UI_getListTextColor) an entry row's own selected
// pill uses; the inactive segment is plain text in the dim color
// render_section_header uses for "Installed", sitting directly on the
// strip with no background of its own.
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
	// 255+NUL - see the badge-reservation fix in render_entry_row for the
	// stack-overflow this exact gotcha caused there).
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
		// pill uses. Inactive label: the dim gray render_section_header uses
		// for "Installed" (COLOR_GRAY) - NOT UI_getListTextColor(false),
		// which returns THEME_COLOR4 (the ordinary unselected list-row
		// color, not this dim one) and made the inactive tab read as bright
		// as the active one.
		SDL_Color color = selected ? UI_getListTextColor(true) : COLOR_GRAY;
		SDL_Surface* surf = TTF_RenderUTF8_Blended(font.small, cell_text[t], color);
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

// Non-selectable section header row ("Installed"): small, dim text with no
// pill background so it reads clearly as a label rather than a selectable
// item. Half the height of an entry row - one line of font.small doesn't
// need a full pill's worth of vertical breathing room. Mirrors ratools.c's
// render_main_header()/render_main_list() split (a non-selectable status
// blit ahead of the selectable pill list, sharing the same
// PADDING/BUTTON_PADDING x so everything lines up). Same function Task 8
// used for the (now superseded-by-tabs) inline "GAMES"/"TOOLS" headers -
// only the call site/label changed.
static int render_section_header(SDL_Surface* screen, ListLayout* layout,
								 const char* label, int y) {
	int h = layout->item_h / 2;
	SDL_Surface* surf = TTF_RenderUTF8_Blended(font.small, label, COLOR_GRAY);
	if (surf) {
		int x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
		SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){x, y + (h - surf->h) / 2, 0, 0});
		SDL_FreeSurface(surf);
	}
	return y + h;
}

// Selectable entry row, built from the same stateless pill primitives
// UI_renderSimpleMenu itself uses (UI_renderListItemPill for the pill
// background + text position, UI_renderListItemText for the clipped/
// scrolling label) - just driven by an explicit running y instead of a flat
// item index, so a header row can sit between groups without perturbing any
// entry's position math. No more "[installed]"/"[update]" name suffix
// (Task 12; grouping into Installed already says "installed") - an
// update-available entry (installed marker present but stale vs
// e->version) instead gets a minimal right-aligned "update" marker, same
// lightweight approach scraper.c's renderMainMenuBadge() uses for its own
// right-side pill badge (small font.tiny text, dim unless the row is
// selected) rather than the heavier two-row UI_renderListItemPillBadged.
//
// Fix round 1 (review Important): the badge occupies a fixed strip at the
// screen's right edge, independent of the pill/text. Without reserving that
// strip, a long name that has to truncate is sized/truncated against the
// FULL row width and can reach - or on a long enough name, exactly overlap
// - the same right-edge coordinate the badge occupies, so name tail +
// ellipsis render underneath the badge text. `reserve` below (badge width +
// one PADDING gap) is fed in as UI_renderListItemPill's `prefix_width` -
// the exact mechanism the pill/text-truncation math already has for "leave
// room for something else on this row" (musicplayer's ui_music.c uses it
// the mirror way, reserving LEFT-side room for a leading icon before the
// text) - so GFX_truncateText (inside UI_calcListPillWidth, called from
// UI_renderListItemPill) computes the truncation point against the
// narrowed budget up front, before any text is measured or drawn, rather
// than the badge being layered on after the fact. Non-badged rows pass
// reserve=0, i.e. exactly the prior behavior/full width.
static int render_entry_row(SDL_Surface* screen, ListLayout* layout,
							const AddonEntry* e, int y, bool selected) {
	bool update_available = e->installed[0] && strcmp(e->installed, e->version) != 0;
	const char* badge = "update";
	int badge_w = 0, badge_h = 0, reserve = 0;
	if (update_available) {
		TTF_SizeUTF8(font.tiny, badge, &badge_w, &badge_h);
		reserve = badge_w + SCALE1(PADDING);
	}

	char truncated[256];
	ListItemPos pos = UI_renderListItemPill(screen, layout, font.large, e->name,
											truncated, y, selected, reserve);
	int text_w = pos.pill_width - SCALE1(BUTTON_PADDING * 2) - reserve;
	UI_renderListItemText(screen, NULL, truncated, font.large,
						  pos.text_x, pos.text_y, text_w, selected);

	if (update_available) {
		SDL_Color color = selected ? COLOR_WHITE : COLOR_GRAY;
		int bx = screen->w - SCALE1(PADDING) - badge_w - SCALE1(PADDING);
		int by = y + (layout->item_h - badge_h) / 2;
		GFX_blitText(font.tiny, badge, 0, color, screen, &(SDL_Rect){bx, by, badge_w, badge_h});
	}
	return y + layout->item_h;
}

// selected indexes rows->indices (0..rows->count-1) - never entries[]
// directly, and never a header row, since build_tab_rows() above already
// leaves headers out of the row list entirely. "Skip headers" falls out of
// that for free in run_list()'s PAD navigation rather than needing
// dedicated logic here, same as Task 8's approach to the inline headers it
// superseded.
static void render_extras_list(SDL_Surface* screen, AddonTab active_tab, int selected, const TabRows* rows) {
	UI_renderMenuBar(screen, "Xtras");
	ListLayout layout = UI_calcListLayout(screen);
	layout.list_y = render_tab_bar(screen, &layout, active_tab);

	if (rows->count == 0) {
		UI_renderEmptyState(screen, "Nothing here yet", NULL, NULL);
		return;
	}

	int y = layout.list_y;
	for (int i = 0; i < rows->count; i++) {
		if (i == rows->installed_start)
			// The header sat flush against the pill right below it; a
			// quarter item height of breathing room reads right on device
			// (half was too much - user feedback 2026-08-08).
			y = render_section_header(screen, &layout, "Installed", y) + layout.item_h / 4;
		y = render_entry_row(screen, &layout, &entries[rows->indices[i]], y, i == selected);
	}
}

static int run_detail(AddonEntry* e); // Task 4; returns 1 if install state changed

// GFX_startFrame/PWR_update/UI_statusBarChanged and PAD_navigateMenu (rather
// than hand-rolled BTN_UP/BTN_DOWN math) match every other pak-tool loop in
// this tree (options.c's run_editor/run_picker, netplay-wizard's pickers) —
// PWR_update still has to run every frame so the power button and battery/
// clock status bar work here same as anywhere else, even though sleep/
// autosleep/power-off themselves are held off for the whole session.
//
// `selected` is scoped to the ACTIVE tab's rows (0..rows.count-1), not a
// global entries[] index - build_tab_rows() gives each tab its own
// available/installed ordering, so "which entry is selected" only makes
// sense relative to whichever tab is currently showing. Switching tabs
// (L1/R1) resets it to 0 (first entry) rather than trying to carry a
// position across two differently-ordered/differently-sized row lists; no
// other nx-redux pak has an established L1/R1 tab-switch convention to
// follow (checked gametime/scraper/musicplayer - their L1/R1 uses are
// alpha-jump and value-step, not tab switching), so plain "either shoulder
// button flips the tab" is used here - exactly two tabs makes wrap and
// clamp the same thing anyway. Task 13 adds an "L1/R1"/"TAB" hint to the
// bar below (user feedback: the switch wasn't discoverable) - one combined
// button-pill hint ("L1/R1" as the button label, GFX_blitButton renders any
// >1-char button string as a text pill rather than two separate circular
// buttons) rather than two separate {L1,"..."}/{R1,"..."} pairs, since
// UI_renderButtonHintBar caps at 4 total pairs and doesn't clip pixel
// overflow past dst->w on its own - measured against the real theme font
// (both device point sizes) before picking this: three pairs (B EXIT / A
// DETAILS / L1/R1 TAB) sum to ~577px on the Brick's 1024px-wide bar and
// ~387px on tg5050's 1280px - comfortable margin on both, see the report.
static void run_list(void) {
	AddonTab active_tab = TAB_GAMES;
	int selected = 0;
	TabRows rows;
	build_tab_rows(active_tab, &rows);
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_B))
			break;
		if (PAD_justPressed(BTN_L1) || PAD_justPressed(BTN_R1)) {
			active_tab = (active_tab == TAB_GAMES) ? TAB_TOOLS : TAB_GAMES;
			build_tab_rows(active_tab, &rows);
			selected = 0;
			dirty = true;
		}
		if (PAD_navigateMenu(&selected, rows.count))
			dirty = true;
		if (PAD_justPressed(BTN_A) && rows.count) {
			AddonEntry* e = &entries[rows.indices[selected]];
			if (run_detail(e)) {
				// The entry just acted on may move from the not-installed
				// group to Installed (or back) once rows are rebuilt below
				// - re-locate it by id so the selection stays on the same
				// entry instead of landing on whatever unrelated row now
				// occupies index `selected` in the regrouped list.
				char id[64];
				snprintf(id, sizeof(id), "%s", e->id);
				catalog_load();
				build_tab_rows(active_tab, &rows);
				selected = 0;
				for (int i = 0; i < rows.count; i++) {
					if (!strcmp(entries[rows.indices[i]].id, id)) {
						selected = i;
						break;
					}
				}
			}
			dirty = true;
		}
		if (UI_statusBarChanged())
			dirty = true;
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			GFX_clear(screen);
			render_extras_list(screen, active_tab, selected, &rows);
			// L1/R1 leads the bar (user preference 2026-08-08), then B, then A.
			// Always shown, even on an empty tab (v1 ships with TOOLS empty) -
			// otherwise the tab switcher's only remaining discoverability
			// hint (see run_list's own comment above) disappears exactly
			// when a user most needs it, on the one screen with nothing else
			// to look at. A/DETAILS only makes sense once there's a row to
			// select.
			if (rows.count)
				UI_renderButtonHintBar(screen, (char*[]){"L1/R1", "TAB", "B", "EXIT", "A", "DETAILS", NULL});
			else
				UI_renderButtonHintBar(screen, (char*[]){"L1/R1", "TAB", "B", "EXIT", NULL});
			GFX_flip(screen);
			dirty = false;
		} else
			GFX_sync();
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
			SDL_Surface* s = TTF_RenderUTF8_Blended(font.small, sub_lines[i], COLOR_WHITE);
			if (s) {
				SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){(screen->w - s->w) / 2, y});
				SDL_FreeSurface(s);
			}
			y += sub_line_h;
		}
	}

	if (detail_h) {
		y += SCALE1(BUTTON_MARGIN);
		SDL_Surface* s = TTF_RenderUTF8_Blended(font.tiny, detail, COLOR_GRAY);
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
	if (!logs_path)
		logs_path = USERDATA_PATH "/logs";

	char cmd[MAX_PATH * 4];
	snprintf(cmd, sizeof(cmd),
			 "PLATFORM='%s' SDCARD_PATH='%s' LOGS_PATH='%s' "
			 "EXTRAS_ROMS_DIR='%s/Roms/" EXTRAS_ROMS_DIRNAME "' "
			 "EXTRAS_PORTS_DIR='%s/Roms/" EXTRAS_ROMS_DIRNAME "/.ports' "
			 "CATALOG_DIR='%s/catalog/%s' "
			 "sh '%s/catalog/%s/%s' 2>&1",
			 PLATFORM, SDCARD_PATH, logs_path,
			 SDCARD_PATH, SDCARD_PATH, pak_dir, e->id, pak_dir, e->id, script_name);

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
		char subtitle[64];
		if (!strcmp(title, "Install"))
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
// Never touches anything under EXTRAS_PORTS_DIR/<id>/ beyond the marker
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
			 "%s/Roms/" EXTRAS_ROMS_DIRNAME "/.ports/%s/.nx_addon_version",
			 SDCARD_PATH, e->id);
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

// X on an installed entry: confirm before uninstalling. Own minimal
// GFX_startFrame/PAD_poll loop (not run_list/run_detail's fuller idiom - a
// modal draws nothing else, so there's no status bar/power indicator to
// keep alive under it) with PAD_poll()+PAD_reset() at the end so the
// confirming A or cancelling B press doesn't leak into run_detail's own
// PAD_justPressed checks next frame - same precedent/shape as settings.c's
// run_confirm_dialog (a different translation unit, so the identical name
// doesn't clash).
static bool run_confirm_dialog(const char* title, const char* subtitle) {
	bool confirmed = false;
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A)) {
			confirmed = true;
			break;
		}
		if (PAD_justPressed(BTN_B))
			break;
		UI_renderConfirmDialog(screen, title, subtitle);
		GFX_flip(screen);
	}
	PAD_poll();
	PAD_reset();
	return confirmed;
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
			TTF_SizeUTF8(f, trial, &tw, &th);
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
					TTF_SizeUTF8(f, piece, &pw, &ph);
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
			TTF_SizeUTF8(f, trial, &tw, &th);
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
			ListLayout layout = UI_calcListLayout(screen);
			int x = SCALE1(PADDING);
			int y = layout.list_y;

			// Title: the same font a selectable list row uses, so the
			// detail page reads as a continuation of the entry just picked
			// rather than a smaller/different "settings row" treatment.
			SDL_Surface* title = TTF_RenderUTF8_Blended(font.large, e->name, COLOR_WHITE);
			if (title) {
				SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){x, y, 0, 0});
				y += title->h;
				SDL_FreeSurface(title);
			} else {
				y += TTF_FontHeight(font.large);
			}
			y += SCALE1(4);

			// Metadata line: category, version, install state, download
			// size - one compact dim line instead of three settings rows.
			char meta[192];
			const char* state = !e->installed[0]				   ? "Not installed"
								: strcmp(e->installed, e->version) ? "Update available"
																   : "Installed";
			snprintf(meta, sizeof(meta), "%s  \xC2\xB7  v%s  \xC2\xB7  %s  \xC2\xB7  ~%d MB",
					 e->category, e->version, state, e->size_mb);
			SDL_Surface* meta_surf = TTF_RenderUTF8_Blended(font.small, meta, COLOR_GRAY);
			if (meta_surf) {
				SDL_BlitSurface(meta_surf, NULL, screen, &(SDL_Rect){x, y, 0, 0});
				y += meta_surf->h;
				SDL_FreeSurface(meta_surf);
			} else {
				y += TTF_FontHeight(font.small);
			}
			y += SCALE1(8);

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
				SDL_Surface* s = TTF_RenderUTF8_Blended(font.tiny, desc_lines[i], COLOR_GRAY);
				if (s) {
					SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x, y, 0, 0});
					SDL_FreeSurface(s);
				}
				y += line_h;
			}

			// X/UNINSTALL only once an entry is installed; hint-bar order is
			// the repo convention (B first, middle buttons next, A last -
			// matches every other X-hint call site, e.g. minarch's
			// ma_menu.c "B" "BACK" "X" "CLEAR" "A" "SET").
			if (e->installed[0])
				UI_renderButtonHintBar(screen, (char*[]){"B", "BACK",
														 "X", "UNINSTALL", "A", "REINSTALL", NULL});
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
	run_list();

	PWR_enableSleep();
	PWR_enableAutosleep();
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return 0;
}
