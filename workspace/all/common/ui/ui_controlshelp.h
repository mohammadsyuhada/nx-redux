#ifndef UI_CONTROLSHELP_H
#define UI_CONTROLSHELP_H

#include "sdl.h"

// Button-action pair for controls help dialog
typedef struct {
	const char* button;
	const char* action;
} ControlHelp;

// Render a controls help dialog overlay showing button mappings.
// controls must be NULL-terminated (last entry has button == NULL).
void UI_renderControlsHelp(SDL_Surface* screen, const char* title,
						   const ControlHelp* controls);

#endif // UI_CONTROLSHELP_H
