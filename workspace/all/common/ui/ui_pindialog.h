#ifndef UI_PINDIALOG_H
#define UI_PINDIALOG_H

#include "sdl.h"

// Full-screen 4-digit PIN picker: up/down changes the focused digit,
// left/right moves focus, A confirms, B cancels. Unfocused slots render a
// dot so an onlooker can't read the whole code. The caller owns the loop
// (poll PAD, call handleInput, then render + flip), like ui_listdialog.

#define PINDIALOG_PIN_LEN 4

typedef enum {
	PINDIALOG_NONE,
	PINDIALOG_CONFIRMED,
	PINDIALOG_CANCEL,
} PinDialogAction;

typedef struct {
	PinDialogAction action;
	char pin[PINDIALOG_PIN_LEN + 1]; // filled when action == PINDIALOG_CONFIRMED
} PinDialogResult;

void PinDialog_init(const char* title);
void PinDialog_setError(const char* msg); // shown under the slots; NULL clears
PinDialogResult PinDialog_handleInput(void);
void PinDialog_render(SDL_Surface* screen);
void PinDialog_quit(void);

#endif // UI_PINDIALOG_H
