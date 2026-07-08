#include "ui_emptystate.h"
#include "ui_draw.h"
#include "ui_icons.h"
#include "api.h"
#include "defines.h"

void UI_renderEmptyState(SDL_Surface* screen, const char* message,
						 const char* subtitle, const char* y_button_label) {
	int hw = screen->w;
	int hh = screen->h;

	int btn_sz = SCALE1(BUTTON_SIZE);

	// Calculate total height for vertical centering
	int icon_size = SCALE1(48);
	SDL_Surface* icon = Icons_getEmpty(false);
	int msg_h = TTF_FontHeight(font.medium);
	int sub_h = subtitle ? TTF_FontHeight(font.small) : 0;

	int total_h = 0;
	if (icon)
		total_h += icon_size + SCALE1(BUTTON_MARGIN);
	total_h += msg_h;
	if (subtitle)
		total_h += SCALE1(BUTTON_MARGIN) + sub_h;
	total_h += SCALE1(BUTTON_MARGIN) + btn_sz;

	int y = (hh - total_h) / 2;

	// Icon
	if (icon) {
		SDL_Rect src_rect = {0, 0, icon->w, icon->h};
		SDL_Rect dst_rect = {(hw - icon_size) / 2, y, icon_size, icon_size};
		SDL_BlitScaled(icon, &src_rect, screen, &dst_rect);
		y += icon_size + SCALE1(BUTTON_MARGIN);
	}

	// Message
	SDL_Surface* text1 = TTF_RenderUTF8_Blended(font.medium, message, COLOR_WHITE);
	if (text1) {
		SDL_BlitSurface(text1, NULL, screen, &(SDL_Rect){(hw - text1->w) / 2, y});
		SDL_FreeSurface(text1);
	}
	y += msg_h;

	// Subtitle
	if (subtitle) {
		y += SCALE1(BUTTON_MARGIN);
		SDL_Surface* text2 = TTF_RenderUTF8_Blended(font.small, subtitle, COLOR_GRAY);
		if (text2) {
			SDL_BlitSurface(text2, NULL, screen, &(SDL_Rect){(hw - text2->w) / 2, y});
			SDL_FreeSurface(text2);
		}
		y += sub_h;
	}

	// Buttons (centered, like confirm dialog)
	y += SCALE1(BUTTON_MARGIN);
	if (y_button_label)
		UI_renderCenteredButtons(screen, y, (char*[]){"B", "BACK", "Y", (char*)y_button_label, NULL});
	else
		UI_renderCenteredButtons(screen, y, (char*[]){"B", "BACK", NULL});
}
