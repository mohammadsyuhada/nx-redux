#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "module_common.h"
#include "module_iptv.h"
#include "iptv.h"
#include "iptv_curated.h"
#include "stream_probe.h"
#include "wifi.h"
#include "display_helper.h"
#include "ffplay_engine.h"
#include "ui_confirmdialog.h"
#include "ui_iptv.h"
#include "ui_icons.h"
#include "ui_listview.h"
#include "ui_loadingoverlay.h"
#include "ui_toast.h"

// Module states
typedef enum {
	IPTV_STATE_USER_CHANNELS,	  // Main screen: user's saved channels
	IPTV_STATE_CURATED_COUNTRIES, // Browse curated countries
	IPTV_STATE_CURATED_CHANNELS	  // Browse curated channels in a country
} IPTVModuleState;

// Context-menu item ids (user-channels page)
#define IPTV_CTX_BROWSE 1  // browse/manage curated channels (was Y)
#define IPTV_CTX_REMOVE 2  // remove the selected user channel (was X)
#define IPTV_CTX_REFRESH 3 // re-fetch the current browse screen from iptv-org

// One-time note that channels come from the public iptv-org index.
static bool disclaimer_shown = false;

// Curated browse state (user-channel + country selection/scroll live in the
// ListViews owned by ui_iptv.c - see IPTVUserChannels_view() and
// IPTVCuratedCountries_view())
static int curated_channel_selected = 0;
static int curated_channel_scroll = 0;
static const char* curated_selected_country_code = NULL;
static char curated_toast_message[128] = "";
static uint32_t curated_toast_time = 0;

// Toast for the user-channels (My Channels) screen — e.g. a playback failure.
static char user_toast_message[128] = "";
static uint32_t user_toast_time = 0;

// Confirmation dialog state
static bool show_confirm = false;
static int confirm_action_type = 0; // 0 = delete from main list, 1 = remove from browse
static int confirm_target_index = -1;
static char confirm_channel_name[IPTV_MAX_NAME] = "";
static char confirm_channel_url[IPTV_MAX_URL] = "";

// Sorted channel index mapping for alphabetical display
static int sorted_channel_indices[256];
static int sorted_channel_count = 0;

static void build_sorted_channel_indices(const char* country_code) {
	int sc = 0;
	const CuratedTVChannel* cs = IPTV_curated_get_channels(country_code, &sc);
	sorted_channel_count = (sc < 256) ? sc : 256;
	for (int i = 0; i < sorted_channel_count; i++)
		sorted_channel_indices[i] = i;
	// Insertion sort by name
	for (int i = 1; i < sorted_channel_count; i++) {
		int key = sorted_channel_indices[i];
		int j = i - 1;
		while (j >= 0 && strcasecmp(cs[sorted_channel_indices[j]].name, cs[key].name) > 0) {
			sorted_channel_indices[j + 1] = sorted_channel_indices[j];
			j--;
		}
		sorted_channel_indices[j + 1] = key;
	}
}

// Copy `name` into `out`, dropping any "[...]" tag segments (e.g. the
// "[Geo-blocked]" / "[Not 24/7]" status tags iptv-org puts in channel names)
// and collapsing the whitespace they leave behind. Used only for the saved
// channel name; the browser list keeps the tags as a heads-up.
static void strip_name_tags(const char* name, char* out, int out_sz) {
	int j = 0, depth = 0;
	for (int i = 0; name[i] && j < out_sz - 1; i++) {
		char c = name[i];
		if (c == '[') {
			depth++;
			continue;
		}
		if (c == ']') {
			if (depth > 0)
				depth--;
			continue;
		}
		if (depth > 0)
			continue;
		if (c == ' ' && j > 0 && out[j - 1] == ' ')
			continue; // collapse runs of spaces left by removed tags
		out[j++] = c;
	}
	while (j > 0 && out[j - 1] == ' ')
		j--; // trim trailing space
	out[j] = '\0';
}

// Fetch (with a blocking "Loading…" overlay) the iptv-org country list.
// Returns the loaded count, or -1 on failure.
static int browse_load_countries(SDL_Surface* screen, IndicatorType show_setting, bool force) {
	Wifi_ensureConnected(screen, show_setting);
	UI_renderLoadingOverlay(screen, "Online TV", "Loading channel list...");
	GFX_flip(screen);
	return IPTV_curated_loadCountries(force, NULL, NULL);
}

// Fetch (with overlay) one country's channels. Returns count, or -1 on failure.
static int browse_load_channels(SDL_Surface* screen, IndicatorType show_setting,
								const char* code, bool force) {
	Wifi_ensureConnected(screen, show_setting);
	UI_renderLoadingOverlay(screen, "Online TV", "Loading channels...");
	GFX_flip(screen);
	return IPTV_curated_loadCountryChannels(code, force, NULL, NULL);
}

// Load the country list and enter the curated-countries screen. On the first
// successful browse, surfaces the iptv-org disclaimer; on failure, an error
// toast (both shown on the countries screen, which now renders toasts).
static void enter_country_browse(SDL_Surface* screen, IndicatorType show_setting,
								 IPTVModuleState* state, bool force) {
	int n = browse_load_countries(screen, show_setting, force);
	if (n < 0) {
		snprintf(curated_toast_message, sizeof(curated_toast_message), "Couldn't load - check Wi-Fi");
	} else if (!disclaimer_shown) {
		snprintf(curated_toast_message, sizeof(curated_toast_message), "From iptv-org - availability varies");
		disclaimer_shown = true;
	} else {
		curated_toast_message[0] = '\0';
	}
	curated_toast_time = SDL_GetTicks();
	UI_listViewReset(IPTVCuratedCountries_view(),
					 IPTV_curated_get_country_count(),
					 IPTV_curated_get_countries());
	*state = IPTV_STATE_CURATED_COUNTRIES;
}

ModuleExitReason IPTVModule_run(SDL_Surface* screen) {
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	IPTVModuleState state = IPTV_STATE_USER_CHANNELS;

	show_confirm = false;

	while (1) {
		GFX_startFrame();
		PAD_poll();

		// Handle confirmation dialog
		if (show_confirm) {
			if (PAD_justPressed(BTN_A)) {
				if (confirm_action_type == 0) {
					// Delete from main list. Content changed: reset the view to
					// the rebuilt array (glide snap + marquee clear), then
					// restore the cursor clamped like the old code did.
					IPTV_removeUserChannel(confirm_target_index);
					const IPTVChannel* channels = IPTV_getUserChannels();
					int user_count = IPTV_getUserChannelCount();
					ListView* lv = IPTVUserChannels_view();
					int prev_selected = lv->selected;
					UI_listViewReset(lv, user_count, channels);
					if (user_count > 0)
						lv->selected = (prev_selected < user_count)
										   ? prev_selected
										   : user_count - 1;
				} else if (confirm_action_type == 1) {
					// Remove from curated browse
					IPTV_removeUserChannelByUrl(confirm_channel_url);
					snprintf(curated_toast_message, sizeof(curated_toast_message), "Removed: %s", confirm_channel_name);
					curated_toast_time = SDL_GetTicks();
				}
				show_confirm = false;
				dirty = 1;
				GFX_sync();
				continue;
			} else if (PAD_justPressed(BTN_B)) {
				show_confirm = false;
				dirty = 1;
				GFX_sync();
				continue;
			}
			// Render confirmation dialog (covers entire screen)
			UI_renderConfirmDialog(screen, "Remove Channel?", confirm_channel_name);
			GFX_flip(screen);
			GFX_sync();
			continue;
		}

		// Handle curated countries browsing
		if (state == IPTV_STATE_CURATED_COUNTRIES) {
			ContextMenuItem ctx_items[2];
			int ctx_count = 0;
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Refresh List", IPTV_CTX_REFRESH);
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

			GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, ctx_items, ctx_count);
			if (global.should_quit)
				return MODULE_EXIT_QUIT;
			if (global.context_id == IPTV_CTX_REFRESH)
				enter_country_browse(screen, show_setting, &state, true);
			if (global.input_consumed) {
				if (global.dirty)
					dirty = 1;
				GFX_sync();
				continue;
			}

			// The ListView owns navigation; the module switches on actions.
			ListView* v = IPTVCuratedCountries_view();
			ListViewAction act = UI_listViewHandleInput(v);
			switch (act.type) {
			case LISTVIEW_ACTIVATED: {
				GFX_clearLayers(LAYER_SCROLLTEXT);
				const CuratedTVCountry* countries = IPTV_curated_get_countries();
				const char* code = countries[act.index].code;
				int n = browse_load_channels(screen, show_setting, code, false);
				curated_selected_country_code = code;
				curated_channel_selected = 0;
				curated_channel_scroll = 0;
				build_sorted_channel_indices(curated_selected_country_code);
				if (n <= 0) {
					snprintf(curated_toast_message, sizeof(curated_toast_message),
							 n < 0 ? "Couldn't load channels" : "No channels for this country");
					curated_toast_time = SDL_GetTicks();
				} else {
					curated_toast_message[0] = '\0';
				}
				state = IPTV_STATE_CURATED_CHANNELS;
				dirty = 1;
				// Re-enter the loop so the channels screen renders next frame;
				// falling through would redraw the (old) countries screen and
				// consume `dirty`, leaving the channels list blank until input.
				continue;
			}
			case LISTVIEW_BACK:
				GFX_clearLayers(LAYER_SCROLLTEXT);
				state = IPTV_STATE_USER_CHANNELS;
				dirty = 1;
				continue; // render the user-channels screen next frame
			default:
				break;
			}
			if (UI_listViewBusy(v))
				dirty = 1;

			ModuleCommon_PWR_update(&dirty, &show_setting);
			if (dirty) {
				render_iptv_curated_countries(screen, show_setting,
											  curated_toast_message, curated_toast_time);
				GFX_flip(screen);
				dirty = 0;
				ModuleCommon_tickToast(curated_toast_message, curated_toast_time, &dirty);
			} else {
				UI_listViewTickIdle(v);
				GFX_sync();
			}
			continue;
		}

		// Handle curated channels browsing
		if (state == IPTV_STATE_CURATED_CHANNELS) {
			ContextMenuItem ctx_items[2];
			int ctx_count = 0;
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Refresh List", IPTV_CTX_REFRESH);
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

			GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, ctx_items, ctx_count);
			if (global.should_quit)
				return MODULE_EXIT_QUIT;
			if (global.context_id == IPTV_CTX_REFRESH) {
				browse_load_channels(screen, show_setting, curated_selected_country_code, true);
				build_sorted_channel_indices(curated_selected_country_code);
				dirty = 1;
			}
			if (global.input_consumed) {
				if (global.dirty)
					dirty = 1;
				GFX_sync();
				continue;
			}

			int channel_count = 0;
			const CuratedTVChannel* channels = IPTV_curated_get_channels(curated_selected_country_code, &channel_count);

			if (PAD_justRepeated(BTN_UP) && sorted_channel_count > 0) {
				curated_channel_selected = (curated_channel_selected > 0) ? curated_channel_selected - 1 : sorted_channel_count - 1;
				dirty = 1;
			} else if (PAD_justRepeated(BTN_DOWN) && sorted_channel_count > 0) {
				curated_channel_selected = (curated_channel_selected < sorted_channel_count - 1) ? curated_channel_selected + 1 : 0;
				dirty = 1;
			} else if (PAD_justPressed(BTN_A) && sorted_channel_count > 0) {
				int actual_idx = sorted_channel_indices[curated_channel_selected];
				const CuratedTVChannel* channel = &channels[actual_idx];
				if (IPTV_userChannelExists(channel->url)) {
					// Already added - confirm removal
					strncpy(confirm_channel_name, channel->name, IPTV_MAX_NAME - 1);
					confirm_channel_name[IPTV_MAX_NAME - 1] = '\0';
					strncpy(confirm_channel_url, channel->url, IPTV_MAX_URL - 1);
					confirm_channel_url[IPTV_MAX_URL - 1] = '\0';
					confirm_action_type = 1;
					show_confirm = true;
					dirty = 1;
				} else {
					// Save without the browser's status tags (e.g. "[Geo-blocked]").
					char clean_name[IPTV_MAX_NAME];
					strip_name_tags(channel->name, clean_name, sizeof(clean_name));
					if (IPTV_addUserChannel(clean_name, channel->url, channel->category, channel->logo, channel->decryption_key) >= 0) {
						// No toast on success: the [+] marker already shows the
						// channel was added.
						curated_toast_message[0] = '\0';
					} else {
						snprintf(curated_toast_message, sizeof(curated_toast_message), "Maximum %d channels reached", IPTV_MAX_USER_CHANNELS);
						curated_toast_time = SDL_GetTicks();
					}
				}
				dirty = 1;
			} else if (PAD_justPressed(BTN_B)) {
				curated_toast_message[0] = '\0';
				UI_clearToast();
				state = IPTV_STATE_CURATED_COUNTRIES;
				dirty = 1;
				continue;
			}

			ModuleCommon_PWR_update(&dirty, &show_setting);
			if (dirty) {
				render_iptv_curated_channels(screen, show_setting, curated_selected_country_code,
											 curated_channel_selected, &curated_channel_scroll,
											 sorted_channel_indices, sorted_channel_count,
											 curated_toast_message, curated_toast_time);
				GFX_flip(screen);
				dirty = 0;

				ModuleCommon_tickToast(curated_toast_message, curated_toast_time, &dirty);
			} else {
				GFX_sync();
			}
			continue;
		}

		// IPTV_STATE_USER_CHANNELS (main screen)
		const IPTVChannel* channels = IPTV_getUserChannels();
		int user_count = IPTV_getUserChannelCount();
		ListView* v = IPTVUserChannels_view();

		// Context menu for this page (MENU tap): browse the curated catalog,
		// remove the selected channel, quit.
		ContextMenuItem ctx_items[3];
		int ctx_count = 0;
		ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Browse Channels", IPTV_CTX_BROWSE);
		if (user_count > 0 && v->selected >= 0 && v->selected < user_count)
			ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Remove Channel", IPTV_CTX_REMOVE);
		ModuleCommon_ctxAdd(ctx_items, &ctx_count, "Quit App", CTX_ID_QUIT);

		GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, ctx_items, ctx_count);
		if (global.should_quit)
			return MODULE_EXIT_QUIT;
		if (global.context_id > 0) {
			switch (global.context_id) {
			case IPTV_CTX_BROWSE:
				enter_country_browse(screen, show_setting, &state, false);
				break;
			case IPTV_CTX_REMOVE:
				if (v->selected >= 0 && v->selected < user_count) {
					GFX_clearLayers(LAYER_SCROLLTEXT);
					strncpy(confirm_channel_name, channels[v->selected].name, IPTV_MAX_NAME - 1);
					confirm_channel_name[IPTV_MAX_NAME - 1] = '\0';
					confirm_target_index = v->selected;
					confirm_action_type = 0;
					show_confirm = true;
				}
				break;
			}
			dirty = 1;
		}
		if (global.input_consumed) {
			if (global.dirty)
				dirty = 1;
			GFX_sync();
			continue;
		}

		// The ListView owns navigation; the module switches on actions.
		ListViewAction act = UI_listViewHandleInput(v);
		switch (act.type) {
		case LISTVIEW_ACTIVATED:
			if (act.index >= 0 && act.index < user_count) {
				const IPTVChannel* ch = &channels[act.index];

				// Ensure WiFi, then show a loading screen instead of the black
				// frame while ffplay connects.
				Wifi_ensureConnected(screen, show_setting);
				UI_renderLoadingOverlay(screen, ch->name, "Connecting...");
				GFX_flip(screen);

				// Fast reachability probe: a dead/geo-blocked stream fails here
				// in a few seconds without a long black-screen hang.
				if (!Stream_probeReachable(ch->url)) {
					snprintf(user_toast_message, sizeof(user_toast_message),
							 "Unavailable - offline or geo-blocked");
					user_toast_time = SDL_GetTicks();
					GFX_clearLayers(LAYER_SCROLLTEXT);
					dirty = 1;
					break;
				}

				FfplayConfig config;
				memset(&config, 0, sizeof(config));
				config.source = FFPLAY_SOURCE_STREAM;
				config.is_stream = true;
				config.screen_width = screen->w;
				strncpy(config.path, ch->url, sizeof(config.path) - 1);
				strncpy(config.title, ch->name, sizeof(config.title) - 1);
				if (ch->decryption_key[0])
					strncpy(config.decryption_key, ch->decryption_key, sizeof(config.decryption_key) - 1);

				ModuleCommon_setAutosleepDisabled(true);
				int rc = FfplayEngine_play(&config);

				// TG5050: display recovery creates a new screen surface
				{
					SDL_Surface* ns = DisplayHelper_getReinitScreen();
					if (ns)
						screen = ns;
				}

				// Fallback: the stream answered HTTP but ffplay couldn't play it
				// (bad codec/data, token-gated segments, mid-stream drop). ffplay
				// exits 0 even on a fatal open error, so trust FfplayEngine_lastFailed
				// over the exit code.
				if (FfplayEngine_lastFailed() || (rc != 0 && rc != FFPLAY_EXIT_AUDIO_CHANGED)) {
					snprintf(user_toast_message, sizeof(user_toast_message),
							 "Couldn't play this channel");
					user_toast_time = SDL_GetTicks();
				}

				Icons_init();
				GFX_clearLayers(LAYER_SCROLLTEXT);
				dirty = 1;
			}
			break;
		case LISTVIEW_BACK:
			GFX_clearLayers(LAYER_SCROLLTEXT);
			return MODULE_EXIT_TO_MENU;
		case LISTVIEW_BUTTON:
			// Empty list: A opens the curated browser (advertised as A/MANAGE).
			// Browse/remove otherwise live in the context menu (MENU tap).
			if (act.btn == BTN_A && user_count == 0) {
				enter_country_browse(screen, show_setting, &state, false);
				dirty = 1;
				continue;
			}
			break;
		default:
			break;
		}
		if (UI_listViewBusy(v))
			dirty = 1;

		ModuleCommon_PWR_update(&dirty, &show_setting);
		if (dirty) {
			render_iptv_user_channels(screen, show_setting, user_toast_message, user_toast_time);
			GFX_flip(screen);
			dirty = 0;
			ModuleCommon_tickToast(user_toast_message, user_toast_time, &dirty);
		} else {
			UI_listViewTickIdle(v);
			GFX_sync();
		}
	}
}
