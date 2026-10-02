#ifndef UI_ACCENT_H
#define UI_ACCENT_H

#include <stdbool.h>
#include <stdint.h>

// The accent: the one theme colour every selection mark uses (the List pill and its text, the tab underline and focus
// plate, the game tile ring, Home's selected cards), plus the ink drawn on it.
//
// By default it follows the theme: the accent is Color 1 (THEME_COLOR1, opacity included, as the List pill always
// was) and its ink is Color 5 (THEME_COLOR5), both read live on every call, so a settings reload is picked up. The
// default theme makes that white with black ink.
//
// An override replaces it with an opaque 0xRRGGBB and the WCAG ink (UI_onAccentFor): a build with -DNX_ACCENT=0x...
// starts with one, and UI_setAccent sets one.

// SDL-free core (host-tested by tests/test_accent.c with -DUI_ACCENT_NO_SDL).

// WCAG 2.x relative luminance of rgb, 0..1.
double UI_relativeLuminance(uint32_t rgb);
// WCAG contrast ratio between two colours, 1..21.
double UI_contrastRatio(uint32_t a, uint32_t b);
// Black (0x000000) or white (0xFFFFFF), whichever contrasts more with rgb (a tie goes to black).
uint32_t UI_onAccentFor(uint32_t rgb);

// Override the theme colours with rgb (0xRRGGBB). Composed looks are cached: if this is ever called at runtime, flush
// the Home card, Grid tile and Row (Carousel/Backdrop) item caches too (only the tab plate re-keys on its own).
void UI_setAccent(uint32_t rgb);
// Whether an override is active; if so, *rgb receives it (rgb may be NULL).
bool UI_accentOverride(uint32_t* rgb);

#ifndef UI_ACCENT_NO_SDL
#include "sdl.h"

// The accent and its ink as SDL colours (the theme's alpha without an override, opaque with one).
SDL_Color UI_accent(void);
SDL_Color UI_onAccent(void);
// The accent as a screen-mapped colour for SDL_FillRect and the GFX_*Color blits: THEME_COLOR1 itself without an
// override (so those blits recover its packed RGBA), the override mapped for fmt otherwise.
Uint32 UI_accentMapped(const SDL_PixelFormat* fmt);
// The ink the same way: THEME_COLOR5 itself without an override, the WCAG ink mapped for fmt otherwise.
Uint32 UI_onAccentMapped(const SDL_PixelFormat* fmt);
#endif

#endif // UI_ACCENT_H
