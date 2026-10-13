#ifndef EMU_SHORTCUTS_H
#define EMU_SHORTCUTS_H

#include <stddef.h>

// Emulator shortcuts (save state, toggle FF, turbo, ...) are global: one file
// per device, edited in the Settings app and read by minarch at every launch.
// Console/game cfgs no longer carry them (minarch ignores any it finds there).
//
//   <userdata>/minarch-shortcuts-<DEVICE>.cfg   (no "-<DEVICE>" when DEVICE is unset)
//
// One line per shortcut, the format minarch's cfgs always used:
//
//   bind <Shortcut Name> = <LABEL>
//
// <LABEL> is one of button_labels[] ("NONE", "A", "MENU+X", ...). Names, not
// enum indices, are what is stored, so the names below must never change;
// new shortcuts are appended. No SDL here (host-testable).

// X(enum suffix, name stored in the file)
#define SHORTCUT_LIST(X)                            \
	X(SAVE_STATE, "Save State")                     \
	X(LOAD_STATE, "Load State")                     \
	X(UNDO_LOAD_STATE, "Undo Load State")           \
	X(RESET_GAME, "Reset Game")                     \
	X(SAVE_QUIT, "Save & Quit")                     \
	X(CYCLE_SCALE, "Cycle Scaling")                 \
	X(CYCLE_EFFECT, "Cycle Effect")                 \
	X(TOGGLE_FF, "Toggle FF")                       \
	X(HOLD_FF, "Hold FF")                           \
	X(TOGGLE_REWIND, "Toggle Rewind")               \
	X(HOLD_REWIND, "Hold Rewind")                   \
	X(GAMESWITCHER, "Game Switcher")                \
	X(SCREENSHOT, "Screenshot")                     \
	/* platforms with turbo (PLAT_canTurbo) only */ \
	X(TOGGLE_TURBO_A, "Toggle Turbo A")             \
	X(TOGGLE_TURBO_B, "Toggle Turbo B")             \
	X(TOGGLE_TURBO_X, "Toggle Turbo X")             \
	X(TOGGLE_TURBO_Y, "Toggle Turbo Y")             \
	X(TOGGLE_TURBO_L, "Toggle Turbo L")             \
	X(TOGGLE_TURBO_L2, "Toggle Turbo L2")           \
	X(TOGGLE_TURBO_R, "Toggle Turbo R")             \
	X(TOGGLE_TURBO_R2, "Toggle Turbo R2")           \
	/* every platform; off at every game launch */  \
	X(TOGGLE_DPAD_JOYSTICK, "Toggle D-pad Joystick")

#define SHORTCUT_ENUM_(id, name) SHORTCUT_##id,
enum {
	SHORTCUT_LIST(SHORTCUT_ENUM_)
		SHORTCUT_COUNT,
};
#undef SHORTCUT_ENUM_

// Bindable buttons: the BTN_ID_* range before BTN_ID_MENU (defines.h), UP..R4.
// A label index is 0 for NONE, 1 + BTN_ID for a button and
// 1 + BTN_ID + SHORTCUT_BUTTON_COUNT for MENU+button.
#define SHORTCUT_BUTTON_COUNT 18
#define SHORTCUT_LABEL_COUNT (1 + 2 * SHORTCUT_BUTTON_COUNT)

extern const char* shortcut_names[SHORTCUT_COUNT];
// in BTN_ID_ order, off by one for NONE; NULL-terminated. minarch's bind menus
// use the same array, so what they show is what this file stores.
extern char* button_labels[SHORTCUT_LABEL_COUNT + 1];

// shortcut index for a stored name, -1 if unknown
int EmuShortcuts_find(const char* name);
// label index for a stored label, -1 if unknown
int EmuShortcuts_labelIndex(const char* label);

static inline int EmuShortcuts_toLabel(int local, int mod) {
	if (local < 0)
		return 0;
	return local + 1 + (mod ? SHORTCUT_BUTTON_COUNT : 0);
}
static inline void EmuShortcuts_fromLabel(int label, int* local, int* mod) {
	if (label <= 0 || label >= SHORTCUT_LABEL_COUNT) {
		*local = -1;
		*mod = 0;
	} else if (label > SHORTCUT_BUTTON_COUNT) {
		*local = label - 1 - SHORTCUT_BUTTON_COUNT;
		*mod = 1;
	} else {
		*local = label - 1;
		*mod = 0;
	}
}

// MENU+SELECT is minarch's built-in "save and open the Game Switcher" combo
// (ma_input.c), checked before any shortcut, so it can't be bound.
#define SHORTCUT_LABEL_MENU_SELECT (1 + 9 + SHORTCUT_BUTTON_COUNT) // BTN_ID_SELECT = 9
static inline int EmuShortcuts_isReserved(int label) {
	return label == SHORTCUT_LABEL_MENU_SELECT;
}

// "<dir>/minarch-shortcuts[-<device_tag>].cfg"; returns 0 when it does not fit
int EmuShortcuts_getPath(char* out, size_t size, const char* dir, const char* device_tag);
// Sets labels[] for every "bind <known name> = <label>" line in text; other
// lines and unknown names are ignored, an unknown label reads as NONE.
void EmuShortcuts_parse(const char* text, int labels[SHORTCUT_COUNT]);
// All NONE, then the file's binds. Returns 1 when the file was read.
int EmuShortcuts_read(const char* path, int labels[SHORTCUT_COUNT]);
// Writes every shortcut (NONE included) and syncs. Returns 1 on success.
int EmuShortcuts_write(const char* path, const int labels[SHORTCUT_COUNT]);

#endif
