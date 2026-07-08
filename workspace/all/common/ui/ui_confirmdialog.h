#ifndef UI_CONFIRMDIALOG_H
#define UI_CONFIRMDIALOG_H

#include "sdl.h"

// Full-screen confirmation dialog: centered title, optional subtitle,
// CANCEL (B) / CONFIRM (A) button hints.
void UI_renderConfirmDialog(SDL_Surface* dst, const char* title,
							const char* subtitle);

#endif // UI_CONFIRMDIALOG_H
