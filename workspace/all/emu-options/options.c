/*
 * options.elf — pre-launch emulator options editor and emulator picker.
 *
 * Four modes, one binary (the CLI shape Tasks 4/5 script against):
 *
 *   options.elf --json J --ini I --game NAME
 *       Global mode. Edits the device-wide emulator config (flycast's
 *       emu.cfg) in place, exactly like the in-game overlay's Options screen.
 *
 *   options.elf --json J --ini I --override O --game NAME
 *       Per-game mode. The INI is read for the baseline only; edits land in a
 *       minimal per-game override file that carries just the diff (see
 *       opts_override.h). Rows that differ from the global value are marked
 *       with a leading "* ".
 *
 *   options.elf --json J --minarch-dir D [--minarch-game NAME]
 *               [--minarch-system P] [--minarch-default P] --game NAME
 *       Minarch mode. Edits minarch's layered flat cfg files (see
 *       opts_minarch.h): global edits land in D/minarch[-$DEVICE].cfg,
 *       --minarch-game switches to the full-snapshot per-game file. Mutually
 *       exclusive with --ini/--override.
 *
 *   options.elf --pick --entry NAME PATH [--entry NAME PATH ...]
 *       Emulator picker. Prints the chosen PATH on stdout and nothing else,
 *       exit 0; exit 1 when the user backs out.
 *
 * Exit codes: 0 save-and-exit (or a picked entry), 1 picker cancelled / config
 * could not be loaded, 2 usage error.
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp, for the picker's alphabetical sort
#include <unistd.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "emu_overlay_cfg.h"
#include "opts_minarch.h"
#include "opts_override.h"
#include "ui_buttonhintbar.h"
#include "ui_list.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "ui_splash.h"

// --entry pairs accepted in picker mode; extras are reported and dropped.
// A card carrying every minarch EXTRAS pak plus DC/N64 plus the 7 BASE paks
// offers 39 entries (seen in the field 2026-07-31 — the BASE paks, appended
// last by Emulator Settings.pak, were all silently dropped at 32), so the
// cap needs real headroom above the worst known card.
#define MAX_PICK_ENTRIES 64
// Longest screen is a section's items plus the trailing reset row.
#define MAX_ROWS (EMU_OVL_MAX_ITEMS + 1)
/*
 * A section name, or an item's "* " + label. 256 is far more than either needs
 * — the longest label the shipped schemas carry is mupen64plus's 22-char
 * "Hi-Res Noise Dithering" — and the buffer size is not what keeps a row
 * legible anyway: the section screen's pill truncates to fit, but
 * UI_renderSettingsRow measures and blits an item label whole, so a long
 * enough user-authored label runs under the value column. That is the
 * component's limitation, shared with every screen built on it.
 */
#define ROW_TEXT_MAX 256
// Right-hand value column on the item screen; the row right-aligns and clips it.
#define ROW_VALUE_MAX 64
// How long a terminal message stays up before the process moves on (ms).
#define OPTS_MESSAGE_MS 2000

// EmuOvlConfig is ~11 MB at this binary's raised limits (32 sections x 64
// items x a ~5 KB item — see the Makefile's -DEMU_OVL_MAX_* line) and the
// two state structs another ~20 KB. None of that belongs on any stack, and
// as BSS it costs nothing until touched: zero-fill pages are only committed
// on write, and this is a standalone process that lives for one edit session
// and exits.
static EmuOvlConfig cfg;
static OptsOverrideState st;
static OptsMinarchState mst;

// Row text for whichever list is on screen, rebuilt on every state change and
// after every edit.
static char row_text[MAX_ROWS][ROW_TEXT_MAX];
// Section screen only: the pill list takes an array of label pointers.
static const char* row_ptr[MAX_ROWS];
/*
 * Item screen only. UI_renderSettingsPage borrows every string by pointer, and
 * the rows are built once per state change but rendered on many later frames,
 * so nothing it reads may live on the builder's stack: hence a values table
 * beside row_text, a row_item[] that outlives build_item_rows, and .desc
 * pointed straight into the (also file-scope) cfg.
 */
static char row_value[MAX_ROWS][ROW_VALUE_MAX];
static UISettingsItem row_item[MAX_ROWS];

static SDL_Surface* screen = NULL;

//////////////////////////////////
// Screens
//////////////////////////////////

/*
 * Pill list, used by the picker and the section screen.
 *
 * This is UI_renderSimpleMenu's body with the button hints hoisted into a
 * parameter. That function hardcodes its A hint to "OPEN", which is wrong on
 * the picker — A picks an emulator there — and its hint bar cannot be
 * suppressed, so drawing a second one over it would stack UI_getScrim's
 * 70%-opaque black twice and leave the first bar's labels showing through.
 * Everything else is the same shared components UI_renderSimpleMenu uses.
 */
static void render_list(const char* title, const char** labels, int count,
						int selected, int* scroll, char** hints) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, title);

	ListLayout layout = UI_calcListLayout(screen);
	UI_adjustListScroll(selected, scroll, layout.items_per_page);

	int visible = count - *scroll;
	if (visible > layout.items_per_page)
		visible = layout.items_per_page;
	if (visible < 0)
		visible = 0;

	char truncated[ROW_TEXT_MAX];
	for (int i = 0; i < visible; i++) {
		int idx = *scroll + i;
		bool is_selected = (idx == selected);
		MenuItemPos pos = UI_renderMenuItemPill(screen, &layout, labels[idx], truncated, i, is_selected, 0);
		UI_renderListItemText(screen, NULL, truncated, font.large,
							  pos.text_x, pos.text_y, layout.max_width, is_selected);
	}

	UI_renderScrollIndicators(screen, *scroll, layout.items_per_page, count);

	UI_renderButtonHintBar(screen, hints);
	GFX_flip(screen);
}

/*
 * Item screen: the Settings app's two-column rows (label pill left, value
 * right, "< value >" on the selected row) instead of one long "Label: Value"
 * pill. SETTINGS_ROW_COUNT short rows fit where the pill list fit
 * list_h/PILL_SIZE tall ones, and the component's last row carries the
 * selected item's description — which is where the schema's options_hint now
 * lands too, as the fallback for items that ship no description of their own.
 *
 * `selected` is absolute (the component windows it itself), and the component
 * runs UI_adjustListScroll on *scroll, so nothing outside touches it.
 *
 * UI_renderSettingsPage rewrites layout.item_h/items_per_page for its own row
 * count, so the layout is recomputed here every frame rather than shared with
 * render_list. The component draws no chrome: bar, hints and flip stay here.
 *
 * The hints are the one piece of chrome this screen cannot hand in from the
 * outside, because they change per row — so they are built here rather than
 * passed as a parameter.
 */
static void render_item_page(const char* title, UISettingsItem* items, int count,
							 int selected, int* scroll) {
	GFX_clear(screen);
	UI_renderMenuBar(screen, title);

	ListLayout layout = UI_calcListLayout(screen);
	UI_renderSettingsPage(screen, &layout, items, count, selected, scroll, NULL);

	/*
	 * Keyed off the very flag the component draws its "< >" arrows from, so the
	 * hint and the row it describes can never disagree: an arrow-less row (the
	 * reset row) is pressed, a cycleable one is stepped. Same shape as the other
	 * screens on this component — portmaster.c:781-786,
	 * musicplayer/ui_settings.c:69-81. A still cycles a value as well as
	 * LEFT/RIGHT, but the bar names the arrows the row is showing.
	 */
	bool cycleable = (selected >= 0 && selected < count && items[selected].cycleable);
	UI_renderButtonHintBar(screen, (char*[]){
									   cycleable ? "LEFT/RIGHT" : "A",
									   cycleable ? "CHANGE" : "SELECT",
									   "B", "BACK",
									   NULL});
	GFX_flip(screen);
}

static void show_message(const char* message, int hold_ms) {
	GFX_clear(screen);
	UI_renderCenteredMessage(screen, message);
	GFX_flip(screen);
	SDL_Delay(hold_ms);
}

//////////////////////////////////
// Picker
//////////////////////////////////

/*
 * stdout is the picker's return channel, so it has to stay clean. GFX_init,
 * InitSettings and the platform layer all log with plain printf, and the
 * callers in Task 5 read this process's stdout — park the real stdout on a
 * spare descriptor and point fd 1 at stderr until there is an answer to print.
 * Returns the saved descriptor, or -1 when the redirect could not be set up
 * (in which case nothing was changed and no restore is needed).
 *
 * FD_CLOEXEC on the saved descriptor is not hygiene, it is the fix for a hang.
 * The caller captures us in a command substitution
 * (Emulator Settings.pak/launch.sh: CHOSEN="$(options.elf --pick ...)"), so
 * `saved` is the write end of that pipe, and the shell's read only returns at
 * EOF — i.e. when every copy of the write end is closed. On tg5050,
 * InitSettings() below calls SetRawFanSpeed(-2), which runs
 * system("…/fancontrol normal &") (tg5050/libmsettings/msettings.c:1092-1115).
 * That trailing & leaves a daemon alive after we exit, and without CLOEXEC it
 * inherits `saved` and holds the pipe open forever: the picker exits 0, the
 * chosen path is printed, and Tools -> Emulator Settings still hangs on a black
 * screen waiting for EOF. tg5040 ships no fan daemon, which is the only reason
 * it ever worked there. Set it the moment the dup succeeds, so no exec between
 * here and stdout_restore can leak it, whatever a platform decides to spawn.
 *
 * fd 1 and fd 2 are inherited by those children too, and are deliberately left
 * alone: launch.sh points stderr at a log file, so a daemon holding it open
 * blocks nothing. Only a pipe end can stall the capture.
 */
static int stdout_park(void) {
	int saved = dup(STDOUT_FILENO);
	if (saved < 0)
		return -1;
	fcntl(saved, F_SETFD, FD_CLOEXEC);
	if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
		close(saved);
		return -1;
	}
	return saved;
}

static void stdout_restore(int saved) {
	if (saved < 0)
		return;
	fflush(stdout);
	dup2(saved, STDOUT_FILENO);
	close(saved);
}

// Returns 0 with *out_path pointing into argv when an entry was chosen,
// 1 when the user backed out, 2 when there was nothing to pick.
static int run_picker(int argc, char** argv, const char** out_path) {
	const char* names[MAX_PICK_ENTRIES];
	const char* paths[MAX_PICK_ENTRIES];
	int count = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--entry") != 0 || i + 2 >= argc)
			continue;
		if (count < MAX_PICK_ENTRIES) {
			names[count] = argv[i + 1];
			paths[count] = argv[i + 2];
			count++;
		} else {
			fprintf(stderr, "options: ignoring --entry '%s' (max %d)\n", argv[i + 1], MAX_PICK_ENTRIES);
		}
		i += 2;
	}

	// main rejects this before GFX_init; kept so the loop below can never run
	// with an empty list (PAD_navigateMenu would divide the cursor by nothing).
	if (count == 0)
		return 2;

	// Alphabetical, case-insensitive. Entries arrive in the caller's glob
	// order — SD paks first, then BASE paks appended by Emulator
	// Settings.pak — which puts "Game Boy Advance" after "Virtual Boy";
	// sorting here keeps every caller consistent without shell-side gymnastics.
	for (int i = 1; i < count; i++) {
		const char* n = names[i];
		const char* p = paths[i];
		int j = i - 1;
		while (j >= 0 && strcasecmp(names[j], n) > 0) {
			names[j + 1] = names[j];
			paths[j + 1] = paths[j];
			j--;
		}
		names[j + 1] = n;
		paths[j + 1] = p;
	}

	int selected = 0;
	int scroll = 0;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (1) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (PAD_navigateMenu(&selected, count)) {
			dirty = true;
		} else if (PAD_justPressed(BTN_A)) {
			*out_path = paths[selected];
			return 0;
		} else if (PAD_justPressed(BTN_B)) {
			return 1;
		}

		if (dirty) {
			render_list("Emulator Settings", names, count, selected, &scroll,
						(char*[]){"A", "SELECT", "B", "BACK", NULL});
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

//////////////////////////////////
// Editing
//////////////////////////////////

// BOOL toggles, CYCLE steps through values[] with wrap, INT steps by int_step
// and clamps (unlike the in-game overlay's wrap: that screen only has a "next"
// action, this one has LEFT and RIGHT).
static void cycle_item(EmuOvlItem* item, int dir) {
	switch (item->type) {
	case EMU_OVL_TYPE_BOOL:
		item->staged_value = item->staged_value ? 0 : 1;
		break;
	case EMU_OVL_TYPE_CYCLE: {
		if (item->value_count <= 0)
			break;
		int idx = 0;
		for (int i = 0; i < item->value_count; i++) {
			if (item->values[i] == item->staged_value) {
				idx = i;
				break;
			}
		}
		idx = (idx + dir + item->value_count) % item->value_count;
		item->staged_value = item->values[idx];
		break;
	}
	case EMU_OVL_TYPE_INT:
		item->staged_value += dir * item->int_step;
		if (item->staged_value > item->int_max)
			item->staged_value = item->int_max;
		else if (item->staged_value < item->int_min)
			item->staged_value = item->int_min;
		break;
	case EMU_OVL_TYPE_ENUM:
		if (item->value_count <= 0)
			break;
		item->staged_value = (item->staged_value + dir + item->value_count) % item->value_count;
		break;
	}
}

// Per-game mode: back to the device-global value. Global mode: back to the
// schema default (the same primitive the in-game overlay's reset row uses).
static void reset_section(int s, bool per_game) {
	if (!per_game) {
		emu_ovl_cfg_reset_section_to_defaults(&cfg.sections[s]);
		return;
	}
	for (int i = 0; i < cfg.sections[s].item_count; i++)
		cfg.sections[s].items[i].staged_value = st.global_value[s][i];
}

static void clear_all_overrides(void) {
	for (int s = 0; s < cfg.section_count; s++)
		reset_section(s, true);
}

// Staged items that still differ from the global baseline, i.e. what
// opts_write_override would put in the file.
static int count_overrides(void) {
	int n = 0;
	for (int s = 0; s < cfg.section_count; s++)
		for (int i = 0; i < cfg.sections[s].item_count; i++)
			if (cfg.sections[s].items[i].staged_value != st.global_value[s][i])
				n++;
	return n;
}

// The row's left column: the schema label, with a leading "* " when the row
// overrides the global value.
static void row_label(const EmuOvlItem* item, bool overridden, char* out, int out_size) {
	snprintf(out, out_size, "%s%s", overridden ? "* " : "", item->label);
}

// The row's right column. The value shown is the STAGED one, so edits are
// visible immediately, and it prefers the schema's human label over
// emu_ovl_cfg_format_value's output — that formatter speaks the INI's dialect
// ("True"/"False", raw ints), which is right for the file and wrong for a row.
// ENUM rows ride the same label scan (values[i] == i for enums), and the
// format_value fallback prints the raw value string.
static void row_value_text(const EmuOvlItem* item, char* out, int out_size) {
	const char* human = NULL;

	for (int v = 0; v < item->value_count; v++) {
		if (item->values[v] == item->staged_value && item->labels[v][0] != '\0') {
			human = item->labels[v];
			break;
		}
	}

	if (human)
		snprintf(out, out_size, "%s", human);
	else if (item->type == EMU_OVL_TYPE_BOOL)
		// Matches the in-game overlay's On/Off; the shipped schemas give bools
		// no labels[] of their own.
		snprintf(out, out_size, "%s", item->staged_value ? "On" : "Off");
	else
		emu_ovl_cfg_format_value(item, item->staged_value, out, out_size);
}

static int build_section_rows(bool per_game) {
	int n = 0;
	for (int s = 0; s < cfg.section_count && n < MAX_ROWS; s++)
		snprintf(row_text[n++], ROW_TEXT_MAX, "%s", cfg.sections[s].name);
	// The count is this row's only feedback: clearing overrides changes nothing
	// on this screen (section rows carry no values), so without it a press of A
	// here looks like it did nothing at all.
	if (per_game && n < MAX_ROWS)
		snprintf(row_text[n++], ROW_TEXT_MAX, "Clear All Overrides (%d)", count_overrides());
	for (int i = 0; i < n; i++)
		row_ptr[i] = row_text[i];
	return n;
}

/*
 * Fills row_item[] for the item screen. `.swatch` is -1 on every row: zero
 * would be a valid colour (black) to UI_renderSettingsRow, not "no swatch".
 * `.desc` prefers the item's own description and falls back to the schema's
 * options_hint, which is how the "Restart game to apply changes" notice stays
 * on screen now that the hand-rolled footer is gone.
 */
static int build_item_rows(int s, bool per_game) {
	EmuOvlSection* sec = &cfg.sections[s];
	int n = 0;
	for (int i = 0; i < sec->item_count && n < MAX_ROWS - 1; i++) {
		const EmuOvlItem* item = &sec->items[i];
		// Recomputed from staged_value on every rebuild, so the marker tracks
		// the edit in progress; on entry it matches the file because
		// opts_read_override staged what it read.
		bool overridden = per_game && item->staged_value != st.global_value[s][i];
		row_label(item, overridden, row_text[n], ROW_TEXT_MAX);
		row_value_text(item, row_value[n], ROW_VALUE_MAX);
		row_item[n] = (UISettingsItem){
			.label = row_text[n],
			.value = row_value[n],
			.swatch = -1,
			.cycleable = 1,
			.desc = item->description[0] != '\0' ? item->description : cfg.options_hint,
		};
		n++;
	}
	// No value, so UI_renderSettingsRow gives it the label-only highlight the
	// other reset rows in this UI get rather than a "< >" value column.
	snprintf(row_text[n], ROW_TEXT_MAX, "%s", per_game ? "Reset to Global" : "Reset to Default");
	row_item[n] = (UISettingsItem){
		.label = row_text[n],
		.value = NULL,
		.swatch = -1,
		.cycleable = 0,
		.desc = cfg.options_hint,
	};
	n++;
	return n;
}

typedef enum { ED_SECTIONS,
			   ED_ITEMS } EdState;

// Runs until B on the section list. Nothing here touches the SD card: every
// edit moves staged_value only, and the single write happens in save_edits().
static void run_editor(const char* title, bool per_game) {
	EdState state = ED_SECTIONS;
	int section_selected = 0;
	int section_scroll = 0;
	int item_selected = 0;
	int item_scroll = 0;
	int current_section = 0;
	int row_count = build_section_rows(per_game);
	bool dirty = true;
	bool done = false;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!done) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (state == ED_SECTIONS) {
			if (PAD_navigateMenu(&section_selected, row_count)) {
				dirty = true;
			} else if (PAD_justPressed(BTN_A)) {
				if (section_selected >= cfg.section_count) {
					clear_all_overrides(); // the trailing per-game row
					row_count = build_section_rows(per_game);
				} else {
					current_section = section_selected;
					item_selected = 0;
					item_scroll = 0;
					row_count = build_item_rows(current_section, per_game);
					state = ED_ITEMS;
				}
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				done = true;
			}
		} else {
			EmuOvlSection* sec = &cfg.sections[current_section];

			if (PAD_navigateMenu(&item_selected, row_count)) {
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				section_selected = current_section;
				row_count = build_section_rows(per_game);
				state = ED_SECTIONS;
				dirty = true;
			} else if (item_selected >= sec->item_count) {
				if (PAD_justPressed(BTN_A)) {
					reset_section(current_section, per_game);
					row_count = build_item_rows(current_section, per_game);
					dirty = true;
				}
			} else {
				int dir = 0;
				if (PAD_justPressed(BTN_A) || PAD_justRepeated(BTN_RIGHT))
					dir = 1;
				else if (PAD_justRepeated(BTN_LEFT))
					dir = -1;
				if (dir != 0) {
					cycle_item(&sec->items[item_selected], dir);
					row_count = build_item_rows(current_section, per_game);
					dirty = true;
				}
			}
		}

		if (dirty) {
			if (state == ED_SECTIONS)
				render_list(title, row_ptr, row_count, section_selected, &section_scroll,
							(char*[]){"A", "OPEN", "B", "SAVE", NULL});
			else
				render_item_page(cfg.sections[current_section].name, row_item, row_count,
								 item_selected, &item_scroll);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}

// The one and only write, on the way out. Per cursor move would mean an SD
// card write per D-pad press.
static void save_edits(bool per_game, const char* ini_path, const char* override_path) {
	if (per_game) {
		// Writes the staged-vs-global diff, or unlinks the file when there is
		// none; syncs on its own.
		if (opts_write_override(&cfg, &st, override_path) < 0) {
			fprintf(stderr, "options: failed to write override %s\n", override_path);
			show_message("Failed to save - check SD card", OPTS_MESSAGE_MS);
			return;
		}
		opts_commit(&cfg, &st);
		return;
	}

	// Global mode. emu_ovl_cfg_write_ini/has_changes/apply_staged all dispatch
	// on item->dirty, and this editor moves staged_value without setting it
	// (opts_commit does the same flagging for the per-game path), so flag what
	// actually moved first. The call order below is the in-game overlay's on
	// close — flycast.patch's write_config_if_dirty(): has_changes -> write_ini
	// -> apply_staged. The patch's cfgSaveBool/cfgSaveInt step between the last
	// two has no counterpart here: flycast is not running yet, so there is no
	// in-memory cfgdb to keep in sync, and it loads this file on start-up.
	for (int s = 0; s < cfg.section_count; s++) {
		for (int i = 0; i < cfg.sections[s].item_count; i++) {
			EmuOvlItem* item = &cfg.sections[s].items[i];
			if (item->staged_value != item->current_value)
				item->dirty = true;
		}
	}

	if (!emu_ovl_cfg_has_changes(&cfg))
		return;

	if (emu_ovl_cfg_write_ini(&cfg, ini_path) < 0) {
		fprintf(stderr, "options: failed to write ini %s\n", ini_path);
		show_message("Failed to save - check SD card", OPTS_MESSAGE_MS);
		return;
	}
	emu_ovl_cfg_apply_staged(&cfg);
	sync();
}

//////////////////////////////////
// main
//////////////////////////////////

int main(int argc, char* argv[]) {
	const char* json_path = NULL;
	const char* ini_path = NULL;
	const char* override_path = NULL;
	const char* game_name = NULL;
	const char* minarch_dir = NULL;
	const char* minarch_game = NULL;
	const char* minarch_system = NULL;
	const char* minarch_default = NULL;
	bool pick = false;
	int entry_count = 0;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		bool has_value = (i + 1 < argc);

		if (strcmp(arg, "--pick") == 0) {
			pick = true;
		} else if (strcmp(arg, "--json") == 0 && has_value) {
			json_path = argv[++i];
		} else if (strcmp(arg, "--ini") == 0 && has_value) {
			ini_path = argv[++i];
		} else if (strcmp(arg, "--override") == 0 && has_value) {
			override_path = argv[++i];
		} else if (strcmp(arg, "--game") == 0 && has_value) {
			game_name = argv[++i];
		} else if (strcmp(arg, "--minarch-dir") == 0 && has_value) {
			minarch_dir = argv[++i];
		} else if (strcmp(arg, "--minarch-game") == 0 && has_value) {
			minarch_game = argv[++i];
		} else if (strcmp(arg, "--minarch-system") == 0 && has_value) {
			minarch_system = argv[++i];
		} else if (strcmp(arg, "--minarch-default") == 0 && has_value) {
			minarch_default = argv[++i];
		} else if (strcmp(arg, "--entry") == 0 && i + 2 < argc) {
			entry_count++;
			i += 2; // run_picker re-walks argv for these
		} else {
			fprintf(stderr, "options: unknown or incomplete argument '%s'\n", arg);
			return 2;
		}
	}

	// Both usage checks run before GFX_init: a usage error must not cost the
	// caller a black screen and a video teardown.
	if (pick && entry_count == 0) {
		fprintf(stderr, "options: --pick needs at least one '--entry NAME PATH'\n");
		return 2;
	}
	if (!pick && minarch_dir && (ini_path || override_path)) {
		fprintf(stderr, "options: --minarch-dir is exclusive with --ini/--override\n");
		return 2;
	}
	if (!pick && !minarch_dir && (!json_path || !ini_path)) {
		fprintf(stderr, "options: --json and --ini are required\n");
		return 2;
	}
	if (!pick && minarch_dir && !json_path) {
		fprintf(stderr, "options: --minarch-dir requires --json\n");
		return 2;
	}

	int saved_stdout = pick ? stdout_park() : -1;

	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, game_name ? game_name : "Emulator Settings");

	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	InitSettings();
	PAD_init();
	PWR_init();

	// This process sits between the launcher and the emulator; a sleep or an
	// auto power-off here would strand the launch script.
	PWR_disableSleep();
	PWR_disableAutosleep();
	PWR_disablePowerOff();

	int exit_code = 0;
	const char* picked_path = NULL;

	if (pick) {
		exit_code = run_picker(argc, argv, &picked_path);
	} else if (minarch_dir) {
		if (emu_ovl_cfg_load(&cfg, json_path) != 0) {
			fprintf(stderr, "options: failed to load schema %s\n", json_path);
			show_message("Settings unavailable", OPTS_MESSAGE_MS);
			exit_code = 1;
		} else if (opts_minarch_load(&cfg, &mst, &st, minarch_system, minarch_default,
									 minarch_dir, minarch_game) != 0) {
			fprintf(stderr, "options: failed to read minarch config in %s\n", minarch_dir);
			show_message("Settings unavailable", OPTS_MESSAGE_MS);
			exit_code = 1;
		} else {
			// opts_minarch_load filled st with the console baseline, so the
			// per-game markers, counts and resets below are the exact same
			// machinery the --override path uses.
			bool per_game = (minarch_game != NULL);
			run_editor(game_name ? game_name : "Emulator Settings", per_game);
			if (opts_minarch_save(&cfg, &mst, &st) < 0) {
				fprintf(stderr, "options: failed to save minarch config\n");
				show_message("Failed to save - check SD card", OPTS_MESSAGE_MS);
			}
			opts_minarch_free(&mst);
			emu_ovl_cfg_free(&cfg);
		}
	} else if (emu_ovl_cfg_load(&cfg, json_path) != 0) {
		fprintf(stderr, "options: failed to load schema %s\n", json_path);
		show_message("Settings unavailable", OPTS_MESSAGE_MS);
		exit_code = 1;
	} else if (emu_ovl_cfg_read_ini(&cfg, ini_path) != 0) {
		fprintf(stderr, "options: failed to read config %s\n", ini_path);
		show_message("Settings unavailable", OPTS_MESSAGE_MS);
		exit_code = 1;
	} else {
		bool per_game = (override_path != NULL);

		// Baseline first, then layer the override on top: opts_read_override
		// rewrites current_value, so a snapshot taken after it would record
		// the overridden value as the global one and the diff would come out
		// empty.
		opts_snapshot_globals(&cfg, &st);
		if (per_game)
			opts_read_override(&cfg, &st, override_path);

		run_editor(game_name ? game_name : "Emulator Settings", per_game);
		save_edits(per_game, ini_path, override_path);
		emu_ovl_cfg_free(&cfg);
	}

	PWR_enableSleep();
	PWR_enableAutosleep();

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	// After GFX_quit, so nothing the teardown logs can reach the real stdout.
	if (pick) {
		stdout_restore(saved_stdout);
		if (picked_path)
			printf("%s\n", picked_path);
	}

	return exit_code;
}
