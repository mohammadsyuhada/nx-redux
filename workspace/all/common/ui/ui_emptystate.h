#ifndef UI_EMPTYSTATE_H
#define UI_EMPTYSTATE_H

#include "sdl.h"

// Render centered empty state with icon, message, optional subtitle, and button hints.
// y_button_label: label for Y button (e.g., "NEW", "MANAGE"), or NULL for no Y button.
void UI_renderEmptyState(SDL_Surface* screen, const char* message,
						 const char* subtitle, const char* y_button_label);

#endif // UI_EMPTYSTATE_H
