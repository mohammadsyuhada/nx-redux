#include "ma_internal.h"
#include "ma_audio.h"
#include "ma_rewind.h"
#include "ma_emutime.h"
#include <msettings.h>

void audio_sample_callback(int16_t left, int16_t right) {
	EmuTime_countAudio(1);
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
// NX_AUDIO_BLOCK=1 (set by a pak's launch.sh): for cores that run emulation
// on their own thread and expect the frontend to block their audio push, as
// RetroArch's audio sync does (DCX.pak's old flycast). The push waits until
// the buffer is at most half full: 1 ms steps, at most 100 ms per call so a
// stalled device can't hang the core, and never while fast-forwarding.
// With the core's threaded rendering turned off, it pushes audio from
// retro_run on minarch's main thread instead, so the wait pauses that thread:
// plain audio sync, which paces that mode correctly too.
static int audio_block = -1;
static void waitForAudioRoom(void) {
	if (audio_block < 0)
		audio_block = getenv("NX_AUDIO_BLOCK") && atoi(getenv("NX_AUDIO_BLOCK")) == 1;
	if (!audio_block || fast_forward)
		return;
	for (int i = 0; i < 100 && SND_bufferOccupancy() > 0.5f; i++)
		usleep(1000);
}
size_t audio_sample_batch_callback(const int16_t* data, size_t frames) {
	waitForAudioRoom();
	EmuTime_countAudio(frames);
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
