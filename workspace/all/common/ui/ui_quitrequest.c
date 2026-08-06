#include "ui_quitrequest.h"
#include "ui_confirmdialog.h"
#include "api.h"
#include "defines.h"

void UI_handleQuitRequest(SDL_Surface* screen, bool* quit, bool* dirty,
						  const char* title, const char* subtitle) {
	// MENU + SELECT requests quit — the same combo minarch uses to exit a game
	// (ma_input.c), reused here to exit an app / pak tool. Edge-triggered on the
	// moment the chord completes so it fires once (not once per held frame) and
	// beats keymon's 500 ms MENU long-press OSD toggle.
	if (!((PAD_isPressed(BTN_MENU) && PAD_justPressed(BTN_SELECT)) ||
		  (PAD_justPressed(BTN_MENU) && PAD_isPressed(BTN_SELECT))))
		return;

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
}
