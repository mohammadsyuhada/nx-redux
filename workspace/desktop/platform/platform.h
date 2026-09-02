// tg5040

#ifndef PLATFORM_H
#define PLATFORM_H

///////////////////////////////

#include "sdl.h"

///////////////////////////////

// Desktop-only: use SDL's normalized SDL_GameController API for external
// controllers (Xbox/PS/Switch/8BitDo). Gates the game-controller poll path in
// the shared api.c so device platforms (which have their own platform.h and
// drive their built-in pad through raw SDL_Joystick) compile it out entirely.
#define HAS_GAMECONTROLLER 1

///////////////////////////////

#define BUTTON_UP BUTTON_NA
#define BUTTON_DOWN BUTTON_NA
#define BUTTON_LEFT BUTTON_NA
#define BUTTON_RIGHT BUTTON_NA

#define BUTTON_SELECT BUTTON_NA
#define BUTTON_START BUTTON_NA

#define BUTTON_A BUTTON_NA
#define BUTTON_B BUTTON_NA
#define BUTTON_X BUTTON_NA
#define BUTTON_Y BUTTON_NA

#define BUTTON_L1 BUTTON_NA
#define BUTTON_R1 BUTTON_NA
#define BUTTON_L2 BUTTON_NA
#define BUTTON_R2 BUTTON_NA
#define BUTTON_L3 BUTTON_NA
#define BUTTON_R3 BUTTON_NA

#define BUTTON_MENU BUTTON_NA
#define BUTTON_MENU_ALT BUTTON_NA
#define BUTTON_POWER BUTTON_NA
#define BUTTON_PLUS BUTTON_NA
#define BUTTON_MINUS BUTTON_NA

///////////////////////////////

// see https://wiki.libsdl.org/SDL2/SDL_Scancode

// Values are SDL2 scancodes. Layout mirrors RetroArch's default keyboard
// binds so it's familiar to emulator users: arrows = d-pad, face buttons on
// the Z/X/A/S cluster, shoulders on Q/W/E/R, Enter = Start, Right Shift =
// Select. MENU (Space) opens the in-game menu; the handheld's POWER maps to
// Backspace (little effect on desktop).
#define CODE_UP 82	  // Up Arrow
#define CODE_DOWN 81  // Down Arrow
#define CODE_LEFT 80  // Left Arrow
#define CODE_RIGHT 79 // Right Arrow

#define CODE_SELECT 229 // Right Shift
#define CODE_START 40	// Return

#define CODE_A 27 // X
#define CODE_B 29 // Z
#define CODE_X 22 // S
#define CODE_Y 4  // A

#define CODE_L1 20 // Q
#define CODE_R1 26 // W
#define CODE_L2 8  // E
#define CODE_R2 21 // R
#define CODE_L3 CODE_NA
#define CODE_R3 CODE_NA

#define CODE_MENU 44  // Space
#define CODE_POWER 42 // Backspace

#define CODE_PLUS CODE_NA
#define CODE_MINUS CODE_NA

///////////////////////////////
// HATS
#define JOY_UP JOY_NA
#define JOY_DOWN JOY_NA
#define JOY_LEFT JOY_NA
#define JOY_RIGHT JOY_NA

#define JOY_SELECT JOY_NA
#define JOY_START JOY_NA

// TODO: these ended up swapped in the first public release of stock :sob:
#define JOY_A JOY_NA
#define JOY_B JOY_NA
#define JOY_X JOY_NA
#define JOY_Y JOY_NA

#define JOY_L1 JOY_NA
#define JOY_R1 JOY_NA
#define JOY_L2 JOY_NA
#define JOY_R2 JOY_NA
#define JOY_L3 JOY_NA
#define JOY_R3 JOY_NA

#define JOY_MENU JOY_NA
#define JOY_POWER JOY_NA
#define JOY_PLUS JOY_NA
#define JOY_MINUS JOY_NA

///////////////////////////////

#define BTN_RESUME BTN_X
#define BTN_SLEEP BTN_POWER
#define BTN_WAKE BTN_POWER
#define BTN_MOD_VOLUME BTN_NONE
#define BTN_MOD_COLORTEMP BTN_NONE
// No brightness modifier on desktop: the host OS owns the display, and mapping
// it to MENU (Space) made a long-press show a useless brightness indicator.
#define BTN_MOD_BRIGHTNESS BTN_NONE
#define BTN_MOD_PLUS BTN_PLUS
#define BTN_MOD_MINUS BTN_MINUS

// Never sleep: a desktop window has no screen to power down, and the shared
// wait-for-wake loop would eat all input including SDL_QUIT (see defines.h).
#define HAS_SLEEP 0

///////////////////////////////

// average smallish screen
//#define FIXED_SCALE 	2
//#define FIXED_WIDTH		640
//#define FIXED_HEIGHT	480
//#define MAIN_ROW_COUNT 6
//#define PADDING 10

// Brick Pro layout: same 1024x768 as the Brick, but the Brick's 3x UI scale
// is sized for a tiny handheld panel and renders oversized on a monitor —
// the same reason the physically-larger Brick Pro runs the 2x layout (see
// workspace/tg5040/platform/platform.h's panel note).
#define FIXED_SCALE 2
#define FIXED_WIDTH 1024
#define FIXED_HEIGHT 768
#define MAIN_ROW_COUNT 11
#define SETTINGS_ROW_COUNT 11
#define PADDING 10

// emulate TSP
//#define FIXED_SCALE 	2
//#define FIXED_WIDTH		1280
//#define FIXED_HEIGHT	720
//#define MAIN_ROW_COUNT  10
//#define PADDING 10

#define FIXED_BPP 2
#define FIXED_DEPTH (FIXED_BPP * 8)
#define FIXED_PITCH (FIXED_WIDTH * FIXED_BPP)
#define FIXED_SIZE (FIXED_PITCH * FIXED_HEIGHT)

// #define HAS_HDMI	1
// #define HDMI_WIDTH 	1280
// #define HDMI_HEIGHT 720
// #define HDMI_PITCH 	(HDMI_WIDTH * FIXED_BPP)
// #define HDMI_SIZE	(HDMI_PITCH * HDMI_HEIGHT)

///////////////////////////////


///////////////////////////////

#define HAS_RUNTIME_PATHS 1
#define MUTE_VOLUME_RAW 63 // 0 unintuitively is 100% volume

#define MAX_LIGHTS 4

// this should be set to the devices native screen refresh rate
#define SCREEN_FPS 60.0
///////////////////////////////

#endif
