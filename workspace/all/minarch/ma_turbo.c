#include <string.h>
#include "ma_turbo.h"

void Turbo_reset(TurboState* s) {
	memset(s, 0, sizeof(*s));
}

int Turbo_step(TurboState* s, int btn_id, int activity) {
	if (btn_id < 0 || btn_id >= TURBO_MAX_BUTTONS)
		return 0;
	TurboButton* b = &s->btn[btn_id];

	if (activity)
		b->idle = 0;
	else if (b->idle < 255)
		b->idle++;

	if (!b->active) {
		if (!activity)
			return 0;
		// First frame of a hold fires immediately.
		b->active = 1;
		b->phase = 0;
	} else if (!activity && b->idle > TURBO_RELEASE_GRACE) {
		// The daemon's pulses (or the real press) stopped: hold is over.
		b->active = 0;
		return 0;
	}

	int pressed = b->phase < TURBO_ON_FRAMES;
	b->phase = (uint8_t)((b->phase + 1) % (TURBO_ON_FRAMES + TURBO_OFF_FRAMES));
	return pressed;
}
