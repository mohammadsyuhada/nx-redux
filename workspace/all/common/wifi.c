#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "api.h"
#include "wifi.h"

// Optional app hook run before each "Connecting..." render, replacing the
// default scroll-layer clear (apps may need to reset their own scroll state)
static void (*pre_render_hook)(void) = NULL;

void Wifi_setConnectScreenHook(void (*hook)(void)) {
	pre_render_hook = hook;
}

static void render_wifi_message_screen(SDL_Surface* scr, IndicatorType show_setting, const char* msg) {
	if (pre_render_hook) {
		pre_render_hook();
	} else {
		GFX_clearLayers(LAYER_SCROLLTEXT);
	}
	GFX_clear(scr);
	int hw = scr->w;
	int hh = scr->h;
	SDL_Surface* text = GFX_renderText(font.medium, msg, COLOR_WHITE);
	if (text) {
		SDL_BlitSurface(text, NULL, scr, &(SDL_Rect){(hw - text->w) / 2, (hh - text->h) / 2});
		SDL_FreeSurface(text);
	}
	GFX_blitHardwareGroup(scr, show_setting);
	GFX_flip(scr);
}

// Check if WiFi is currently connected
bool Wifi_isConnected(void) {
	return PLAT_wifiEnabled() && PLAT_wifiConnected();
}

bool Wifi_ensureConnected(SDL_Surface* scr, IndicatorType show_setting) {
	if (Wifi_isConnected())
		return true;
	// Never auto-enable or auto-connect networking. Inform the user and bail.
	// Message distinguishes radio-off from on-but-not-connected so it's accurate.
	if (scr) {
		const char* msg = PLAT_wifiEnabled()
							  ? "WiFi is not connected. Connect in Settings."
							  : "WiFi is off. Enable it in Settings.";
		render_wifi_message_screen(scr, show_setting, msg);
		SDL_Delay(1200); // give the user a beat to read it
	}
	return false;
}
