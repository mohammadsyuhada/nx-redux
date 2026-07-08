#include "ui_loadingoverlay.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

void UI_renderLoadingOverlay(SDL_Surface* dst, const char* title, const char* subtitle) {
	// Full-screen semi-transparent overlay (cached, same pattern as button hint bar)
	static SDL_Surface* overlay = NULL;
	if (!UI_getScrim(&overlay, dst->w, dst->h))
		return;
	SDL_BlitSurface(overlay, NULL, dst, &(SDL_Rect){0, 0});

	// Title: large font, white, centered
	int title_h = TTF_FontHeight(font.large);
	int total_h = title_h;
	if (subtitle)
		total_h += SCALE1(4) + TTF_FontHeight(font.small);
	int y = (dst->h - total_h) / 2;

	SDL_Rect title_rect = {0, y, dst->w, title_h};
	GFX_blitMessage(font.large, (char*)title, dst, &title_rect);

	// Subtitle: small font, gray, centered below title
	if (subtitle) {
		int sub_h = TTF_FontHeight(font.small);
		y += title_h + SCALE1(4);
		SDL_Rect sub_rect = {0, y, dst->w, sub_h};
		GFX_blitMessage(font.small, (char*)subtitle, dst, &sub_rect);
	}
}
