#include "ui_downloadprogress.h"
#include "api.h"
#include "defines.h"
#include <stdio.h>

void UI_renderDownloadProgress(SDL_Surface* screen, const UIDownloadProgress* info) {
	int hw = screen->w;
	int hh = screen->h;

	// Status message (centered, above bar)
	if (info->status) {
		SDL_Surface* status_text = TTF_RenderUTF8_Blended(font.medium, info->status, COLOR_WHITE);
		if (status_text) {
			SDL_BlitSurface(status_text, NULL, screen,
							&(SDL_Rect){(hw - status_text->w) / 2, hh / 2 - SCALE1(20)});
			SDL_FreeSurface(status_text);
		}
	}

	if (info->show_bar) {
		// Progress bar dimensions
		int bar_w = hw - SCALE1(PADDING * 8);
		int bar_h = SCALE1(12);
		int bar_x = SCALE1(PADDING * 4);
		int bar_y = hh / 2 + SCALE1(10);

		// Background (dark gray)
		SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
		SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 64, 64, 64));

		// Progress fill
		int prog_w = (bar_w * info->progress) / 100;
		if (prog_w > 0) {
			SDL_Rect prog_rect = {bar_x, bar_y, prog_w, bar_h};
			SDL_FillRect(screen, &prog_rect, THEME_COLOR2);
		}

		// Percentage text inside bar
		char pct_str[16];
		snprintf(pct_str, sizeof(pct_str), "%d%%", info->progress);
		SDL_Surface* pct_text = TTF_RenderUTF8_Blended(font.tiny, pct_str, COLOR_WHITE);
		if (pct_text) {
			int pct_x = bar_x + (bar_w - pct_text->w) / 2;
			int pct_y = bar_y + (bar_h - pct_text->h) / 2;
			SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){pct_x, pct_y});
			SDL_FreeSurface(pct_text);
		}

		// Detail text below bar
		if (info->detail && info->detail[0]) {
			SDL_Surface* detail_text = TTF_RenderUTF8_Blended(font.small, info->detail, COLOR_GRAY);
			if (detail_text) {
				SDL_BlitSurface(detail_text, NULL, screen,
								&(SDL_Rect){(hw - detail_text->w) / 2, bar_y + bar_h + SCALE1(6)});
				SDL_FreeSurface(detail_text);
			}
		}
	}
}
