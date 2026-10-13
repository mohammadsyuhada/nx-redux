#pragma once

#include <stdint.h>
#include "libretro.h"

int setFastForward(int enable);
void input_poll_callback(void);
// Called before each core frame (ma_core.c run wrapper); see input_state_callback.
void Input_beginFrame(void);
// Runs a save/load/reset shortcut the input callback queued; call after core.run().
void Input_runDeferredShortcut(void);
int16_t input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id);
void Input_init(const struct retro_input_descriptor* vars);

// Current local RETRO_DEVICE_ID_JOYPAD_* button bitmask (for netplay input sync).
uint32_t Input_getButtons(void);
// Current local sticks {left x, left y, right x, right y}, the left one from the
// D-pad while "Toggle D-pad Joystick" is on and a direction is held (also what
// netplay sends).
void Input_getLocalSticks(int16_t sticks[4]);

// Per-pak "minarch_face_buttons = positional" (default.cfg): the core's face
// buttons follow the pad's physical positions whatever the Button layout
// setting, as the console's own controller would (Dreamcast). Menus keep
// following the layout. Set by Config_readOptions.
extern int input_positional_faces;

// Per-pak "minarch_stick_dpad = off" (default.cfg): the left stick no longer
// doubles as the D-pad under the Standard controller. For analog cores
// (Dreamcast, N64, PSP), which already read the stick as a stick, so a push
// would also press D-pad buttons (Daytona USA's camera, issue #211).
extern int input_stick_dpad;
