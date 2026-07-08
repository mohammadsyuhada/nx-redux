#include "ui_splash.h"
#include "api.h"
#include "defines.h"

void UI_showSplashScreen(SDL_Surface* screen, const char* title) {
	GFX_clear(screen);

	SDL_Surface* title_text = TTF_RenderUTF8_Blended(font.large, title, COLOR_WHITE);
	if (title_text) {
		SDL_BlitSurface(title_text, NULL, screen, &(SDL_Rect){(screen->w - title_text->w) / 2, screen->h / 2 - title_text->h});
		SDL_FreeSurface(title_text);
	}

	SDL_Surface* loading = TTF_RenderUTF8_Blended(font.small, "Loading...", COLOR_GRAY);
	if (loading) {
		SDL_BlitSurface(loading, NULL, screen, &(SDL_Rect){(screen->w - loading->w) / 2, screen->h / 2 + SCALE1(4)});
		SDL_FreeSurface(loading);
	}

	GFX_flip(screen);
}
