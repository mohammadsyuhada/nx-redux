#include "ma_internal.h"
#include "ma_audio.h"
#include "ma_rewind.h"
#include "audio_manager.h"
#include <msettings.h>

void audio_sample_callback(int16_t left, int16_t right) {
	if (rewinding && !rewind_ctx.audio)
		return;
	if (!fast_forward || ff_audio) {
		if (use_core_fps || fast_forward) {
			SND_batchSamples_fixed_rate(&(const SND_Frame){left, right}, 1);
		} else {
			SND_batchSamples(&(const SND_Frame){left, right}, 1);
		}
	}
}
size_t audio_sample_batch_callback(const int16_t* data, size_t frames) {
	if (rewinding && !rewind_ctx.audio)
		return frames;
	if (!fast_forward || ff_audio) {
		if (use_core_fps || fast_forward) {
			return SND_batchSamples_fixed_rate((const SND_Frame*)data, frames);
		} else {
			return SND_batchSamples((const SND_Frame*)data, frames);
		}
	} else
		return frames;
}

// We need to do this on the audio thread (aka main thread currently)
static bool resetAudio = false;

void Audio_onSinkChanged(int sink_type) {
	(void)sink_type;
	resetAudio = true;
}


// Reset audio on the main (audio) thread after a sink change was flagged.
void Audio_checkAndResetIfNeeded(void) {
	if (resetAudio) {
		resetAudio = false;
		SND_resetAudio(core.sample_rate, core.fps);
		SetVolume(GetVolume());
	}
}
