#include <string.h>
#include <msettings.h>
#include "api.h"
#include "audio_manager.h"
#include "module_common.h"
#include "ui_confirmdialog.h"
#include "ui_quitrequest.h"
#include "ui_toast.h"
#include "ui_main.h"

static bool autosleep_disabled = false;

// Dialog states
static bool show_controls_help = false;

// Overlay state tracking - force hide after button release
static bool overlay_buttons_were_active = false;
static uint32_t overlay_release_time = 0;
#define OVERLAY_VISIBLE_AFTER_RELEASE_MS 800 // How long overlay stays visible after release
#define OVERLAY_FORCE_HIDE_DURATION_MS 500	 // How long to keep forcing hide

void ModuleCommon_tickToast(char* message, uint32_t toast_time, bool* dirty) {
	if (message[0] == '\0')
		return;
	if (SDL_GetTicks() - toast_time < TOAST_DURATION) {
		*dirty = 1;
	} else {
		message[0] = '\0';
		*dirty = 1;
	}
}

void ModuleCommon_init(void) {
	autosleep_disabled = false;
	show_controls_help = false;
	overlay_buttons_were_active = false;
	overlay_release_time = 0;
}

GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, IndicatorType* show_setting, int app_state) {
	GlobalInputResult result = {false, false, false};

	// Quit on MENU + SELECT via the shared confirm dialog — the same combo the
	// pak tools and in-game minarch use. UI_handleQuitRequest runs its own
	// blocking A/B dialog and only fires on the chord, so it's a no-op most
	// frames.
	{
		bool want_quit = false, dlg_dirty = false;
		UI_handleQuitRequest(screen, &want_quit, &dlg_dirty, "Quit Media Player?", NULL);
		if (want_quit) {
			result.should_quit = true;
			result.input_consumed = true;
			return result;
		}
		if (dlg_dirty) {
			result.dirty = true;
			result.input_consumed = true;
			return result;
		}
	}

	// MENU tap toggles the controls-help modal. A long MENU press is the OSD
	// (handled globally by keymon); PAD_tappedMenu only fires on a short release,
	// so the two don't collide.
	if (PAD_tappedMenu(SDL_GetTicks())) {
		show_controls_help = !show_controls_help;
		GFX_clearLayers(LAYER_SCROLLTEXT);
		PLAT_GPU_Flip();
		result.input_consumed = true;
		result.dirty = true;
		return result;
	}

	// Controls help modal — any button except MENU (the toggle above) closes it.
	if (show_controls_help) {
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B) || PAD_justPressed(BTN_X) ||
			PAD_justPressed(BTN_Y) || PAD_justPressed(BTN_START) || PAD_justPressed(BTN_SELECT) ||
			PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_DOWN) ||
			PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT) ||
			PAD_justPressed(BTN_L1) || PAD_justPressed(BTN_R1)) {
			show_controls_help = false;
			result.input_consumed = true;
			result.dirty = true;
			return result;
		}
		// Dialog is shown, consume input and render (covers entire screen)
		render_controls_help(screen, app_state);
		GFX_flip(screen);
		result.input_consumed = true;
		return result;
	}

	// Handle power management
	{
		bool dirty_before = result.dirty ? true : false;
		bool dirty_tmp = dirty_before;
		PWR_update(&dirty_tmp, show_setting, NULL, NULL);

		if (dirty_tmp && !dirty_before) {
			result.dirty = true;
		}
	}

	return result;
}

void ModuleCommon_setAutosleepDisabled(bool disabled) {
	if (disabled && !autosleep_disabled) {
		PWR_disableAutosleep();
		autosleep_disabled = true;
	} else if (!disabled && autosleep_disabled) {
		PWR_enableAutosleep();
		autosleep_disabled = false;
	}
}

void ModuleCommon_quit(void) {
	// Ensure autosleep is re-enabled
	if (autosleep_disabled) {
		PWR_enableAutosleep();
		autosleep_disabled = false;
	}

	// Clear GPU scroll text layer
	GFX_clearLayers(LAYER_SCROLLTEXT);
}

void ModuleCommon_PWR_update(bool* dirty, IndicatorType* show_setting) {
	// Poll for audio device changes (keeps AudioManager state current)
	AudioMgr_pollEvents();

	// Track overlay-triggering buttons for auto-hide (check BEFORE PWR_update):
	// SELECT = brightness, START = color temp, PLUS/MINUS = volume. START must be
	// here too or the color-temp indicator never force-hides after release.
	bool overlay_buttons_active = PAD_isPressed(BTN_PLUS) || PAD_isPressed(BTN_MINUS) || PAD_isPressed(BTN_SELECT) || PAD_isPressed(BTN_START);

	if (overlay_buttons_were_active && !overlay_buttons_active) {
		// Buttons just released - start timer
		overlay_release_time = SDL_GetTicks();
	}

	// Call platform PWR_update
	PWR_update(dirty, show_setting, NULL, NULL);

	// After visible period, force hide overlay
	if (overlay_release_time > 0) {
		uint32_t elapsed = SDL_GetTicks() - overlay_release_time;
		if (elapsed >= OVERLAY_VISIBLE_AFTER_RELEASE_MS) {
			// Visible period passed, now force hide
			*show_setting = INDICATOR_NONE;
			*dirty = 1;
			// Stop forcing after the duration
			if (elapsed >= OVERLAY_VISIBLE_AFTER_RELEASE_MS + OVERLAY_FORCE_HIDE_DURATION_MS) {
				overlay_release_time = 0;
			}
		}
	}

	overlay_buttons_were_active = overlay_buttons_active;
}
