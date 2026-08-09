#include "ui_message.h"
#include "api.h"

void UI_showMessage(SDL_Surface* screen, const char* message, int hold_ms) {
	GFX_clear(screen);
	UI_renderCenteredMessage(screen, message);
	GFX_flip(screen);
	SDL_Delay(hold_ms);
}

void UI_renderCenteredMessage(SDL_Surface* dst, const char* message) {
	SDL_Rect fullscreen_rect = {0, 0, dst->w, dst->h};
	GFX_blitMessage(font.large, (char*)message, dst, &fullscreen_rect);
}
