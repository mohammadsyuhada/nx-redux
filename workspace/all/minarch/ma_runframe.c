#include "ma_internal.h"
#include "utils.h"
#include "ma_runframe.h"
#include "ma_input.h"
#include "ma_rewind.h"
#include "ma_avinfo.h"
#include "ma_saves.h"
#include "ma_options.h"

_Static_assert(SYNC_SRC_AUTO == AVSYNC_AUTO && SYNC_SRC_SCREEN == AVSYNC_SCREEN && SYNC_SRC_CORE == AVSYNC_CORE &&
				   SYNC_SRC_EMULATED == AVSYNC_EMULATED,
			   "ma_avinfo.h sync values must match SYNC_SRC_*");

void chooseSyncRef(void) {
	use_core_fps = SyncRef_useCoreFps(sync_ref, core.get_region() == RETRO_REGION_PAL, core.fps, SCREEN_FPS);
}

// Core options swapped for a cheaper value while fast-forwarding. Only what the
// core reads changes (FF_coreOptionValue, from GET_VARIABLE): the stored option,
// the menus and the saved config keep the user's choice.
static const struct {
	const char* key;
	const char* value;
} ff_core_options[] = {
	// High costs ~5.4 ms a frame on the Brick against ~2.1 ms for Low, which
	// alone held fceumm (FC, FDS) to 2x at any Max FF Speed
	{"fceumm_sndquality", "Low"},
	{NULL, NULL},
};

const char* FF_coreOptionValue(const char* key, const char* value) {
	if (!fast_forward || !key)
		return value;
	for (int i = 0; ff_core_options[i].key; i++) {
		if (!strcmp(key, ff_core_options[i].key))
			return ff_core_options[i].value;
	}
	return value;
}

// On every fast-forward on/off edge: have the core re-read its options (above)
// and, when FF audio plays, drop minarch's resampler to Low until FF ends. With
// FF audio off the samples never reach the resampler, so it is left alone.
static void syncFFQuality(void) {
	static int was_ff = 0;
	static int resampler_lowered = 0;
	if (fast_forward == was_ff)
		return;
	was_ff = fast_forward;
	for (int i = 0; ff_core_options[i].key; i++) {
		if (OptionList_getOption(&config.core, ff_core_options[i].key)) {
			config.core.changed = 1;
			break;
		}
	}
	if (fast_forward && ff_audio && resampling_quality != 0) {
		SND_setQuality(0);
		resampler_lowered = 1;
	} else if (!fast_forward && resampler_lowered) {
		SND_setQuality(resampling_quality);
		resampler_lowered = 0;
	}
}

// A fast-forward pass already runs max_ff_speed + 1 core frames (see run_frame),
// so pacing each pass to one native frame period gives exactly (max_ff_speed + 1)x.
static void limitFF(void) {
	static uint64_t last_time = 0;

	uint64_t now = getMicroseconds();
	if (fast_forward && max_ff_speed && core.fps > 0) {
		uint64_t ff_frame_time = 1000000 / core.fps;
		if (last_time == 0)
			last_time = now;
		int elapsed = now - last_time;
		// running behind by more than a pass: resync instead of bursting to catch up
		if (elapsed > 0 && elapsed < (int)(ff_frame_time * 2)) {
			if (elapsed < ff_frame_time) {
				int delay = (ff_frame_time - elapsed) / 1000;
				if (delay > 0)
					SDL_Delay(delay);
			}
			last_time += ff_frame_time;
			return;
		}
	}
	last_time = now;
}

void run_frame(void) {
	syncFFQuality();
	// if rewind is toggled, fast-forward toggle must stay off; fast-forward hold pauses rewind
	int do_rewind = (rewind_pressed || rewind_toggle) && !(rewind_toggle && ff_hold_active);
	if (do_rewind) {
		int was_rewinding = rewinding;
		int rewind_result = Rewind_step_back();
		if (rewind_result == REWIND_STEP_OK) {
			// Actually stepped back - run one frame to render the restored state
			rewinding = 1;
			fast_forward = 0;
			core.run();
		} else if (rewind_result == REWIND_STEP_CADENCE) {
			// Waiting for cadence - don't run core, just re-render current frame
			rewinding = 1;
			fast_forward = 0;
			// Poll input manually since core.run() isn't called
			input_poll_callback();
			// Skip core.run() entirely to avoid advancing the game
		} else {
			int hold_empty = rewind_ctx.enabled && rewind_pressed && !rewind_toggle;
			if (hold_empty) {
				// Hold-to-rewind: freeze when empty to avoid advance/rewind oscillation.
				rewinding = was_rewinding ? 1 : 0;
				// Poll input manually so release is detected while core.run() is skipped
				input_poll_callback();
			} else {
				// Buffer empty: auto untoggle rewind, resume FF if it was paused for a hold
				if (rewind_toggle)
					rewind_toggle = 0;
				if (ff_paused_by_rewind_hold && ff_toggled) {
					ff_paused_by_rewind_hold = 0;
					fast_forward = setFastForward(1);
				}
				if (was_rewinding) {
					rewinding = 1;
					Rewind_sync_encode_state();
				}
				rewinding = 0;
				core.run();
				Rewind_push(0);
			}
		}
	} else {
		Rewind_sync_encode_state();
		rewinding = 0;
		if (ff_paused_by_rewind_hold && !rewind_pressed) {
			// resume fast forward after hold rewind ends
			if (ff_toggled)
				fast_forward = setFastForward(1);
			ff_paused_by_rewind_hold = 0;
		}

		int ff_runs = 1;
		if (fast_forward) {
			// when "None" is selected, assume a modest 2x instead of unbounded spam
			ff_runs = max_ff_speed ? max_ff_speed + 1 : 2;
		}

		for (int ff_step = 0; ff_step < ff_runs; ff_step++) {
			core.run();
			Rewind_push(0);
		}
	}
	limitFF();
	State_resumePending();
}
