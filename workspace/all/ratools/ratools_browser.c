#include "ratools_browser.h"

#include <libgen.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <rcheevos/rc_api_runtime.h> // RC_ACHIEVEMENT_TYPE_*

#include "api.h"
#include "defines.h"
#include "ra_offline.h" // RA_Offline_getGameRomPath
#include "ratools_data.h"
#include "ui_buttonhintbar.h"
#include "utils.h"
#include "ui_list.h"
#include "ui_image.h"
#include "ui_menubar.h"
#include "ui_emptystate.h"
#include "ui_font.h"
#include "ui_list_layout.h"

static void rat_badge_path(const char* badge_name, bool locked, char* buf, size_t n) {
	// naming matches ra_badges.c: <name>.png / <name>_lock.png
	// RA_BADGE_CACHE_DIR (ra_badges.h) is SHARED_USERDATA_PATH "/.ra/badges",
	// which is no longer adjacent-string-literal-concatenable now that
	// SHARED_USERDATA_PATH is a runtime array on desktop builds -- spell the
	// same path out with snprintf instead (byte-identical to the macro on
	// device, mirroring how ra_badges.c itself builds this same directory).
	if (locked)
		snprintf(buf, n, "%s/.ra/badges/%s_lock.png", SHARED_USERDATA_PATH, badge_name);
	else
		snprintf(buf, n, "%s/.ra/badges/%s.png", SHARED_USERDATA_PATH, badge_name);
}

// Resolve a game's box art from its recorded rom path, mirroring nxredux's
// thumbnail conventions (gamelist.c ~line 788):
//   single-file rom:  <dir>/.media/<rom-name>.png
//   multi-file rom (cue/bin/m3u in its own folder): nxredux shows the FOLDER
//   as the game entry, so the art is keyed off the folder name one level up:
//                     <parent>/.media/<folder-name>.png
static bool rat_game_art_path(const char* game_hash, char* out, size_t out_size) {
	char rom_path[512];
	if (!RA_Offline_getGameRomPath(game_hash, rom_path, sizeof(rom_path)))
		return false;

	ROM_findArt(rom_path, out, out_size);
	return true; // a missing file just means no image (loader returns NULL)
}

// ---------------- achievement detail (one achievement) ----------------

// Wrap text into at most max_lines lines (the last one ellipsized) in buf; lines[] points into buf. Returns the
// line count (0 for empty text).
static int rat_wrap_lines(TTF_Font* f, const char* text, int max_w, int max_lines, char* buf, size_t n,
						  char** lines) {
	if (!f || !text || !text[0])
		return 0;
	snprintf(buf, n, "%s", text);
	GFX_wrapText(f, buf, max_w, max_lines);
	int count = 0;
	char* cur = buf;
	while (cur && count < max_lines) {
		lines[count++] = cur;
		char* nl = strchr(cur, '\n');
		if (nl) {
			*nl = '\0';
			cur = nl + 1;
		} else {
			cur = NULL;
		}
	}
	return count;
}

// Draw lines centred on the screen, each in a line box of box px (the glyphs centred in it). Returns the y
// under the last box.
static int rat_draw_lines(SDL_Surface* screen, TTF_Font* f, char** lines, int count, int box, SDL_Color color,
						  int y) {
	for (int i = 0; i < count; i++) {
		SDL_Surface* s = f ? GFX_renderText(f, lines[i], color) : NULL;
		if (s) {
			SDL_BlitSurface(s, NULL, screen,
							&(SDL_Rect){(screen->w - s->w) / 2, y + (box - TTF_FontHeight(f)) / 2});
			SDL_FreeSurface(s);
		}
		y += box;
	}
	return y;
}

#define RAT_DETAIL_INFO_MAX 4
#define RAT_DETAIL_DESC_LINES 8

// Full page for one achievement (LIST-LAYOUT §10.6 "Achievement page"): the standard page title
// ("RetroAchievements | <game>" + " (u/t)"), then a centred column: the badge, 10 dp, the name (headline,
// bold), 4 dp, the description (secondary, white), 8 dp, the info lines (points, state, unlock rate, type;
// secondary, grey), lines 1.2 x their size. The text keeps its size and the badge takes the height left, 40 to
// 96 dp; only if even a 40 dp badge doesn't fit does the page scroll (UP/DOWN). LEFT/RIGHT flips between
// achievements, B goes back. All from the offline cache. Returns the achievement shown last (the list
// selects it).
static int rat_show_achievement_detail(SDL_Surface* screen, RAT_Achievement* achs, int count, int start,
									   const char* title, const char* suffix) {
	int i = start;
	int offset = 0; // scroll offset when the column doesn't fit
	bool dirty = true;
	int show = 1;
	while (show) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B)) {
			show = 0;
		} else if (PAD_justPressed(BTN_LEFT) || PAD_justRepeated(BTN_LEFT)) {
			i = (i - 1 + count) % count;
			offset = 0;
			dirty = true;
		} else if (PAD_justPressed(BTN_RIGHT) || PAD_justRepeated(BTN_RIGHT)) {
			i = (i + 1) % count;
			offset = 0;
			dirty = true;
		} else if (PAD_justRepeated(BTN_UP) && offset > 0) {
			offset -= UI_textRolePx(UI_TEXT_SECONDARY);
			if (offset < 0)
				offset = 0;
			dirty = true;
		} else if (PAD_justRepeated(BTN_DOWN)) {
			offset += UI_textRolePx(UI_TEXT_SECONDARY); // clamped below
			dirty = true;
		}

		if (dirty) {
			RAT_Achievement* a = &achs[i];

			GFX_clear(screen);
			UI_renderMenuBarAt(screen, title, suffix, -1, true, false);

			// the band: the title's letters to the hint icons, with 16 dp strips (the detail-page strip)
			int strip = NX_DP(16);
			int band_top = UI_pageTitleBandTop() + strip;
			int band_bottom = UI_buttonHintIconTop(screen->h) - strip;
			int band_h = band_bottom - band_top;
			int text_w = screen->w - 2 * NX_DP(24);

			// the text column (each role's font fetched where it's measured; UIFont pointers stay valid while
			// only these two sizes are in use)
			int name_box = (int)(UI_textRolePx(UI_TEXT_HEADLINE) * 1.2f + 0.5f);
			int sec_box = (int)(UI_textRolePx(UI_TEXT_SECONDARY) * 1.2f + 0.5f);
			char name_buf[256], desc_buf[512];
			char* name_lines[2];
			char* desc_lines[RAT_DETAIL_DESC_LINES];
			int n_name = rat_wrap_lines(UI_textRole(UI_TEXT_HEADLINE, true), a->title, text_w, 2, name_buf,
										sizeof(name_buf), name_lines);
			int n_desc = rat_wrap_lines(UI_textRole(UI_TEXT_SECONDARY, false), a->description, text_w,
										RAT_DETAIL_DESC_LINES, desc_buf, sizeof(desc_buf), desc_lines);

			char info[RAT_DETAIL_INFO_MAX][96];
			int n_info = 0;
			snprintf(info[n_info++], sizeof(info[0]), a->points == 1 ? "1 point" : "%u points", a->points);
			if (a->unlock_time > 0) {
				struct tm tm;
				localtime_r(&a->unlock_time, &tm);
				char tbuf[64];
				strftime(tbuf, sizeof(tbuf), "%B %d %Y, %I:%M%p", &tm);
				snprintf(info[n_info++], sizeof(info[0]),
						 a->state == RAT_ACH_PENDING ? "Unlocked %s (pending sync)" : "Unlocked %s", tbuf);
			} else {
				snprintf(info[n_info++], sizeof(info[0]), "Locked");
			}
			if (a->rarity > 0)
				snprintf(info[n_info++], sizeof(info[0]), "%.2f%% unlock rate", a->rarity);
			const char* type_str = NULL;
			switch (a->type) {
			case RC_ACHIEVEMENT_TYPE_MISSABLE:
				type_str = "Missable";
				break;
			case RC_ACHIEVEMENT_TYPE_PROGRESSION:
				type_str = "Progression";
				break;
			case RC_ACHIEVEMENT_TYPE_WIN:
				type_str = "Win Condition";
				break;
			default:
				break;
			}
			if (type_str)
				snprintf(info[n_info++], sizeof(info[0]), "%s", type_str);

			int text_h = NX_DP(10) + n_name * name_box;
			if (n_desc)
				text_h += NX_DP(4) + n_desc * sec_box;
			text_h += NX_DP(8) + n_info * sec_box;

			int badge_size = UI_detailBadgeSize(band_h, text_h, NX_DP(40), NX_DP(96));
			int total_h = badge_size + text_h;
			int max_offset = total_h > band_h ? total_h - band_h : 0;
			if (offset > max_offset)
				offset = max_offset;
			int y = total_h <= band_h ? band_top + (band_h - total_h) / 2 : band_top - offset;

			SDL_Rect old_clip;
			SDL_GetClipRect(screen, &old_clip);
			SDL_SetClipRect(screen, &(SDL_Rect){0, band_top, screen->w, band_h});

			char bp[512];
			rat_badge_path(a->badge_name, a->state == RAT_ACH_LOCKED, bp, sizeof(bp));
			SDL_Surface* badge = UI_loadRoundedImage(bp, badge_size, badge_size / 2);
			if (!badge) {
				rat_badge_path(a->badge_name, a->state != RAT_ACH_LOCKED, bp, sizeof(bp));
				badge = UI_loadRoundedImage(bp, badge_size, badge_size / 2);
			}
			if (badge) {
				SDL_BlitSurface(badge, NULL, screen, &(SDL_Rect){(screen->w - badge_size) / 2, y});
				SDL_FreeSurface(badge);
			}
			y += badge_size + NX_DP(10);

			y = rat_draw_lines(screen, UI_textRole(UI_TEXT_HEADLINE, true), name_lines, n_name, name_box,
							   COLOR_WHITE, y);
			TTF_Font* sec = UI_textRole(UI_TEXT_SECONDARY, false);
			if (n_desc)
				y = rat_draw_lines(screen, sec, desc_lines, n_desc, sec_box, COLOR_WHITE, y + NX_DP(4));
			char* info_lines[RAT_DETAIL_INFO_MAX];
			for (int k = 0; k < n_info; k++)
				info_lines[k] = info[k];
			rat_draw_lines(screen, sec, info_lines, n_info, sec_box, COLOR_GRAY, y + NX_DP(8));

			SDL_SetClipRect(screen, &old_clip);
			if (max_offset > 0)
				UI_renderScrollArrows(screen, band_top - strip / 2, band_bottom + strip / 2, offset > 0,
									  offset < max_offset);

			if (count > 1)
				UI_renderButtonHintBar(screen, (char*[]){"LEFT/RIGHT", "PREV/NEXT", "B", "BACK", NULL});
			else
				UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
	return i;
}

// ---------------- achievements screen (one game) ----------------

// The rich-row layout shared by the achievements list and the games list (LIST-LAYOUT §10.2): 1.5 x pill rows
// rounded to whole rows in the band under the title, half-row arrow strips.
static ListLayout rat_rich_layout(SDL_Surface* screen) {
	ListLayout layout = UI_calcListLayout(screen);
	int row_h = UI_listFitRowHeight(layout.avail_bottom - layout.avail_top, SCALE1(PILL_SIZE) * 3 / 2);
	UI_listLayoutSetRowHeight(&layout, row_h, 0);
	if (layout.items_per_page < 1)
		layout.items_per_page = 1;
	return layout;
}

// Draw one rich row: the capsule (selected), the round thumbnail, the title and the second line (in color).
static void rat_render_rich_row(SDL_Surface* screen, const ListLayout* layout, SDL_Surface* thumb,
								const char* title, const char* sub, SDL_Color sub_color, int y, bool sel) {
	RichRowPos pos = UI_renderRichRow(screen, layout, title, sub, y, sel);
	if (thumb)
		SDL_BlitScaled(thumb, NULL, screen, &(SDL_Rect){pos.image_x, pos.image_y, pos.image_size, pos.image_size});
	// the full title: the text clip (UI_renderListItemText) cuts it at the capsule's text end
	TTF_Font* title_font = UIFont_getPx(pos.title_px, false);
	if (title_font)
		UI_renderListItemText(screen, NULL, title, title_font, pos.text_x, pos.title_y, pos.text_max_width, sel);
	TTF_Font* second_font = UIFont_getPx(pos.second_px, false);
	SDL_Surface* s = second_font ? GFX_renderText(second_font, sub, sub_color) : NULL;
	if (s) {
		SDL_Rect src = {0, 0, s->w > pos.text_max_width ? pos.text_max_width : s->w, s->h};
		SDL_BlitSurface(s, &src, screen, &(SDL_Rect){pos.text_x, pos.second_y});
		SDL_FreeSurface(s);
	}
}

// Load a rich row's round thumbnail (NULL when missing): centre-cropped, over a neutral dark disc so art with
// transparent areas (mix images) still fills the circle, with an anti-aliased mask on the final surface.
static SDL_Surface* rat_load_thumb(const char* path, int size) {
	return UI_loadCircleThumb(path, size, (SDL_Color){0x26, 0x26, 0x26, 255});
}

static void rat_show_achievements(SDL_Surface* screen, const RAT_Game* game) {
	RAT_Achievement* achs = NULL;
	int count = RAT_loadAchievements(game, &achs);
	RAT_sortAchievements(achs, count); // honour the sort-order setting

	// No description area (LIST-LAYOUT §10.2): the rows fill the band and A opens the achievement's page
	ListLayout layout = rat_rich_layout(screen);
	int rows_visible = layout.items_per_page;
	int thumb_size = UI_richRowImageSize(layout.item_h);

	SDL_Surface** badges = NULL;
	if (count > 0) {
		badges = (SDL_Surface**)calloc(count, sizeof(SDL_Surface*));
		for (int i = 0; i < count && badges; i++) {
			char p[512];
			// unlocked/pending show the colored badge, locked the grey one
			rat_badge_path(achs[i].badge_name, achs[i].state == RAT_ACH_LOCKED, p, sizeof(p));
			badges[i] = rat_load_thumb(p, thumb_size);
			if (!badges[i]) { // fall back to the other variant if only one is cached
				rat_badge_path(achs[i].badge_name, achs[i].state != RAT_ACH_LOCKED, p, sizeof(p));
				badges[i] = rat_load_thumb(p, thumb_size);
			}
		}
	}

	const SDL_Color col_state_unlocked = {130, 220, 130, 255};
	const SDL_Color col_state_pending = {255, 200, 80, 255};
	const SDL_Color col_state_locked = {150, 150, 150, 255};

	int selected = 0, scroll = 0;

	// "RetroAchievements | <game>" with the never-truncated " (u/t)" suffix: only the game title ellipsizes
	char header_title[192];
	char header_count[32];
	UI_pageTitle(header_title, sizeof(header_title), "RetroAchievements", game->title);
	snprintf(header_count, sizeof(header_count), " (%d/%d)", game->unlocked, game->total);

	bool quit = false, dirty = true;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		// wrap around at both ends, like every other list in the firmware
		if (PAD_justRepeated(BTN_DOWN) && count > 0) {
			selected = (selected + 1) % count;
			dirty = true;
		}
		if (PAD_justRepeated(BTN_UP) && count > 0) {
			selected = (selected - 1 + count) % count;
			dirty = true;
		}
		if (PAD_justPressed(BTN_B))
			quit = true;
		if (PAD_justPressed(BTN_A) && count > 0) {
			// LEFT/RIGHT on the page moves the list too: B returns to the last viewed
			selected = rat_show_achievement_detail(screen, achs, count, selected, header_title, header_count);
			PAD_reset(); // the B that closed the detail view is still latched
			dirty = true;
		}

		if (selected < scroll)
			scroll = selected;
		if (selected >= scroll + rows_visible)
			scroll = selected - rows_visible + 1;

		if (dirty) {
			GFX_clear(screen);
			UI_renderMenuBarAt(screen, header_title, header_count, -1, true, false);

			for (int r = 0; r < rows_visible && scroll + r < count; r++) {
				int i = scroll + r;
				int y = layout.list_y + r * layout.item_h;
				const char* state_txt =
					achs[i].state == RAT_ACH_UNLOCKED ? "Unlocked" : achs[i].state == RAT_ACH_PENDING ? "Pending sync"
																									  : "Locked";
				SDL_Color state_col =
					achs[i].state == RAT_ACH_UNLOCKED ? col_state_unlocked : achs[i].state == RAT_ACH_PENDING ? col_state_pending
																											  : col_state_locked;
				char sub[64];
				snprintf(sub, sizeof(sub), "%s - %u pts", state_txt, achs[i].points);
				// row 2 in the achievement's state color
				rat_render_rich_row(screen, &layout, badges ? badges[i] : NULL, achs[i].title, sub, state_col, y,
									i == selected);
			}

			UI_renderScrollIndicatorsAt(screen, &layout, scroll, rows_visible, count);

			if (count == 0)
				UI_renderEmptyState(screen, "No achievements cached",
									"Data downloads when this game is played online", NULL);

			if (count > 0)
				UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "DETAILS", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	if (badges) {
		for (int i = 0; i < count; i++)
			if (badges[i])
				SDL_FreeSurface(badges[i]);
		free(badges);
	}
	free(achs);
}

// ---------------- games list ----------------

void RATBrowser_run(SDL_Surface* screen) {
	RAT_Game* games = NULL;
	int count = RAT_listGames(&games);

	ListLayout layout = rat_rich_layout(screen);
	int rows_visible = layout.items_per_page;
	int thumb_size = UI_richRowImageSize(layout.item_h);

	// Load box art up front (one shot, not lazy) — the list is small and
	// this keeps the render loop free of I/O.
	SDL_Surface** arts = NULL;
	if (count > 0) {
		arts = (SDL_Surface**)calloc(count, sizeof(SDL_Surface*));
		for (int i = 0; i < count && arts; i++) {
			char art_path[1024];
			if (rat_game_art_path(games[i].hash, art_path, sizeof(art_path)))
				arts[i] = rat_load_thumb(art_path, thumb_size);
		}
	}

	const SDL_Color col_sub = {180, 180, 180, 255};
	const SDL_Color col_pending = {255, 200, 80, 255};

	int selected = 0, scroll = 0;

	bool quit = false, dirty = true;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		// wrap around at both ends, like every other list in the firmware
		if (PAD_justRepeated(BTN_DOWN) && count > 0) {
			selected = (selected + 1) % count;
			dirty = true;
		}
		if (PAD_justRepeated(BTN_UP) && count > 0) {
			selected = (selected - 1 + count) % count;
			dirty = true;
		}
		if (PAD_justPressed(BTN_B))
			quit = true;
		if (PAD_justPressed(BTN_A) && count > 0) {
			rat_show_achievements(screen, &games[selected]);
			dirty = true;
		}

		if (selected < scroll)
			scroll = selected;
		if (selected >= scroll + rows_visible)
			scroll = selected - rows_visible + 1;

		if (dirty) {
			GFX_clear(screen);
			UI_renderMenuBar(screen, "RetroAchievements | Achievements");

			for (int r = 0; r < rows_visible && scroll + r < count; r++) {
				int i = scroll + r;
				int y = layout.list_y + r * layout.item_h;
				char sub[96];
				if (games[i].pending > 0)
					snprintf(sub, sizeof(sub), "%d/%d unlocked - %d pending sync",
							 games[i].unlocked, games[i].total, games[i].pending);
				else
					snprintf(sub, sizeof(sub), "%d/%d unlocked", games[i].unlocked, games[i].total);
				// row 2 yellow while sync is pending
				rat_render_rich_row(screen, &layout, arts ? arts[i] : NULL, games[i].title, sub,
									games[i].pending > 0 ? col_pending : col_sub, y, i == selected);
			}

			UI_renderScrollIndicatorsAt(screen, &layout, scroll, rows_visible, count);

			if (count == 0)
				UI_renderEmptyState(screen, "No cached games",
									"Play online once or download game data in Settings", NULL);
			else
				UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "OPEN", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	if (arts) {
		for (int i = 0; i < count; i++)
			if (arts[i])
				SDL_FreeSurface(arts[i]);
		free(arts);
	}
	free(games);
}
