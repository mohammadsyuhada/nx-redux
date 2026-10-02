#ifndef UI_FONT_H
#define UI_FONT_H

#include "defines.h" // FONT_LARGE (UI_TEXT_LABEL_LOGICAL)
#include "sdl.h"
#include <stdbool.h>

// The system UI font (GFX_getSystemFontPath, the user's selected font) at arbitrary text sizes: the Home
// tab, the page title (ui_menubar.c) and any other caller. Fonts are opened on demand and cached (up to 64
// size/weight pairs); a system-font reload (font change, GFX_reloadScale) or a FIXED_SCALE change closes
// them all and they reopen at the new font and scale. UI thread only: the cache (and the Arabic fallback resolver
// it installs into GFX) has no locking, so worker threads must not call any UIFont_* / UI_textRole function.

// Cached system font at NX_SP(sp) px; bold is a synthetic TTF_STYLE_BOLD on its own instance. Owned by
// the cache: never close it. Valid until a later UIFont_get (which may evict it when 64 are open, or
// reopen everything after a FIXED_SCALE change) or UIFont_quit: use it before the next UIFont_get. Closing a font
// also drops its GFX_getCachedText surfaces. NULL when the font can't be opened.
TTF_Font* UIFont_get(float sp, bool bold);
// The same at an exact px size (NX_SP already applied): text sized off the list label (UI_textRole, rich rows).
TTF_Font* UIFont_getPx(int px, bool bold);
// Ink extent of UTF-8 text from its glyph boxes, in px relative to the baseline (+ is up): top is the
// highest maxy, bottom the lowest miny. Both 0 for empty text or a NULL font.
void UIFont_inkBounds(TTF_Font* f, const char* text, int* top, int* bottom);
// Close every cached font.
void UIFont_quit(void);

// Text roles (LIST-LAYOUT §10.6): four steps around the list label, font.large (16 logical, SCALE1 px), so
// they follow the UI scale. headline 1.15 x (detail/progress headline, achievement name, empty-page title),
// label 1.0 x (list rows, loading text), secondary 0.8 x (a row's secondary span, detail lines, achievement
// description and info, rich row title), caption 0.64 x (notes without a headline, rich row second line).
// The options rows keep font.small / font.tiny (optionLabel 12, optionValue 10), and the page title, hint bar,
// modals and toasts keep their own fixed sizes.
#define UI_TEXT_LABEL_LOGICAL ((float)FONT_LARGE) // font.large, the list label (defines.h)
typedef enum {
	UI_TEXT_HEADLINE,
	UI_TEXT_LABEL,
	UI_TEXT_SECONDARY,
	UI_TEXT_CAPTION,
} UITextRole;
// Size in logical units (18.4 / 16 / 12.8 / 10.24).
float UI_textRoleLogical(UITextRole role);
// Size in px at the current scale (SCALE1, rounded).
int UI_textRolePx(UITextRole role);
// The system font for a role (cached like UIFont_get: use it before the next UIFont_get call). bold is the
// synthetic bold style (the achievement name, a progress page's status line).
TTF_Font* UI_textRole(UITextRole role, bool bold);

#endif // UI_FONT_H
