#include "ui_downloadprogress.h"
#include "api.h"
#include "defines.h"
#include "ui_buttonhintbar.h"
#include "ui_font.h"
#include "ui_menubar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One centred line, cut with an ellipsis to max_w. The font is used straight away (UIFont pointers are
// only valid until the next UIFont_get).
static void drawCentredLine(SDL_Surface* screen, TTF_Font* f, const char* text, SDL_Color color, int y, int max_w) {
	if (!f || !text || !text[0])
		return;
	// GFX_truncateText's output is never longer than its input (it cuts on code points, then appends "...")
	char* buf = malloc(strlen(text) + 1);
	if (!buf)
		return;
	GFX_truncateText(f, text, buf, max_w, 0);
	SDL_Surface* s = GFX_renderText(f, buf, color);
	free(buf);
	if (s) {
		SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){(screen->w - s->w) / 2, y});
		SDL_FreeSurface(s);
	}
}

// A long-running tool page (LIST-LAYOUT §10.6 "Progress page"): the status line at the secondary size
// (0.8 x the list label, bold for the semi-bold weight), the bar under it with a gap of 1.0 x the status
// size, and the detail line (the current item) at the caption size with 0.75 x the status size above it.
// No headline-size text. The column is centred between the page title strip (+8 dp) and the hint bar.
void UI_renderDownloadProgress(SDL_Surface* screen, const UIDownloadProgress* info) {
	int hw = screen->w;
	int status_px = UI_textRolePx(UI_TEXT_SECONDARY);
	// one side margin for the text column and the bar
	int margin = SCALE1(PADDING * 4);
	int text_w = hw - margin * 2;

	// the line heights first (each font fetched and dropped before the next)
	TTF_Font* f = UI_textRole(UI_TEXT_SECONDARY, true);
	int status_h = f ? TTF_FontHeight(f) : status_px;
	f = UI_textRole(UI_TEXT_CAPTION, false);
	int detail_h = f ? TTF_FontHeight(f) : UI_textRolePx(UI_TEXT_CAPTION);

	int bar_w = text_w;
	int bar_h = SCALE1(12);
	int bar_x = margin;
	int bar_gap = status_px;
	int detail_gap = (status_px * 3 + 2) / 4;

	bool has_status = info->status && info->status[0];
	// the detail slot is kept while a bar shows, so the column does not jump when the detail text comes and goes
	bool has_detail = info->show_bar && info->detail;

	int content_h = has_status ? status_h : 0;
	if (info->show_bar)
		content_h += (has_status ? bar_gap : 0) + bar_h;
	if (has_detail)
		content_h += detail_gap + detail_h;

	int band_top = UI_menuBarHeight() + NX_DP(8);
	int band_bottom = UI_buttonHintBarTop(screen->h);
	int y = band_top + (band_bottom - band_top - content_h) / 2;
	if (y < band_top)
		y = band_top;

	// Status line
	if (has_status) {
		drawCentredLine(screen, UI_textRole(UI_TEXT_SECONDARY, true), info->status, COLOR_WHITE, y, text_w);
		y += status_h + (info->show_bar ? bar_gap : 0);
	}

	if (info->show_bar) {
		int progress = info->progress < 0 ? 0 : (info->progress > 100 ? 100 : info->progress);
		int bar_y = y;

		// Background (dark gray)
		SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
		SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 64, 64, 64));

		// Progress fill
		int prog_w = (bar_w * progress) / 100;
		if (prog_w > 0) {
			SDL_Rect prog_rect = {bar_x, bar_y, prog_w, bar_h};
			GFX_fillRectColor(screen, &prog_rect, THEME_COLOR2);
		}

		// Percentage text inside bar
		char pct_str[16];
		snprintf(pct_str, sizeof(pct_str), "%d%%", progress);
		SDL_Surface* pct_text = GFX_renderText(font.tiny, pct_str, COLOR_WHITE);
		if (pct_text) {
			int pct_x = bar_x + (bar_w - pct_text->w) / 2;
			int pct_y = bar_y + (bar_h - pct_text->h) / 2;
			SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){pct_x, pct_y});
			SDL_FreeSurface(pct_text);
		}
		y = bar_y + bar_h;

		// Detail line (the current item) below the bar
		if (has_detail) {
			y += detail_gap;
			drawCentredLine(screen, UI_textRole(UI_TEXT_CAPTION, false), info->detail, COLOR_GRAY, y, text_w);
		}
	}
}
