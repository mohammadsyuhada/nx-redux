// tg5040

#ifndef PLATFORM_H
#define PLATFORM_H

///////////////////////////////

#ifdef SDL
#include "sdl.h"
#endif

///////////////////////////////

// Brick and Brick Pro share the 1024x768 panel *resolution* (FIXED_WIDTH/HEIGHT key
// off is_brick || is_brickpro), but NOT the UI scale: the Brick is a small panel
// where 3x is right, while the Brick Pro's panel is physically much larger (about
// the Smart Pro S's height), so 3x renders everything ~1.4x too big. The Brick Pro
// therefore uses the Smart Pro's 2x layout (FIXED_SCALE / MAIN_ROW_COUNT /
// SETTINGS_ROW_COUNT / PADDING key off is_brick alone) to match it visually. Input
// still keys off is_brick || is_brickpro: the Brick Pro additionally has analog
// sticks, two extra shoulder buttons (L4/R4) and a HOME key.
extern int is_brick;
extern int is_brickpro;

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
#define BUTTON_L4 BUTTON_NA
#define BUTTON_R4 BUTTON_NA

#define BUTTON_MENU BUTTON_NA
#define BUTTON_MENU_ALT BUTTON_NA
#define BUTTON_POWER 116 // BUTTON_NA
#define BUTTON_PLUS BUTTON_NA
#define BUTTON_MINUS BUTTON_NA

///////////////////////////////

#define CODE_UP CODE_NA
#define CODE_DOWN CODE_NA
#define CODE_LEFT CODE_NA
#define CODE_RIGHT CODE_NA

#define CODE_SELECT CODE_NA
#define CODE_START CODE_NA

#define CODE_A CODE_NA
#define CODE_B CODE_NA
#define CODE_X CODE_NA
#define CODE_Y CODE_NA

#define CODE_L1 CODE_NA
#define CODE_R1 CODE_NA
#define CODE_L2 CODE_NA
#define CODE_R2 CODE_NA
#define CODE_L3 CODE_NA
#define CODE_R3 CODE_NA
#define CODE_L4 CODE_NA
#define CODE_R4 CODE_NA

#define CODE_MENU CODE_NA
#define CODE_POWER 102

#define CODE_PLUS 128
#define CODE_MINUS 129

///////////////////////////////
// HATS
#define JOY_UP JOY_NA
#define JOY_DOWN JOY_NA
#define JOY_LEFT JOY_NA
#define JOY_RIGHT JOY_NA

#define JOY_SELECT 6
#define JOY_START 7

// TODO: these ended up swapped in the first public release of stock :sob:
#define JOY_A 1
#define JOY_B 0
#define JOY_X 3
#define JOY_Y 2

#define JOY_L1 4
#define JOY_R1 5
#define JOY_L2 JOY_NA
#define JOY_R2 JOY_NA
// Brick: 9/10 are the FN1/FN2 keys. Brick Pro: 9/10 are the stick clicks and the
// FN keys move up to 11/12 (L4/R4).
#define JOY_L3 (is_brick || is_brickpro ? 9 : JOY_NA)
#define JOY_R3 (is_brick || is_brickpro ? 10 : JOY_NA)
#define JOY_L4 (is_brickpro ? 11 : JOY_NA)
#define JOY_R4 (is_brickpro ? 12 : JOY_NA)

// The two extra "function" keys (F1/F2). The Brick has no analog sticks, so they are the
// L3/R3 joystick buttons; on the Brick Pro the analog sticks take L3/R3 (stick-click), so
// the function keys move to L4/R4. Paks that act on F1/F2 must use BTN_FN1/BTN_FN2 rather
// than hard-coding BTN_L3/BTN_R3 — otherwise the toggle lands on the Brick Pro's stick
// click instead of its F1/F2 key. Other platforms fall back to BTN_L3/R3 (see defines.h).
#define BTN_FN1 (is_brickpro ? BTN_L4 : BTN_L3)
#define BTN_FN2 (is_brickpro ? BTN_R4 : BTN_R3)
// Display name for those keys — the Brick and Brick Pro both physically print "F1"/"F2"
// (the underlying index differs, L3/R3 vs L4/R4, but the label the user sees does not).
#define BTN_FN1_NAME "F1"
#define BTN_FN2_NAME "F2"

#define JOY_MENU 8
#define JOY_HOME (is_brickpro ? 15 : JOY_NA)
#define JOY_POWER 102
#define JOY_PLUS (is_brick || is_brickpro ? 14 : 128)
#define JOY_MINUS (is_brick || is_brickpro ? 13 : 129)

///////////////////////////////

#define AXIS_L2 2 // ABSZ
#define AXIS_R2 5 // RABSZ

#define HAS_JOYSTICK (!is_brick)

#define AXIS_LX 0 // ABS_X, -30k (left) to 30k (right)
#define AXIS_LY 1 // ABS_Y, -30k (up) to 30k (down)
#define AXIS_RX 3 // ABS_RX, -30k (left) to 30k (right)
#define AXIS_RY 4 // ABS_RY, -30k (up) to 30k (down)

///////////////////////////////

#define BTN_RESUME BTN_X
#define BTN_SLEEP BTN_POWER
#define BTN_WAKE BTN_POWER
#define BTN_MOD_VOLUME BTN_NONE
#define BTN_MOD_BRIGHTNESS BTN_SELECT
#define BTN_MOD_COLORTEMP BTN_START
#define BTN_MOD_PLUS BTN_PLUS
#define BTN_MOD_MINUS BTN_MINUS

///////////////////////////////

#define FIXED_SCALE (is_brick ? 3 : 2) // Brick Pro uses 2x (see the panel note above)
#define FIXED_WIDTH (is_brick || is_brickpro ? 1024 : 1280)
#define FIXED_HEIGHT (is_brick || is_brickpro ? 768 : 720)
#define FIXED_BPP 2
#define FIXED_DEPTH (FIXED_BPP * 8)
#define FIXED_PITCH (FIXED_WIDTH * FIXED_BPP)
#define FIXED_SIZE (FIXED_PITCH * FIXED_HEIGHT)

///////////////////////////////

// Brick Pro runs the Smart Pro's 2x layout but on a taller panel (768 vs 720), so it
// fits one extra main-menu row (11 vs 10). Settings/padding stay on the shared 2x values.
#define MAIN_ROW_COUNT (is_brick ? 7 : (is_brickpro ? 11 : 10))
#define SETTINGS_ROW_COUNT (is_brick ? 9 : 11)
#define PADDING (is_brick ? 5 : 10)

///////////////////////////////

#define SDCARD_PATH "/mnt/SDCARD"
#define MUTE_VOLUME_RAW 0

// this should be set to the devices native screen refresh rate
#define SCREEN_FPS 60.235

// Brick Pro has 5 zones (f1, f2, top bar, joysticks, triggers), Brick 4, Smart Pro 3
#define MAX_LIGHTS 5

///////////////////////////////

#endif
