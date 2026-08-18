#include <string.h>
#include <time.h>
#include <msettings.h>
#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "ui_menubar.h"
#include "ui_toast.h"
#include "module_player.h"
#include "settings.h"
#include "ui_main.h"
#include "ui_confirmdialog.h"
#include "ui_music.h"
#include "ui_radio.h"
#include "player.h"
#include "radio.h"
#include "spectrum.h"
#include "background.h"

static bool autosleep_disabled = false;
static uint32_t last_input_time = 0;

// Screen off hint state
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;

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
	last_input_time = SDL_GetTicks();
	screen_off_hint_active = false;
	show_controls_help = false;
	overlay_buttons_were_active = false;
	overlay_release_time = 0;
}

// app_state of the music Now Playing page (see module_player.c): the only
// page where a MENU tap still opens the controls-help modal directly.
#define APP_STATE_MUSIC_PLAYING 2

// Run platform power management (power button, autosleep, volume/brightness
// indicators). Must run every frame — including while an overlay (context menu,
// controls help) is open — or the power button and autosleep go dead until the
// overlay is dismissed.
static void pump_power(GlobalInputResult* result, IndicatorType* show_setting) {
	bool dirty_tmp = result->dirty;
	IndicatorType indicator = (IndicatorType)*show_setting;
	PWR_update(&dirty_tmp, &indicator, NULL, NULL);
	*show_setting = (int)indicator;
	if (dirty_tmp && !result->dirty)
		result->dirty = true;
}

GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, IndicatorType* show_setting, int app_state,
												 const ContextMenuItem* menu_items, int menu_count) {
	GlobalInputResult result = {false, false, false, 0};

	// Poll USB HID events (earphone buttons)
	USBHIDEvent hid_event;
	while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
		if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
			// Check what's currently playing and handle accordingly
			// Check radio first (takes priority when streaming)
			RadioState radio_state = Radio_getState();
			PlayerState player_state = Player_getState();

			if (radio_state == RADIO_STATE_PLAYING || radio_state == RADIO_STATE_BUFFERING) {
				// Radio is streaming - stop it
				Radio_stop();
				result.dirty = true;
				result.input_consumed = true;
			} else if (player_state == PLAYER_STATE_PLAYING || player_state == PLAYER_STATE_PAUSED) {
				// Music player is active - toggle pause
				Player_togglePause();
				result.dirty = true;
				result.input_consumed = true;
			} else {
				// Nothing playing - check if we can resume radio
				const char* last_url = Radio_getCurrentUrl();
				if (last_url && last_url[0] != '\0') {
					// Resume last radio station
					Radio_play(last_url);
					result.dirty = true;
					result.input_consumed = true;
				}
			}
		} else if (hid_event == USB_HID_EVENT_NEXT_TRACK || hid_event == USB_HID_EVENT_PREV_TRACK) {
			// Next/previous track - check what's currently active
			RadioState radio_state = Radio_getState();

			if (radio_state == RADIO_STATE_PLAYING || radio_state == RADIO_STATE_BUFFERING || radio_state == RADIO_STATE_CONNECTING) {
				// Radio is active - switch stations
				RadioStation* stations;
				int station_count = Radio_getStations(&stations);
				if (station_count > 1) {
					// Find current station index
					int current_idx = Radio_findCurrentStationIndex();
					if (current_idx < 0)
						continue;
					// Calculate new index
					int new_idx;
					if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
						new_idx = (current_idx + 1) % station_count;
					} else {
						new_idx = (current_idx - 1 + station_count) % station_count;
					}
					// Switch to new station
					Radio_stop();
					Radio_play(stations[new_idx].url);
					result.dirty = true;
					result.input_consumed = true;
				}
			} else if (PlayerModule_isActive()) {
				// Music player is active - next/previous song
				if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
					PlayerModule_nextTrack();
				} else {
					PlayerModule_prevTrack();
				}
				result.dirty = true;
				result.input_consumed = true;
			}
		}
	}

	// Volume/brightness/color-temp combos are handled globally by keymon and by
	// PWR_update (Select + Vol = brightness, Start + Vol = color temp, Vol alone
	// = volume). We don't consume input or return early here — let PWR_update
	// detect the volume button press and set show_setting to display the UI.

	// Quitting is done via the context menu's "Quit App" item (every page has
	// one) or B from the main menu — the old MENU+SELECT chord was removed.

	// Context menu (opened on a MENU tap below): consumes all input while open.
	if (ContextMenu_isOpen()) {
		// Keep the MENU-tap state machine ticking so it doesn't fire on stale
		// state after close (mirrors nextui's main loop).
		PAD_tappedMenu(SDL_GetTicks());

		ContextMenuResult cmr = ContextMenu_handleInput();
		if (cmr.action == CONTEXTMENU_SELECTED) {
			if (cmr.id == CTX_ID_QUIT) {
				result.should_quit = true;
			} else if (cmr.id == CTX_ID_CONTROLS) {
				show_controls_help = true;
			} else {
				result.context_id = cmr.id;
			}
			result.dirty = true;
		} else if (cmr.action == CONTEXTMENU_CANCEL) {
			result.dirty = true;
		} else if (PAD_justRepeated(BTN_UP) || PAD_justRepeated(BTN_DOWN)) {
			// Selection moved: redraw the overlay (main surface is untouched)
			ContextMenu_render(screen);
			PLAT_GPU_Flip();
		}
		pump_power(&result, show_setting); // keep power/autosleep alive while open
		result.input_consumed = true;
		return result;
	}

	// MENU tap: on the music Now Playing page it toggles the controls-help
	// modal (so lyric/visualizer shortcuts stay discoverable); everywhere else
	// it opens the page's context menu. A long MENU press is the OSD (handled
	// globally by keymon); PAD_tappedMenu only fires on a short release, so
	// the two don't collide.
	if (PAD_tappedMenu(SDL_GetTicks())) {
		if (show_controls_help || app_state == APP_STATE_MUSIC_PLAYING) {
			show_controls_help = !show_controls_help;
			GFX_clearLayers(LAYER_SCROLLTEXT);
			PLAT_clearLayers(LAYER_SPECTRUM);
			PLAT_clearLayers(LAYER_PLAYTIME);
			PLAT_GPU_Flip();
			PlayTime_clear();
		} else if (menu_count > 0) {
			ContextMenu_open((ContextMenuItem*)menu_items, menu_count);
			// Clear text/status GPU layers so they don't float above the
			// menu's dim backdrop; they re-render after close.
			// (LAYER_PLAYTIME shares its plane with LAYER_PODCAST_PROGRESS.)
			GFX_clearLayers(LAYER_SCROLLTEXT);
			PLAT_clearLayers(LAYER_SPECTRUM);
			PLAT_clearLayers(LAYER_PLAYTIME);
			PLAT_clearLayers(LAYER_BUFFER);
			PlayTime_clear();
			ContextMenu_render(screen);
			PLAT_GPU_Flip();
		}
		pump_power(&result, show_setting);
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
			pump_power(&result, show_setting);
			result.input_consumed = true;
			result.dirty = true;
			return result;
		}
		// Dialog is shown, consume input and render (covers entire screen)
		render_controls_help(screen, app_state);
		GFX_flip(screen);
		pump_power(&result, show_setting); // keep power/autosleep alive while open
		result.input_consumed = true;
		return result;
	}

	// Non-overlay path: the caller runs ModuleCommon_PWR_update this frame, so
	// don't pump power again here (that double-processed the power button and
	// indicator timers). Only the overlay early-returns above pump, because the
	// module skips its own PWR_update on a consumed frame.
	return result;
}

void ModuleCommon_ctxAdd_impl(ContextMenuItem* items, int* count, int cap, const char* label, int id) {
	// Bound against BOTH the caller's array capacity and the widget's copy cap.
	if (cap > CONTEXTMENU_MAX_ITEMS)
		cap = CONTEXTMENU_MAX_ITEMS;
	if (*count >= cap)
		return;
	snprintf(items[*count].label, CONTEXTMENU_MAX_TEXT, "%s", label);
	items[*count].id = id;
	(*count)++;
}

void ModuleCommon_setAutosleepDisabled(bool disabled) {
	if (disabled && !autosleep_disabled) {
		PWR_disableAutosleep();
		autosleep_disabled = true;
	} else if (!disabled && autosleep_disabled) {
		// Don't re-enable autosleep if background audio is still playing
		if (!Background_isPlaying()) {
			PWR_enableAutosleep();
			autosleep_disabled = false;
		}
	}
}

bool ModuleCommon_isScreenOffHintActive(void) {
	return screen_off_hint_active;
}

void ModuleCommon_startScreenOffHint(void) {
	screen_off_hint_active = true;
	screen_off_hint_start = SDL_GetTicks();
	screen_off_hint_start_wallclock = time(NULL);
}

void ModuleCommon_resetScreenOffHint(void) {
	screen_off_hint_active = false;
}

void ModuleCommon_recordInputTime(void) {
	last_input_time = SDL_GetTicks();
}

bool ModuleCommon_checkAutoScreenOffTimeout(void) {
	if (screen_off_hint_active)
		return false;
	uint32_t screen_timeout_ms = Settings_getScreenOffTimeout() * 1000;
	if (screen_timeout_ms > 0 && SDL_GetTicks() - last_input_time >= screen_timeout_ms) {
		ModuleCommon_startScreenOffHint();
		return true;
	}
	return false;
}

bool ModuleCommon_processScreenOffHintTimeout(void) {
	if (!screen_off_hint_active)
		return false;
	uint32_t now = SDL_GetTicks();
	time_t now_wallclock = time(NULL);
	bool timeout_sdl = (now - screen_off_hint_start >= SCREEN_OFF_HINT_DURATION_MS);
	bool timeout_wallclock = (now_wallclock - screen_off_hint_start_wallclock >= (SCREEN_OFF_HINT_DURATION_MS / 1000));
	if (timeout_sdl || timeout_wallclock) {
		screen_off_hint_active = false;
		PLAT_enableBacklight(0);
		return true;
	}
	return false;
}

void ModuleCommon_quit(void) {
	// Ensure autosleep is re-enabled
	if (autosleep_disabled) {
		PWR_enableAutosleep();
		autosleep_disabled = false;
	}

	// Clear all GPU layers
	GFX_clearLayers(LAYER_SCROLLTEXT);
	PLAT_clearLayers(LAYER_SPECTRUM);
	PLAT_clearLayers(LAYER_PLAYTIME);
	PLAT_clearLayers(LAYER_BUFFER);
}


void ModuleCommon_PWR_update(bool* dirty, IndicatorType* show_setting) {
	// Track overlay-triggering buttons for auto-hide (check BEFORE PWR_update):
	// SELECT = brightness, START = color temp, PLUS/MINUS = volume. START must be
	// here too or the color-temp indicator never force-hides after release.
	bool overlay_buttons_active = PAD_isPressed(BTN_PLUS) || PAD_isPressed(BTN_MINUS) || PAD_isPressed(BTN_SELECT) || PAD_isPressed(BTN_START);

	if (overlay_buttons_were_active && !overlay_buttons_active) {
		// Buttons just released - start timer
		overlay_release_time = SDL_GetTicks();
	}

	// Call platform PWR_update (convert int* to expected types)
	bool dirty_bool = *dirty != 0;
	IndicatorType indicator = (IndicatorType)*show_setting;
	PWR_update(&dirty_bool, &indicator, NULL, NULL);
	*dirty = dirty_bool ? 1 : 0;
	*show_setting = (int)indicator;

	// Redraw when the menu-bar wifi/bt status changes (the pages only render
	// on input, so without this the wifi icon stays stale until a keypress)
	if (UI_statusBarChanged())
		*dirty = 1;

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

	// Check for pending audio device changes (set from inotify thread)
	Player_update();

	// Advance whatever is playing in the background (track-ended auto-advance,
	// periodic progress save). Every module's loop calls this, so background
	// playback keeps progressing on any page — not just the main menu. It's a
	// no-op when nothing is backgrounded (active_bg == BG_NONE).
	Background_tick();
}
