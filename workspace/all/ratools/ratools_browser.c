#include "ratools_browser.h"

#include <libgen.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <rcheevos/rc_api_runtime.h> // RC_ACHIEVEMENT_TYPE_*

#include "api.h"
#include "defines.h"
#include "ra_badges.h"	// RA_BADGE_CACHE_DIR only (no ra_badges.c linked)
#include "ra_offline.h" // RA_Offline_getGameRomPath
#include "ratools_data.h"
#include "ui_buttonhintbar.h"
#include "utils.h"
#include "ui_list.h"
#include "ui_image.h"
#include "ui_menubar.h"

static int rat_render_text(SDL_Surface* screen, const char* text, TTF_Font* f,
						   SDL_Color color, int x, int y) {
	SDL_Surface* s = TTF_RenderUTF8_Blended(f, text, color);
	if (!s)
		return 0;
	SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x, y, s->w, s->h});
	int w = s->w;
	SDL_FreeSurface(s);
	return w;
}

// Image slot size inside a rich pill row (matches
// UI_renderListItemPillRich: item_h 1.5x PILL_SIZE minus 4px padding each side)
#define RAT_THUMB_SIZE (SCALE1(PILL_SIZE) * 3 / 2 - SCALE1(4) * 2)

static void rat_badge_path(const char* badge_name, bool locked, char* buf, size_t n) {
	// naming matches ra_badges.c: <name>.png / <name>_lock.png
	if (locked)
		snprintf(buf, n, RA_BADGE_CACHE_DIR "/%s_lock.png", badge_name);
	else
		snprintf(buf, n, RA_BADGE_CACHE_DIR "/%s.png", badge_name);
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

	char dir_buf[512], base_buf[512];
	snprintf(dir_buf, sizeof(dir_buf), "%s", rom_path);
	snprintf(base_buf, sizeof(base_buf), "%s", rom_path);

	char* dir = dirname(dir_buf); // may point into dir_buf, or a static "."
	char* base = strrchr(base_buf, '/');
	base = base ? base + 1 : base_buf;
	char* dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';

	snprintf(out, out_size, "%s/.media/%s.png", dir, base);
	if (exists(out))
		return true;

	// multi-file fallback: art named after the rom's containing folder
	char parent_buf[512];
	snprintf(parent_buf, sizeof(parent_buf), "%s", dir);
	char* folder = strrchr(parent_buf, '/');
	if (folder && folder != parent_buf) {
		*folder = '\0';
		snprintf(out, out_size, "%s/.media/%s.png", parent_buf, folder + 1);
	}
	return true; // a missing file just means no image (loader returns NULL)
}

// Settings-style description: tiny font, gray, wrapped onto at most two
// lines with EACH line centered (this toolchain's SDL_ttf 2.0.13 predates
// TTF_SetFontWrappedAlign, whose wrapped rendering left-aligns lines, so we
// wrap manually). The second line is ellipsized when the text doesn't fit.
static void rat_render_desc_wrapped(SDL_Surface* screen, const char* text,
									SDL_Color color, int wrap_w, int y) {
	char work[256], line1[256] = "", line2[260] = "";
	snprintf(work, sizeof(work), "%s", text);

	// greedy word wrap into line1 then line2
	bool overflow = false;
	char* save = NULL;
	char* cur = line1;
	for (char* tok = strtok_r(work, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
		char cand[256];
		snprintf(cand, sizeof(cand), "%s%s%s", cur, cur[0] ? " " : "", tok);
		int w = 0;
		TTF_SizeUTF8(font.tiny, cand, &w, NULL);
		if (w <= wrap_w || !cur[0]) {
			memcpy(cur, cand, strlen(cand) + 1);
		} else if (cur == line1) {
			cur = line2;
			memcpy(cur, tok, strlen(tok) + 1);
		} else {
			overflow = true; // words left over past line 2
			break;
		}
	}

	// ellipsize line2 when words were dropped
	if (overflow && line2[0]) {
		for (;;) {
			char cand[264];
			snprintf(cand, sizeof(cand), "%s...", line2);
			int w = 0;
			TTF_SizeUTF8(font.tiny, cand, &w, NULL);
			if (w <= wrap_w)
				break;
			size_t len = strlen(line2);
			if (len <= 1)
				break;
			len--;
			// never split a UTF-8 sequence mid-way
			while (len > 1 && (line2[len] & 0xC0) == 0x80)
				len--;
			line2[len] = '\0';
		}
		strcat(line2, "...");
	}

	int lh = TTF_FontHeight(font.tiny);
	const char* lines[2] = {line1, line2};
	for (int i = 0; i < 2; i++) {
		if (!lines[i][0])
			break;
		SDL_Surface* s = TTF_RenderUTF8_Blended(font.tiny, lines[i], color);
		if (s) {
			SDL_BlitSurface(s, NULL, screen,
							&(SDL_Rect){(screen->w - s->w) / 2, y + i * lh, 0, 0});
			SDL_FreeSurface(s);
		}
	}
}

// ---------------- achievement detail (one achievement) ----------------

// tiny centered metadata line on the detail canvas
static void rat_detail_meta_line(SDL_Surface* canvas, const char* text, int* content_y) {
	SDL_Surface* s = TTF_RenderUTF8_Blended(font.tiny, text, COLOR_LIGHT_TEXT);
	if (!s)
		return;
	SDL_BlitSurface(s, NULL, canvas, &(SDL_Rect){(canvas->w - s->w) / 2, *content_y});
	*content_y += s->h + SCALE1(2);
	SDL_FreeSurface(s);
}

// Full detail view for one achievement, mirroring the in-game page: badge,
// title, description, points, unlock time, rarity, and type tag - all from
// the offline cache. LEFT/RIGHT flips between achievements, B goes back.
// Content is drawn to a transparent canvas and blitted vertically centered.
static void rat_show_achievement_detail(SDL_Surface* screen, RAT_Achievement* achs,
										int count, int start) {
	int i = start;
	SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormat(
		0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!canvas)
		return;

	bool dirty = true;
	int show = 1;
	while (show) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justPressed(BTN_B)) {
			show = 0;
		} else if (PAD_justPressed(BTN_LEFT) || PAD_justRepeated(BTN_LEFT)) {
			i = (i - 1 + count) % count;
			dirty = true;
		} else if (PAD_justPressed(BTN_RIGHT) || PAD_justRepeated(BTN_RIGHT)) {
			i = (i + 1) % count;
			dirty = true;
		}

		if (dirty) {
			RAT_Achievement* a = &achs[i];

			GFX_clear(screen);
			SDL_FillRect(canvas, NULL, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));

			int content_y = 0;
			int center_x = canvas->w / 2;
			int badge_size = SCALE1(64);

			// full-size badge (the list thumbnails are smaller)
			char bp[512];
			rat_badge_path(a->badge_name, a->state == RAT_ACH_LOCKED, bp, sizeof(bp));
			SDL_Surface* badge = UI_loadRoundedImage(bp, badge_size, SCALE1(8));
			if (!badge) {
				rat_badge_path(a->badge_name, a->state != RAT_ACH_LOCKED, bp, sizeof(bp));
				badge = UI_loadRoundedImage(bp, badge_size, SCALE1(8));
			}
			if (badge) {
				SDL_BlitSurface(badge, NULL, canvas,
								&(SDL_Rect){center_x - badge_size / 2, content_y});
				SDL_FreeSurface(badge);
				content_y += badge_size + SCALE1(6);
			}

			int max_text_width = screen->w - SCALE1(PADDING * 6);
			content_y = GFX_blitWrappedText(font.medium, a->title, max_text_width, 2,
											COLOR_WHITE, canvas, content_y);
			content_y += SCALE1(2);
			content_y = GFX_blitWrappedText(font.small, a->description, max_text_width, 0,
											COLOR_WHITE, canvas, content_y);
			content_y += SCALE1(4);

			char line[96];
			snprintf(line, sizeof(line), a->points == 1 ? "1 point" : "%u points", a->points);
			rat_detail_meta_line(canvas, line, &content_y);

			if (a->unlock_time > 0) {
				struct tm tm;
				localtime_r(&a->unlock_time, &tm);
				char tbuf[64];
				strftime(tbuf, sizeof(tbuf), "%B %d %Y, %I:%M%p", &tm);
				snprintf(line, sizeof(line),
						 a->state == RAT_ACH_PENDING ? "Unlocked %s (pending sync)" : "Unlocked %s",
						 tbuf);
				rat_detail_meta_line(canvas, line, &content_y);
			}

			if (a->rarity > 0) {
				snprintf(line, sizeof(line), "%.2f%% unlock rate", a->rarity);
				rat_detail_meta_line(canvas, line, &content_y);
			}

			const char* type_str = NULL;
			switch (a->type) {
			case RC_ACHIEVEMENT_TYPE_MISSABLE:
				type_str = "[Missable]";
				break;
			case RC_ACHIEVEMENT_TYPE_PROGRESSION:
				type_str = "[Progression]";
				break;
			case RC_ACHIEVEMENT_TYPE_WIN:
				type_str = "[Win Condition]";
				break;
			default:
				break;
			}
			if (type_str)
				rat_detail_meta_line(canvas, type_str, &content_y);

			// blit the block vertically centered above the hint bar
			int avail_h = screen->h - SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN);
			int dest_y = (avail_h - content_y) / 2;
			if (dest_y < SCALE1(PADDING))
				dest_y = SCALE1(PADDING);
			SDL_BlitSurface(canvas, &(SDL_Rect){0, 0, canvas->w, content_y},
							screen, &(SDL_Rect){0, dest_y});

			UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	SDL_FreeSurface(canvas);
}

// ---------------- achievements screen (one game) ----------------

// Render a rich-pill subtitle (row 2), clipped to the pill's text width.
// Shared by the achievements list and the games list.
static void rat_render_subtitle(SDL_Surface* screen, const char* sub,
								SDL_Color color, ListItemRichPos pos) {
	SDL_Surface* s = TTF_RenderUTF8_Blended(font.small, sub, color);
	if (s) {
		SDL_Rect src = {0, 0, s->w > pos.text_max_width ? pos.text_max_width : s->w, s->h};
		SDL_BlitSurface(s, &src, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
		SDL_FreeSurface(s);
	}
}

static void rat_show_achievements(SDL_Surface* screen, const RAT_Game* game) {
	RAT_Achievement* achs = NULL;
	int count = RAT_loadAchievements(game, &achs);
	RAT_sortAchievements(achs, count); // honour the sort-order setting

	SDL_Surface** badges = NULL;
	if (count > 0) {
		badges = (SDL_Surface**)calloc(count, sizeof(SDL_Surface*));
		for (int i = 0; i < count && badges; i++) {
			char p[512];
			// unlocked/pending show the colored badge, locked the grey one
			rat_badge_path(achs[i].badge_name, achs[i].state == RAT_ACH_LOCKED, p, sizeof(p));
			badges[i] = UI_loadRoundedImage(p, RAT_THUMB_SIZE, SCALE1(8));
			if (!badges[i]) { // fall back to the other variant if only one is cached
				rat_badge_path(achs[i].badge_name, achs[i].state != RAT_ACH_LOCKED, p, sizeof(p));
				badges[i] = UI_loadRoundedImage(p, RAT_THUMB_SIZE, SCALE1(8));
			}
		}
	}

	const SDL_Color col_state_unlocked = {130, 220, 130, 255};
	const SDL_Color col_state_pending = {255, 200, 80, 255};
	const SDL_Color col_state_locked = {150, 150, 150, 255};
	const SDL_Color col_desc = {180, 180, 180, 255};

	int selected = 0, scroll = 0;
	int row_h = SCALE1(PILL_SIZE) * 3 / 2; // PillRich draws at 1.5x PILL_SIZE

	ListLayout layout = UI_calcListLayout(screen);
	int base_y = layout.list_y; // top of content, right under the menu bar
	// counts live in the menu bar title, so the list starts right under it —
	// together with the tightened bottom reserve this fits 3 rich-pill rows
	// on tg5040 (was 2)
	int list_top = base_y;
	// bottom area: wrapped description (settings style, up to 2 tiny lines)
	// above the hint bar
	int desc_area_h = TTF_FontHeight(font.tiny) * 2 + SCALE1(2);
	int bottom_reserve = SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN);
	int list_bottom_pad = bottom_reserve + desc_area_h + SCALE1(4);
	int rows_visible = (screen->h - list_top - list_bottom_pad) / row_h;
	if (rows_visible < 1)
		rows_visible = 1;

	char header_title[192];
	snprintf(header_title, sizeof(header_title), "%s (%d/%d)",
			 game->title, game->unlocked, game->total);

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
			rat_show_achievement_detail(screen, achs, count, selected);
			PAD_reset(); // the B that closed the detail view is still latched
			dirty = true;
		}

		if (selected < scroll)
			scroll = selected;
		if (selected >= scroll + rows_visible)
			scroll = selected - rows_visible + 1;

		if (dirty) {
			GFX_clear(screen);
			UI_renderMenuBar(screen, header_title);

			for (int r = 0; r < rows_visible && scroll + r < count; r++) {
				int i = scroll + r;
				int y = list_top + r * row_h;
				bool sel = (i == selected);
				bool has_image = (badges && badges[i] != NULL);

				const char* state_txt =
					achs[i].state == RAT_ACH_UNLOCKED ? "Unlocked" : achs[i].state == RAT_ACH_PENDING ? "Pending sync"
																									  : "Locked";
				SDL_Color state_col =
					achs[i].state == RAT_ACH_UNLOCKED ? col_state_unlocked : achs[i].state == RAT_ACH_PENDING ? col_state_pending
																											  : col_state_locked;
				char sub[64];
				snprintf(sub, sizeof(sub), "%s - %u pts", state_txt, achs[i].points);

				char truncated[256];
				ListItemRichPos pos = UI_renderListItemPillRich(
					screen, &layout, achs[i].title, sub, truncated, y, sel, has_image, 0);

				if (has_image) {
					SDL_Rect dst = {pos.image_x, pos.image_y, pos.image_size, pos.image_size};
					SDL_BlitScaled(badges[i], NULL, screen, &dst);
				}

				// Title (row 1) — pass full title, not truncated; the pill's
				// clip rect (via UI_renderListItemText) cuts it off.
				UI_renderListItemText(screen, NULL, achs[i].title, font.medium,
									  pos.title_x, pos.title_y, pos.text_max_width, sel);

				// Subtitle (row 2), in the achievement's state color
				rat_render_subtitle(screen, sub, state_col, pos);
			}

			UI_renderScrollIndicators(screen, scroll, rows_visible, count);

			if (count == 0) {
				rat_render_text(screen, "No achievement data cached for this game",
								font.medium, col_desc, SCALE1(20), list_top);
			} else if (achs[selected].description[0]) {
				// right under the last row - fills the gap to the list and
				// stays clear of the bottom scroll indicator
				rat_render_desc_wrapped(screen, achs[selected].description, col_desc,
										screen->w - SCALE1(PADDING * 2),
										list_top + rows_visible * row_h + SCALE1(4));
			}

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

	// Load box art up front (one shot, not lazy) — the list is small and
	// this keeps the render loop free of I/O.
	SDL_Surface** arts = NULL;
	if (count > 0) {
		arts = (SDL_Surface**)calloc(count, sizeof(SDL_Surface*));
		for (int i = 0; i < count && arts; i++) {
			char art_path[1024];
			if (rat_game_art_path(games[i].hash, art_path, sizeof(art_path)))
				arts[i] = UI_loadRoundedImage(art_path, RAT_THUMB_SIZE, SCALE1(8));
		}
	}

	const SDL_Color col_sub = {180, 180, 180, 255};
	const SDL_Color col_pending = {255, 200, 80, 255};

	int selected = 0, scroll = 0;
	int row_h = SCALE1(PILL_SIZE) * 3 / 2; // PillRich draws at 1.5x PILL_SIZE

	ListLayout layout = UI_calcListLayout(screen);
	int list_top = layout.list_y;
	int rows_visible =
		(screen->h - list_top - SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN + 8)) / row_h;
	if (rows_visible < 1)
		rows_visible = 1;

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
			UI_renderMenuBar(screen, "Achievements");

			for (int r = 0; r < rows_visible && scroll + r < count; r++) {
				int i = scroll + r;
				int y = list_top + r * row_h;
				bool sel = (i == selected);
				bool has_image = (arts && arts[i] != NULL);

				char sub[96];
				if (games[i].pending > 0)
					snprintf(sub, sizeof(sub), "%d/%d unlocked - %d pending sync",
							 games[i].unlocked, games[i].total, games[i].pending);
				else
					snprintf(sub, sizeof(sub), "%d/%d unlocked", games[i].unlocked, games[i].total);

				char truncated[256];
				ListItemRichPos pos = UI_renderListItemPillRich(
					screen, &layout, games[i].title, sub, truncated, y, sel, has_image, 0);

				if (has_image) {
					SDL_Rect dst = {pos.image_x, pos.image_y, pos.image_size, pos.image_size};
					SDL_BlitScaled(arts[i], NULL, screen, &dst);
				}

				// Title (row 1) — pass full title, not truncated; the pill's
				// clip rect (via UI_renderListItemText) cuts it off.
				UI_renderListItemText(screen, NULL, games[i].title, font.medium,
									  pos.title_x, pos.title_y, pos.text_max_width, sel);

				// Subtitle (row 2), yellow while sync is pending
				SDL_Color sub_col = games[i].pending > 0 ? col_pending : col_sub;
				rat_render_subtitle(screen, sub, sub_col, pos);
			}

			UI_renderScrollIndicators(screen, scroll, rows_visible, count);

			if (count == 0)
				rat_render_text(screen,
								"No cached games. Play online once or run Download all game data.",
								font.small, col_sub, SCALE1(20), list_top);

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
