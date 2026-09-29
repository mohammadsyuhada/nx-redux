#include "ma_emutime.h"

static unsigned long long pending_frames = 0;
static unsigned long long total_frames = 0;

void EmuTime_countAudio(size_t frames) {
	pending_frames += frames;
	total_frames += frames;
}

double EmuTime_takeSlot(double sample_rate, double fps) {
	double frame = 1.0 / (fps > 0 ? fps : 60.0);
	unsigned long long counted = pending_frames;
	pending_frames = 0;
	if (counted == 0 || sample_rate <= 0)
		return frame; // silent core, or first present: one frame
	double slot = counted / sample_rate;
	if (slot < 0.5 * frame)
		slot = 0.5 * frame;
	else if (slot > 4 * frame)
		slot = 4 * frame; // a burst (resume, state load) must not stall video
	return slot;
}

void EmuTime_reset(void) {
	pending_frames = 0;
}

unsigned long long EmuTime_totalFrames(void) {
	return total_frames;
}
