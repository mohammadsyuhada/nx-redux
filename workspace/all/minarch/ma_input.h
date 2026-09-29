#pragma once

#include <stdint.h>
#include "libretro.h"

int setFastForward(int enable);
void input_poll_callback(void);
// Called before each core frame (ma_core.c run wrapper); see input_state_callback.
void Input_beginFrame(void);
int16_t input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id);
void Input_init(const struct retro_input_descriptor* vars);

// Current local RETRO_DEVICE_ID_JOYPAD_* button bitmask (for netplay input sync).
uint32_t Input_getButtons(void);

// Per-pak "minarch_face_buttons = positional" (default.cfg): the core's face
// buttons follow the pad's physical positions whatever the Button layout
// setting, as the console's own controller would (Dreamcast). Menus keep
// following the layout. Set by Config_readOptions.
extern int input_positional_faces;
