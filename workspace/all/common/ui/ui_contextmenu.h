#ifndef UI_CONTEXTMENU_H
#define UI_CONTEXTMENU_H

#include "sdl.h" // IWYU pragma: keep
#include <stdbool.h>

#define CONTEXTMENU_MAX_ITEMS 16
#define CONTEXTMENU_MAX_TEXT 64

typedef struct {
	char label[CONTEXTMENU_MAX_TEXT];
	int id; // caller-defined identifier for the action
} ContextMenuItem;

typedef enum {
	CONTEXTMENU_NONE,
	CONTEXTMENU_SELECTED,
	CONTEXTMENU_CANCEL,
} ContextMenuAction;

typedef struct {
	ContextMenuAction action;
	int id; // the selected item's id (-1 if cancelled)
} ContextMenuResult;

// Open the context menu with a title and list of items.
// items array is copied internally.
void ContextMenu_open(const char* title, ContextMenuItem* items, int count);

// Process input. Call after PAD_poll().
ContextMenuResult ContextMenu_handleInput(void);

// Render the context menu overlay on top of the current screen.
void ContextMenu_render(SDL_Surface* screen);

// Close the context menu.
void ContextMenu_close(void);

// Returns true if the context menu is currently open (or was just closed this frame).
bool ContextMenu_isOpen(void);


#endif // UI_CONTEXTMENU_H
