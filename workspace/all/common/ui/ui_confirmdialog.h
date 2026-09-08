#ifndef UI_CONFIRMDIALOG_H
#define UI_CONFIRMDIALOG_H

#include "sdl.h"
#include <stdbool.h>
#include <stdint.h>

// Full-screen confirmation dialog: centered title, optional subtitle,
// CANCEL (B) / CONFIRM (A) button hints.
void UI_renderConfirmDialog(SDL_Surface* dst, const char* title,
							const char* subtitle);
// Same dialog with caller-supplied button hints: NULL-terminated
// "button","label" pairs, e.g. (char*[]){"B", "BACK", "A", "RETRY", NULL}.
// NULL hints = the default CANCEL/CONFIRM pair.
void UI_renderConfirmDialogHints(SDL_Surface* dst, const char* title,
								 const char* subtitle, char** hints);

// Canonical power-aware blocking modal loop, factored out of the several
// GFX_startFrame/PAD_poll/PWR_update/dirty-flip loops duplicated across
// gamelist.c, settings.c and extras.c. The caller supplies render (draw
// only - the loop flips) and handle (poll input / advance state); handle
// returns UI_MODAL_CONTINUE to keep looping, UI_MODAL_DIRTY to force a
// redraw without ending the loop, or any value >= 0 to end the loop with
// that result.
enum { UI_MODAL_CONTINUE = -1,
	   UI_MODAL_DIRTY = -2 };
typedef struct {
	SDL_Surface* screen;
	void (*render)(SDL_Surface* screen, void* ctx); // draw only; loop flips
	int (*handle)(void* ctx);						// UI_MODAL_CONTINUE / UI_MODAL_DIRTY / >=0 ends loop with that result
	void* ctx;
	const volatile bool* quit_flag; // optional external abort (returns -1); NULL to ignore
	uint32_t timeout_ms;			// 0 = none; expiry returns -1
	bool clear_layers;				// GFX_clearLayers(LAYER_ALL) on entry + exit
	bool reset_pad;					// trailing PAD_poll(); PAD_reset();
} UI_ModalOpts;
int UI_modalLoop(const UI_ModalOpts* o);

// Full-screen confirm/cancel modal built on UI_modalLoop. Returns true on A
// (confirm), false on B (cancel) or on quit_flag/timeout abort.
bool UI_confirmModal(SDL_Surface* screen, const char* title, const char* subtitle,
					 const volatile bool* quit_flag, bool clear_layers, bool reset_pad);
// UI_confirmModal with caller-supplied button hints (see
// UI_renderConfirmDialogHints). A still confirms (true), B still cancels.
bool UI_confirmModalHints(SDL_Surface* screen, const char* title, const char* subtitle,
						  char** hints, const volatile bool* quit_flag, bool clear_layers,
						  bool reset_pad);

// Full-screen PIN-entry modal built on UI_modalLoop. Owns PinDialog_init/
// PinDialog_quit for the call. Returns true and fills pin_out (at least
// PINDIALOG_PIN_LEN+1 bytes) on confirm, false on cancel/quit_flag/timeout.
bool UI_pinModal(SDL_Surface* screen, const char* title, const char* error, char* pin_out,
				 const volatile bool* quit_flag, bool clear_layers, bool reset_pad);

#endif // UI_CONFIRMDIALOG_H
