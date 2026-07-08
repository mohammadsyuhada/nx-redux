#include "ui_message.h"
#include "api.h"
#include "defines.h"

void UI_renderCenteredMessage(SDL_Surface* dst, const char* message) {
	SDL_Rect fullscreen_rect = {0, 0, dst->w, dst->h};
	GFX_blitMessage(font.large, (char*)message, dst, &fullscreen_rect);
}
