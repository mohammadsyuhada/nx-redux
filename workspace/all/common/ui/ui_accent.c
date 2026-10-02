// ui_accent.c — the accent theme token. The core is SDL-free (host-tested by tests/test_accent.c, built with
// -DUI_ACCENT_NO_SDL); the SDL part reads the theme colours from api/config.
#include "ui_accent.h"
#include <math.h>

#ifdef NX_ACCENT
static bool overridden = true;
static uint32_t override_rgb = (uint32_t)(NX_ACCENT) & 0xFFFFFF;
#else
static bool overridden = false;
static uint32_t override_rgb = 0xFFFFFF;
#endif

static double channel(uint32_t v) {
	double c = (v & 0xFF) / 255.0;
	return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

double UI_relativeLuminance(uint32_t rgb) {
	return 0.2126 * channel(rgb >> 16) + 0.7152 * channel(rgb >> 8) + 0.0722 * channel(rgb);
}

double UI_contrastRatio(uint32_t a, uint32_t b) {
	double la = UI_relativeLuminance(a), lb = UI_relativeLuminance(b);
	return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

uint32_t UI_onAccentFor(uint32_t rgb) {
	rgb &= 0xFFFFFF;
	return UI_contrastRatio(rgb, 0x000000) >= UI_contrastRatio(rgb, 0xFFFFFF) ? 0x000000 : 0xFFFFFF;
}

void UI_setAccent(uint32_t rgb) {
	override_rgb = rgb & 0xFFFFFF;
	overridden = true;
}

bool UI_accentOverride(uint32_t* rgb) {
	if (overridden && rgb)
		*rgb = override_rgb;
	return overridden;
}

#ifndef UI_ACCENT_NO_SDL
#include "api.h"
#include "config.h"

static SDL_Color opaque(uint32_t rgb) {
	return (SDL_Color){(Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255};
}

SDL_Color UI_accent(void) {
	uint32_t rgb;
	return UI_accentOverride(&rgb) ? opaque(rgb) : uintToColour(THEME_COLOR1_255);
}

SDL_Color UI_onAccent(void) {
	uint32_t rgb;
	return UI_accentOverride(&rgb) ? opaque(UI_onAccentFor(rgb)) : uintToColour(THEME_COLOR5_255);
}

Uint32 UI_accentMapped(const SDL_PixelFormat* fmt) {
	uint32_t rgb;
	if (!UI_accentOverride(&rgb))
		return THEME_COLOR1;
	return SDL_MapRGBA(fmt, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255);
}

Uint32 UI_onAccentMapped(const SDL_PixelFormat* fmt) {
	uint32_t rgb;
	if (!UI_accentOverride(&rgb))
		return THEME_COLOR5;
	rgb = UI_onAccentFor(rgb);
	return SDL_MapRGBA(fmt, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255);
}
#endif
