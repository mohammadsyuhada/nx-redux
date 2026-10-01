#ifndef UI_MENUBAR_H
#define UI_MENUBAR_H

#include <stdbool.h>
#include <stddef.h>
#include "sdl.h"

// Page title (LIST-LAYOUT §10.1): one look everywhere. UIFont_get(UI_PAGE_TITLE_SP, bold) in COLOR_GRAY,
// vertically centred in the top strip (UI_menuBarHeight()). The part up to and including the first " | " and
// the optional suffix (e.g. the RA count " (12/39)") never truncate; only the middle ellipsizes
// (UI_titleFit, common/ui_title_fit.h).
#define UI_PAGE_TITLE_SP 16

// Height of the top strip (SCALE1(28)).
int UI_menuBarHeight(void);
// y of the page title's letters' bottom (its baseline) in the top strip.
int UI_pageTitleBaseline(void);
// Where a titled page's content band starts: under the title's ink (its descenders, "p" in "Options") plus
// 2 dp (LIST-LAYOUT §10.2: a list is centred between the title's letters and the hint bar's icons, not the
// boxes), so a block that fills the band never touches the title.
int UI_pageTitleBandTop(void);
// Default title x: the standard pill list's text start, the 14 dp list inset (NX_DP(NX_LIST_INSET_DP)).
int UI_pageTitleX(void);
// Draw a page title at x in the top strip, fitted into max_w px (<= 0: no limit beyond the screen). suffix
// may be NULL. Returns the drawn width.
int UI_renderPageTitle(SDL_Surface* dst, int x, const char* title, const char* suffix, int max_w);
// The same; shadow draws the dark shadow (black 60%, SCALE1(1) offset) under it, for a title over a picture.
int UI_renderPageTitleEx(SDL_Surface* dst, int x, const char* title, const char* suffix, int max_w, bool shadow);

// Render the top menu bar: semi-transparent background, page title (left),
// hardware group (right). Returns the width of the hardware group (ow) for
// callers that need it. The title is at UI_pageTitleX(), at most
// min(0.8 x screen width, the room left of the hardware group) wide.
int UI_renderMenuBar(SDL_Surface* screen, const char* title);
// The same; scrim=false skips the background (the caller draws its own fade).
int UI_renderMenuBarEx(SDL_Surface* screen, const char* title, bool scrim);
// The same; title_shadow draws the title with the dark shadow (black 60%, SCALE1(1) offset), for a bar over a
// picture (nextui's Backdrop).
int UI_renderMenuBarShadowed(SDL_Surface* screen, const char* title, bool scrim, bool title_shadow);
// The full form: the title at x (< 0: UI_pageTitleX()) with an optional never-truncated suffix (NULL for
// none), for pages whose content starts elsewhere (settings option rows, the 24 dp game-list gutter).
int UI_renderMenuBarAt(SDL_Surface* screen, const char* title, const char* suffix, int x, bool scrim,
					   bool title_shadow);
// "<tool> | <page>" (LIST-LAYOUT §10.1: a page inside a tool names its tool), or tool alone when page is NULL or
// empty; the standard bar otherwise (UI_renderMenuBar).
int UI_renderMenuBarPage(SDL_Surface* screen, const char* tool, const char* page);
// Format "<parent> | <page>" into out (parent alone when page is NULL or empty). Returns out.
const char* UI_pageTitle(char* out, size_t size, const char* parent, const char* page);

// Snapshot the just-rendered top menu bar into a new surface (caller frees).
// Used to overlay a fixed menu bar during slide transitions. Returns NULL on failure.
SDL_Surface* UI_captureMenuBar(SDL_Surface* screen);

// Returns 1 when the wifi/bluetooth status shown in the bar changed since the
// last call (used to trigger a redraw).
int UI_statusBarChanged(void);

#endif // UI_MENUBAR_H
