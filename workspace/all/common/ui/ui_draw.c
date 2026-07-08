#include "ui_draw.h"
#include "api.h"
#include "defines.h"
#include <math.h>

void UI_fillRoundedRect(SDL_Surface* dst, int x, int y, int w, int h,
						int radius, uint32_t color) {
	int r = radius;
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	if (r < 0)
		r = 0;

	if (h - 2 * r > 0)
		SDL_FillRect(dst, &(SDL_Rect){x, y + r, w, h - 2 * r}, color);

	for (int dy = 0; dy < r; dy++) {
		int yd = r - dy;
		int inset = r - (int)sqrtf((float)(r * r - yd * yd));
		int row_w = w - 2 * inset;
		if (row_w <= 0)
			continue;
		SDL_FillRect(dst, &(SDL_Rect){x + inset, y + dy, row_w, 1}, color);
		SDL_FillRect(dst, &(SDL_Rect){x + inset, y + h - 1 - dy, row_w, 1}, color);
	}
}

SDL_Surface* UI_getScrim(SDL_Surface** cache, int w, int h) {
	SDL_Surface* scrim = *cache;
	if (!scrim || scrim->w != w || scrim->h != h) {
		if (scrim)
			SDL_FreeSurface(scrim);
		scrim = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32,
									 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
		if (scrim) {
			SDL_FillRect(scrim, NULL, SDL_MapRGBA(scrim->format, 0, 0, 0, 178));
			SDL_SetSurfaceBlendMode(scrim, SDL_BLENDMODE_BLEND);
		}
		*cache = scrim;
	}
	return scrim;
}

void UI_renderCenteredButtons(SDL_Surface* dst, int y, char** pairs) {
	int btn_sz = SCALE1(BUTTON_SIZE);
	int btn_gap = SCALE1(BUTTON_TEXT_GAP);
	int btn_margin = SCALE1(BUTTON_MARGIN);

	// Measure the row
	int widths[8];
	int count = 0;
	int total_w = 0;
	for (int i = 0; pairs[i * 2] && pairs[i * 2 + 1] && count < 8; i++) {
		int text_w, th;
		TTF_SizeUTF8(font.tiny, pairs[i * 2 + 1], &text_w, &th);
		widths[count] = btn_sz + btn_gap + text_w;
		total_w += (count > 0 ? btn_margin : 0) + widths[count];
		count++;
	}
	if (count == 0)
		return;

	// Render centered
	int bx = (dst->w - total_w) / 2;
	for (int i = 0; i < count; i++) {
		GFX_blitButton(pairs[i * 2 + 1], pairs[i * 2], dst, &(SDL_Rect){bx, y, 0, 0});
		bx += widths[i] + btn_margin;
	}
}
