#include "ui_menubar.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

int UI_renderMenuBar(SDL_Surface* screen, const char* title) {
	// Semi-transparent bar background (cached between calls)
	static SDL_Surface* menu_bar = NULL;
	int bar_h = SCALE1(BUTTON_SIZE) + SCALE1(BUTTON_MARGIN * 2);
	if (!UI_getScrim(&menu_bar, screen->w, bar_h))
		return 0;
	SDL_BlitSurface(menu_bar, NULL, screen, &(SDL_Rect){0, 0});

	// Hardware group (right side)
	int ow = GFX_blitHardwareGroup(screen, PWR_getShowSetting());

	// Title text (left side, no pill)
	if (title && title[0]) {
		int max_title_w = screen->w - ow - SCALE1(PADDING * 2);
		char truncated[256];
		GFX_truncateText(font.small, title, truncated, max_title_w, 0);

		SDL_Surface* text = TTF_RenderUTF8_Blended(font.small, truncated, COLOR_GRAY);
		if (text) {
			int text_y = (bar_h - text->h) / 2;
			SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), text_y});
			SDL_FreeSurface(text);
		}
	}

	return ow;
}

SDL_Surface* UI_captureMenuBar(SDL_Surface* screen) {
	int bar_h = SCALE1(BUTTON_SIZE) + SCALE1(BUTTON_MARGIN * 2);
	SDL_Surface* bar = SDL_CreateRGBSurfaceWithFormat(
		0, screen->w, bar_h, screen->format->BitsPerPixel,
		screen->format->format);
	if (!bar)
		return NULL;
	SDL_FillRect(bar, NULL, SDL_MapRGBA(bar->format, 0, 0, 0, 255));
	SDL_BlitSurface(screen, &(SDL_Rect){0, 0, screen->w, bar_h}, bar, NULL);
	return bar;
}

int UI_statusBarChanged(void) {
	static int was_online = -1;
	static int had_bt = -1;
	int is_online = PWR_isOnline();
	int has_bt = PLAT_btIsConnected();
	if (was_online == -1) {
		was_online = is_online;
		had_bt = has_bt;
		return 0;
	}
	int changed = (was_online != is_online) || (had_bt != has_bt);
	was_online = is_online;
	had_bt = has_bt;
	return changed;
}
