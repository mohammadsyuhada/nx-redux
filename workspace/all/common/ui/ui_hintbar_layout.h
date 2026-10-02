#ifndef UI_HINTBAR_LAYOUT_H
#define UI_HINTBAR_LAYOUT_H

// The hint bar's pure geometry (SDL-free, host-tested): the one place its icons' centring is defined, for the bar's
// renderer and UI_buttonHintIconTop (ui_buttonhintbar.c) and the info band's ink top (nextui InfoBand_hintInkTop).

// How far below the bar's top edge (bar_h tall) its icon_h tall icons start: centred in the bar.
static inline int UI_hintBarIconOffset(int bar_h, int icon_h) {
	return (bar_h - icon_h) / 2;
}

#endif // UI_HINTBAR_LAYOUT_H
