// Game list screen: the ROM/console browser that nextui.c boots into.
// Input handling, background/thumbnail resolution and rendering for this
// screen live here; nextui.c only dispatches to it. (split from nextui.c)

#include "gamelist.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "shortcuts.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_message.h"
#include "ui_contextmenu.h"
#include "ui_list.h"
#include "utils.h"

#include "content.h"
#include "gameswitcher.h"
#include "imgloader.h"
#include "launcher.h"
#include "search.h"
#include "types.h"

#include <assert.h>
#include <msettings.h>
#include <libgen.h>
#include <string.h>

static bool gl_simple_mode = false;

static ScrollTextState list_scroll = {0};
static ShortcutAction confirm_shortcut_action = SHORTCUT_NONE;
static Entry* confirm_shortcut_entry = NULL;

static bool had_thumb = false;
static int ox;
static char folderBgPath[1024] = {0};

void GameList_init(bool simple_mode) {
	gl_simple_mode = simple_mode;
}

bool GameList_confirmOpen(void) {
	return confirm_shortcut_action != SHORTCUT_NONE;
}

bool GameList_scrollBusy(void) {
	return ScrollText_isScrolling(&list_scroll) || ScrollText_needsRender(&list_scroll);
}

bool GameList_scrollIsScrolling(void) {
	return ScrollText_isScrolling(&list_scroll);
}

void GameList_scrollTickIdle(void) {
	if (confirm_shortcut_action != SHORTCUT_NONE) {
		GFX_clearLayers(LAYER_SCROLLTEXT);
	} else {
		ScrollText_activateAfterDelay(&list_scroll);
		if (ScrollText_isScrolling(&list_scroll)) {
			ScrollText_animateOnly(&list_scroll);
		}
	}
}

void GameList_clearScroll(void) {
	ScrollText_clear(&list_scroll);
}

static void resolveAndLoadBackground(Entry* entry, const char* rompath,
									 bool* list_show_entry_names) {
	// Persists across calls to avoid redundant background reloads
	static int lastType = -1;

	char defaultBgPath[512];
	snprintf(defaultBgPath, sizeof(defaultBgPath), SDCARD_PATH "/bg.png");

	// Resolve: what path to compare for changes, and what bg image to load
	const char* cmpPath = NULL;
	char bgPath[512] = {0};

	if ((entry->type == ENTRY_DIR || entry->type == ENTRY_ROM) &&
		Shortcuts_exists(entry->path + strlen(SDCARD_PATH))) {
		cmpPath = entry->path;
	} else if ((entry->type == ENTRY_DIR || entry->type == ENTRY_ROM) &&
			   CFG_getRomsUseFolderBackground()) {
		cmpPath = entry->type == ENTRY_DIR ? entry->path : rompath;
		snprintf(bgPath, sizeof(bgPath), "%s/.media/%s.png", cmpPath,
				 entry->type == ENTRY_DIR ? "bg" : "bglist");
		if (!exists(bgPath))
			strncpy(bgPath, defaultBgPath, sizeof(bgPath) - 1);
	} else if (entry->type == ENTRY_PAK && suffixMatch(".pak", entry->path)) {
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
	if (strcmp(cmpPath, folderBgPath) == 0 && lastType == entry->type)
		return;

	lastType = entry->type;
	strncpy(folderBgPath, cmpPath, sizeof(folderBgPath) - 1);

	// Load background, or clear if image doesn't exist
	if (bgPath[0] && exists(bgPath))
		startLoadFolderBackground(bgPath, onBackgroundLoaded);
	else {
		onBackgroundLoaded(NULL);
		*list_show_entry_names = true;
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
	} else if (PAD_quickMenuPressed(now)) {
		result.screen = SCREEN_QUICKMENU;
		result.animdir = SLIDE_DOWN;
		result.folderbgchanged = true;
		*dirty = true;
		GFX_clearLayers(LAYER_SCROLLTEXT);
		ScrollText_clear(&list_scroll);
		if (!HAS_POWER_BUTTON && !gl_simple_mode)
			PWR_enableSleep();
		return result;
	} else if (PAD_tappedSelect(now) && confirm_shortcut_action == SHORTCUT_NONE) {
		result.screen = SCREEN_GAMESWITCHER;
		GameSwitcher_resetSelection();
		result.animdir = SLIDE_UP;
		*dirty = true;
		GFX_clearLayers(LAYER_SCROLLTEXT);
		ScrollText_clear(&list_scroll);
		return result;
	} else if (total > 0 && confirm_shortcut_action == SHORTCUT_NONE) {
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

	if (confirm_shortcut_action == SHORTCUT_NONE && PAD_justRepeated(BTN_L1) &&
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
	} else if (confirm_shortcut_action == SHORTCUT_NONE && PAD_justRepeated(BTN_R1) &&
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

	Entry* entry = top->entries->items[top->selected];

	if (*dirty && total > 0)
		readyResume(entry);

	// Handle confirmation dialog for shortcuts
	if (confirm_shortcut_action != SHORTCUT_NONE) {
		if (PAD_justPressed(BTN_A)) {
			Shortcuts_confirmAction(confirm_shortcut_action,
									confirm_shortcut_entry);
			confirm_shortcut_action = SHORTCUT_NONE;
			confirm_shortcut_entry = NULL;

			// Refresh root directory to show updated shortcuts
			Directory* root = stack->items[0];
			EntryArray_free(root->entries);
			root->entries = getRoot(gl_simple_mode);
			IntArray_init(&root->alphas);
			Directory_index(root);
			// Keep selected in bounds
			if (root->selected >= root->entries->count) {
				root->selected =
					root->entries->count > 0 ? root->entries->count - 1 : 0;
			}

			*dirty = true;
		} else if (PAD_justPressed(BTN_B)) {
			confirm_shortcut_action = SHORTCUT_NONE;
			confirm_shortcut_entry = NULL;
			*dirty = true;
		}
	} else if (total > 0 && resume.can_resume && PAD_justReleased(BTN_RESUME) && !PAD_isPressed(BTN_L2) && !PAD_isPressed(BTN_R2)) {
		resume.should_resume = true;
		Entry_open(entry);
		*dirty = true;
	}
	// Y to search at root
	else if (stack->count == 1 && PAD_justReleased(BTN_Y)) {
		if (Search_open()) {
			result.screen = SCREEN_SEARCH;
			result.animdir = SLIDE_LEFT;
			GFX_clearLayers(LAYER_SCROLLTEXT);
			ScrollText_clear(&list_scroll);
		}
		*dirty = true;
		return result;
	}
	// Y to add/remove shortcut (only in Tools folder or console directory)
	else if (total > 0 &&
			 (Shortcuts_isInToolsFolder(top->path) ||
			  Shortcuts_isInConsoleDir(top->path)) &&
			 canPinEntry(entry) && PAD_justReleased(BTN_Y)) {
		if (Shortcuts_exists(entry->path + strlen(SDCARD_PATH))) {
			confirm_shortcut_action = SHORTCUT_REMOVE;
		} else {
			confirm_shortcut_action = SHORTCUT_ADD;
		}
		confirm_shortcut_entry = entry;
		*dirty = true;
	} else if (total > 0 && PAD_justPressed(BTN_A)) {
		Entry_open(entry);
		if (entry->type == ENTRY_DIR && !startgame) {
			result.animdir = SLIDE_LEFT;
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

	Entry* entry = top->entries->items[top->selected];
	assert(entry);
	char tmp_path[MAX_PATH];
	strncpy(tmp_path, entry->path, sizeof(tmp_path) - 1);
	tmp_path[sizeof(tmp_path) - 1] = '\0';

	char* res_name = strrchr(tmp_path, '/');
	if (res_name)
		res_name++;
	else
		res_name = tmp_path;

	char path_copy[1024];
	strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
	path_copy[sizeof(path_copy) - 1] = '\0';

	char* rompath = dirname(path_copy);

	char res_copy[1024];
	strncpy(res_copy, res_name, sizeof(res_copy) - 1);
	res_copy[sizeof(res_copy) - 1] = '\0';

	char* dot = strrchr(res_copy, '.');
	if (dot)
		*dot = '\0';

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

		// pin action (hardware hints override this when volume is pressed)
		if (!(show_setting && !GetHDMI()) && total > 0 &&
			!GetHDMI() &&
			(Shortcuts_isInToolsFolder(top->path) ||
			 Shortcuts_isInConsoleDir(top->path)) &&
			canPinEntry(entry)) {
			right_pairs[p++] = "Y";
			right_pairs[p++] = Shortcuts_exists(entry->path + strlen(SDCARD_PATH))
								   ? "UNPIN"
								   : "PIN";
		}

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
		} else if (confirm_shortcut_action == SHORTCUT_NONE) {
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

	// Render confirmation dialog for shortcuts
	if (confirm_shortcut_action != SHORTCUT_NONE && confirm_shortcut_entry) {
		char* title =
			confirm_shortcut_action == SHORTCUT_ADD ? "Pin shortcut?" : "Unpin shortcut?";
		UI_renderConfirmDialog(screen, title, confirm_shortcut_entry->name);
	}
}
