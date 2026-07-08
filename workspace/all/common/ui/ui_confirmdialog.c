#include "ui_confirmdialog.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

void UI_renderConfirmDialog(SDL_Surface* dst, const char* title,
							const char* subtitle) {
	int padding_x = SCALE1(PADDING * 4);
	int content_w = dst->w - padding_x * 2;

	GFX_clearLayers(LAYER_SCROLLTEXT);
	SDL_FillRect(dst, NULL, SDL_MapRGB(dst->format, 0, 0, 0));

	int btn_sz = SCALE1(BUTTON_SIZE);

	int title_h = TTF_FontHeight(font.large);
	int total_h = title_h;
	if (subtitle)
		total_h += SCALE1(BUTTON_MARGIN) + TTF_FontHeight(font.small);
	total_h += SCALE1(BUTTON_MARGIN) + btn_sz;

	int y = (dst->h - total_h) / 2;

	// Title
	SDL_Rect title_rect = {padding_x, y, content_w, title_h};
	GFX_blitMessage(font.large, (char*)title, dst, &title_rect);

	// Subtitle (optional)
	if (subtitle) {
		int sub_h = TTF_FontHeight(font.small);
		y += title_h + SCALE1(BUTTON_MARGIN);
		SDL_Rect sub_rect = {padding_x, y, content_w, sub_h};
		GFX_blitMessage(font.small, (char*)subtitle, dst, &sub_rect);
		y += sub_h;
	} else {
		y += title_h;
	}

	// Buttons
	y += SCALE1(BUTTON_MARGIN);
	UI_renderCenteredButtons(dst, y, (char*[]){"B", "CANCEL", "A", "CONFIRM", NULL});
}
