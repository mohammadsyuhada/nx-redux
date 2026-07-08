#include "ui_quitrequest.h"
#include "ui_confirmdialog.h"
#include "api.h"
#include "defines.h"

void UI_handleQuitRequest(SDL_Surface* screen, bool* quit, bool* dirty,
						  const char* title, const char* subtitle) {
	static uint32_t start_press_time = 0;

	if (PAD_justPressed(BTN_START))
		start_press_time = SDL_GetTicks();

	if (PAD_isPressed(BTN_START) && start_press_time &&
		SDL_GetTicks() - start_press_time >= 500) {
		start_press_time = 0;
		PAD_reset();

		int confirmed = 0;
		int done = 0;
		while (!done) {
			GFX_startFrame();
			PAD_poll();
			if (PAD_justPressed(BTN_A)) {
				confirmed = 1;
				done = 1;
			} else if (PAD_justPressed(BTN_B)) {
				done = 1;
			}
			UI_renderConfirmDialog(screen, title, subtitle);
			GFX_flip(screen);
		}
		PAD_reset();
		if (confirmed)
			*quit = true;
		*dirty = true;
		return;
	}

	if (!PAD_isPressed(BTN_START))
		start_press_time = 0;
}
