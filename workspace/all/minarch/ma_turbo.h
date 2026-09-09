#pragma once

#include <stdint.h>

// Frontend-side turbo fire.
//
// On TrimUI devices turbo is a stock-OS feature: trimui_inputd pulses any
// button flagged via /tmp/trimui_inputd/turbo_* on its own GPIO poll cadence
// (Brick: 8 ms pulses at 60 Hz, Smart Pro S: 16 ms pulses at 31 Hz). minarch
// samples the pad once per frame, so those pulses alias into "held" or
// "nothing" instead of repeated presses (upstream #780). Rather than fight
// the daemon, minarch reconstructs "physically held" from the pulse train
// (any press/hold/release seen in a frame counts as activity, with a short
// release grace) and emits its own frame-based cadence to the core.
//
// This module is deliberately dependency-free so the host unit test in
// scripts/tests/test-turbo-shaper.sh can compile it as-is.

#define TURBO_ON_FRAMES 2	  // frames reported pressed per cycle
#define TURBO_OFF_FRAMES 2	  // frames reported released per cycle (15 Hz at 60 fps)
#define TURBO_RELEASE_GRACE 2 // frames without any pad activity before the hold ends

#define TURBO_MAX_BUTTONS 32 // indexed by BTN_ID_*

typedef struct TurboButton {
	uint8_t active; // a hold is in progress
	uint8_t phase;	// position in the on/off cycle
	uint8_t idle;	// consecutive frames without activity (saturating)
} TurboButton;

typedef struct TurboState {
	TurboButton btn[TURBO_MAX_BUTTONS];
} TurboState;

void Turbo_reset(TurboState* s);

// Advance one frame for button `btn_id`. `activity` is non-zero when the pad
// layer saw the button pressed, just pressed or just released this frame.
// Returns 1 when the core should see the button pressed this frame.
int Turbo_step(TurboState* s, int btn_id, int activity);
