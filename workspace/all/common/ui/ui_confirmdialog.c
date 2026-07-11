#include "ui_confirmdialog.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

#include <string.h>

#define CONFIRM_MAX_SUB_LINES 4

// Greedy word-wrap `text` to `max_w` using `font`, into up to
// CONFIRM_MAX_SUB_LINES lines. Returns the line count. (SDL_ttf 2.0.13's
// wrapped rendering left-aligns lines, so callers render each line centered.)
static int confirm_wrap_lines(TTF_Font* font, const char* text, int max_w,
							  char lines[CONFIRM_MAX_SUB_LINES][256]) {
	char work[512];
	snprintf(work, sizeof(work), "%s", text);

	int n = 0;
	lines[0][0] = '\0';
	char* save = NULL;
	for (char* tok = strtok_r(work, " ", &save); tok && n < CONFIRM_MAX_SUB_LINES;
		 tok = strtok_r(NULL, " ", &save)) {
		char cand[256];
		snprintf(cand, sizeof(cand), "%s%s%s", lines[n], lines[n][0] ? " " : "", tok);
		int w = 0;
		TTF_SizeUTF8(font, cand, &w, NULL);
		if (w <= max_w || !lines[n][0]) {
			snprintf(lines[n], sizeof(lines[n]), "%s", cand);
		} else if (n + 1 < CONFIRM_MAX_SUB_LINES) {
			n++;
			snprintf(lines[n], sizeof(lines[n]), "%s", tok);
		} else {
			break; // out of lines; drop the rest
		}
	}
	return lines[0][0] ? n + 1 : 0;
}

void UI_renderConfirmDialog(SDL_Surface* dst, const char* title,
							const char* subtitle) {
	int padding_x = SCALE1(PADDING * 4);
	int content_w = dst->w - padding_x * 2;

	GFX_clearLayers(LAYER_SCROLLTEXT);
	SDL_FillRect(dst, NULL, SDL_MapRGB(dst->format, 0, 0, 0));

	int btn_sz = SCALE1(BUTTON_SIZE);

	// Wrap the subtitle ourselves into centered lines so a long subtitle
	// (a) leaves room for the buttons below it and (b) stays horizontally
	// centered - SDL_ttf's own wrapped rendering left-aligns.
	char sub_lines[CONFIRM_MAX_SUB_LINES][256];
	int sub_line_count = 0;
	int sub_line_h = TTF_FontHeight(font.small);
	if (subtitle && subtitle[0])
		sub_line_count = confirm_wrap_lines(font.small, subtitle, content_w, sub_lines);

	int title_h = TTF_FontHeight(font.large);
	int total_h = title_h;
	if (sub_line_count)
		total_h += SCALE1(BUTTON_MARGIN) + sub_line_count * sub_line_h;
	total_h += SCALE1(BUTTON_MARGIN) + btn_sz;

	int y = (dst->h - total_h) / 2;

	// Title
	SDL_Rect title_rect = {padding_x, y, content_w, title_h};
	GFX_blitMessage(font.large, (char*)title, dst, &title_rect);
	y += title_h;

	// Subtitle (optional) - each line rendered horizontally centered
	if (sub_line_count) {
		y += SCALE1(BUTTON_MARGIN);
		for (int i = 0; i < sub_line_count; i++) {
			SDL_Surface* s = TTF_RenderUTF8_Blended(font.small, sub_lines[i], COLOR_WHITE);
			if (s) {
				SDL_BlitSurface(s, NULL, dst, &(SDL_Rect){(dst->w - s->w) / 2, y});
				SDL_FreeSurface(s);
			}
			y += sub_line_h;
		}
	}

	// Buttons
	y += SCALE1(BUTTON_MARGIN);
	UI_renderCenteredButtons(dst, y, (char*[]){"B", "CANCEL", "A", "CONFIRM", NULL});
}
