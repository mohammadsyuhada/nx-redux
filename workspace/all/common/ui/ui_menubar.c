#include "ui_menubar.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

#include "ui_font.h"
#include "../ui_title_fit.h"
#include "../text_shape.h"

#include <stdio.h>
#include <string.h>

// The page title keeps the device's default size whatever the UI scale (like the main menu's tabs): UI_PAGE_TITLE_SP at
// NATIVE_SCALE, not FIXED_SCALE.
static TTF_Font* titleFont(void) {
	return UIFont_getPx(NX_NATIVE_SP(UI_PAGE_TITLE_SP), false);
}

int UI_menuBarHeight(void) {
	return SCALE1(BUTTON_SIZE) + SCALE1(BUTTON_MARGIN * 2);
}

int UI_pageTitleBaseline(void) {
	TTF_Font* f = titleFont();
	if (!f)
		return UI_menuBarHeight();
	// the title surface is TTF_FontHeight tall, centred in the strip (UI_renderPageTitleEx)
	return (UI_menuBarHeight() - TTF_FontHeight(f)) / 2 + TTF_FontAscent(f);
}

int UI_pageTitleBandTop(void) {
	int base = UI_pageTitleBaseline();
	TTF_Font* f = titleFont();
	if (!f)
		return base;
	// the deepest descender of the title's likely letters (and the separator's bar), from the glyph metrics
	int desc = 0;
	for (const char* c = "gjpqy|"; *c; c++) {
		int minx, maxx, miny, maxy, adv;
		if (TTF_GlyphMetrics(f, (Uint16)*c, &minx, &maxx, &miny, &maxy, &adv) == 0 && -miny > desc)
			desc = -miny;
	}
	return base + desc + NX_DP(2);
}

int UI_pageTitleX(void) {
	// the list label inset (LIST-LAYOUT §10.1): == UI_listTextX(), the shared list rows' text start
	return NX_DP(NX_LIST_INSET_DP);
}

// A title with Arabic text is drawn with the Arabic fallback face, which has no "|" (U+007C): when the part
// through the first " | " has no Arabic, it is drawn with the primary font and only the rest goes through the
// Arabic-aware path. Returns that part's length in bytes (0: draw the title whole).
static size_t titleSplit(const char* s) {
	if (!TextShape_hasArabic(s))
		return 0;
	const char* sep = strstr(s, UI_TITLE_SEPARATOR);
	if (!sep)
		return 0;
	size_t n = (size_t)(sep - s) + strlen(UI_TITLE_SEPARATOR);
	char head[256];
	if (n >= sizeof(head) || !s[n])
		return 0;
	memcpy(head, s, n);
	head[n] = '\0';
	return TextShape_hasArabic(head) ? 0 : n;
}

static int measureTitle(const char* s, void* ctx) {
	int w = 0;
	size_t n = titleSplit(s);
	if (n) {
		char head[256];
		memcpy(head, s, n);
		head[n] = '\0';
		int rest_w = 0;
		GFX_measureText((TTF_Font*)ctx, head, &w, NULL);
		GFX_measureText((TTF_Font*)ctx, s + n, &rest_w, NULL);
		return w + rest_w;
	}
	GFX_measureText((TTF_Font*)ctx, s, &w, NULL);
	return w;
}

// Blit a title piece at (x, y), with the optional drop shadow.
static void blitTitlePiece(SDL_Surface* dst, SDL_Surface* text, int x, int y, bool shadow) {
	if (shadow) {
		// over a picture: the cached grey surface tinted black at 60%, SCALE1(1) right and down (no
		// per-frame TTF render); the cache's surface is shared, so its mods are restored after
		Uint8 r, g, b, a;
		SDL_GetSurfaceColorMod(text, &r, &g, &b);
		SDL_GetSurfaceAlphaMod(text, &a);
		SDL_SetSurfaceColorMod(text, 0, 0, 0);
		SDL_SetSurfaceAlphaMod(text, (Uint8)(a * 153 / 255));
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){x + SCALE1(1), y + SCALE1(1)});
		SDL_SetSurfaceColorMod(text, r, g, b);
		SDL_SetSurfaceAlphaMod(text, a);
	}
	SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){x, y});
}

int UI_renderPageTitleEx(SDL_Surface* dst, int x, const char* title, const char* suffix, int max_w, bool shadow) {
	if (!dst || !title || !title[0])
		return 0;
	TTF_Font* f = titleFont();
	if (!f)
		return 0;
	if (max_w <= 0 || max_w > dst->w - x)
		max_w = dst->w - x;

	// The bar redraws every frame during list animation: re-fit only when the input changes. The key holds
	// the font's height and the scale too, since a reopened font can land at the same address.
	// The strings are keyed whole (length + hash): a long title can share the first 255 bytes with another.
	static char last_title[256], last_suffix[64], fitted[256];
	static UI_TitleKey last_title_key, last_suffix_key;
	static TTF_Font* last_font = NULL;
	static int last_max_w = -1, last_h = -1, last_scale = -1;
	const char* sfx = suffix ? suffix : "";
	int fh = TTF_FontHeight(f);
	UI_TitleKey title_key = UI_titleFit_key(title), suffix_key = UI_titleFit_key(sfx);
	if (f != last_font || max_w != last_max_w || fh != last_h || FIXED_SCALE != last_scale ||
		strncmp(title, last_title, sizeof(last_title)) != 0 || strncmp(sfx, last_suffix, sizeof(last_suffix)) != 0 ||
		!UI_titleFit_keyEq(title_key, last_title_key) || !UI_titleFit_keyEq(suffix_key, last_suffix_key)) {
		UI_titleFit(title, suffix && suffix[0] ? suffix : NULL, max_w, measureTitle, f, fitted, sizeof(fitted));
		snprintf(last_title, sizeof(last_title), "%s", title);
		snprintf(last_suffix, sizeof(last_suffix), "%s", sfx);
		last_title_key = title_key;
		last_suffix_key = suffix_key;
		last_font = f;
		last_max_w = max_w;
		last_h = fh;
		last_scale = FIXED_SCALE;
	}

	// Cached (do not free): the title only changes on navigation.
	size_t split = titleSplit(fitted);
	if (split) {
		// the ASCII parent through " | " in the primary font, the Arabic rest after it, on one baseline
		char head[256];
		memcpy(head, fitted, split);
		head[split] = '\0';
		SDL_Surface* head_s = GFX_getCachedText(f, head, COLOR_GRAY);
		int head_w = 0;
		GFX_measureText(f, head, &head_w, NULL);
		int baseline = (UI_menuBarHeight() - fh) / 2 + TTF_FontAscent(f);
		if (head_s)
			blitTitlePiece(dst, head_s, x, (UI_menuBarHeight() - head_s->h) / 2, shadow);
		// the rest's cached surface comes from the Arabic face (GFX_getCachedText); align its ascent
		TTF_Font* fb = GFX_fallbackFontFor(f);
		SDL_Surface* rest_s = GFX_getCachedText(f, fitted + split, COLOR_GRAY);
		if (!rest_s)
			return head_w;
		blitTitlePiece(dst, rest_s, x + head_w, baseline - TTF_FontAscent(fb ? fb : f), shadow);
		return head_w + rest_s->w;
	}
	SDL_Surface* text = GFX_getCachedText(f, fitted, COLOR_GRAY);
	if (!text)
		return 0;
	blitTitlePiece(dst, text, x, (UI_menuBarHeight() - text->h) / 2, shadow);
	return text->w;
}

int UI_renderPageTitle(SDL_Surface* dst, int x, const char* title, const char* suffix, int max_w) {
	return UI_renderPageTitleEx(dst, x, title, suffix, max_w, false);
}

int UI_renderMenuBar(SDL_Surface* screen, const char* title) {
	return UI_renderMenuBarAt(screen, title, NULL, -1, true, false);
}

int UI_renderMenuBarEx(SDL_Surface* screen, const char* title, bool scrim) {
	return UI_renderMenuBarAt(screen, title, NULL, -1, scrim, false);
}

int UI_renderMenuBarShadowed(SDL_Surface* screen, const char* title, bool scrim, bool title_shadow) {
	return UI_renderMenuBarAt(screen, title, NULL, -1, scrim, title_shadow);
}

int UI_renderMenuBarAt(SDL_Surface* screen, const char* title, const char* suffix, int x, bool scrim,
					   bool title_shadow) {
	int bar_h = UI_menuBarHeight();
	if (scrim) {
		// Semi-transparent bar background (cached between calls)
		static SDL_Surface* menu_bar = NULL;
		if (!UI_getScrim(&menu_bar, screen->w, bar_h))
			return 0;
		SDL_BlitSurface(menu_bar, NULL, screen, &(SDL_Rect){0, 0});
	}

	// Hardware group (right side)
	int ow = GFX_blitHardwareGroup(screen, PWR_getShowSetting());

	// Page title (left side, no pill): at most 80% of the screen, and clear of the hardware group
	if (title && title[0]) {
		if (x < 0)
			x = UI_pageTitleX();
		int max_w = screen->w * 8 / 10;
		int room = screen->w - ow - SCALE1(PADDING * 2) - x;
		if (room < max_w)
			max_w = room;
		if (max_w > 0)
			UI_renderPageTitleEx(screen, x, title, suffix, max_w, title_shadow);
	}

	return ow;
}

const char* UI_pageTitle(char* out, size_t size, const char* parent, const char* page) {
	if (!out || size == 0)
		return out;
	if (page && page[0])
		snprintf(out, size, "%s | %s", parent ? parent : "", page);
	else
		snprintf(out, size, "%s", parent ? parent : "");
	return out;
}

int UI_renderMenuBarPage(SDL_Surface* screen, const char* tool, const char* page) {
	char title[256];
	return UI_renderMenuBar(screen, UI_pageTitle(title, sizeof(title), tool, page));
}

SDL_Surface* UI_captureMenuBar(SDL_Surface* screen) {
	int bar_h = UI_menuBarHeight();
	SDL_Surface* bar = SDL_CreateRGBSurfaceWithFormat(
		0, screen->w, bar_h, screen->format->BitsPerPixel,
		screen->format->format);
	if (!bar)
		return NULL;
	SDL_FillRect(bar, NULL, SDL_MapRGBA(bar->format, 0, 0, 0, 255));
	SDL_BlitSurface(screen, &(SDL_Rect){0, 0, screen->w, bar_h}, bar, NULL);
	return bar;
}

int UI_statusBarChanged(void) {
	static int was_online = -1;
	static int had_bt = -1;
	int is_online = PWR_isOnline();
	int has_bt = PLAT_btIsConnected();
	if (was_online == -1) {
		was_online = is_online;
		had_bt = has_bt;
		return 0;
	}
	int changed = (was_online != is_online) || (had_bt != has_bt);
	was_online = is_online;
	had_bt = has_bt;
	return changed;
}
