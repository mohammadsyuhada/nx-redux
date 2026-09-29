#ifndef FLIP_SCHEDULE_H
#define FLIP_SCHEDULE_H
// Present scheduling for GFX_flip_scheduled, kept pure so it can be host-tested.
// Each present is due one slot after the previous one. The slot is 1/fps for
// Native sync, or the emulated time a GPU core covered for Emulated sync.
// More than FLIP_SCHEDULE_MAX_LOST nominal frames late or early re-anchors at
// "now" (the long-standing GFX_flip_fixed_rate rule), and so does a change of
// the nominal frame (an fps change). Per-present slot changes never re-anchor.
#include <stdint.h>

#define FLIP_SCHEDULE_MAX_LOST 2

typedef struct {
	int anchored;
	int64_t target;	 // tick the previous present was due at
	int64_t nominal; // nominal frame (1/fps) the schedule runs at
} FlipSchedule;

static inline int64_t FlipSchedule_next(FlipSchedule* s, int64_t now, int64_t slot, int64_t nominal) {
	if (!s->anchored || nominal != s->nominal) {
		s->anchored = 1;
		s->nominal = nominal;
		s->target = now;
		return now;
	}
	int64_t target = s->target + slot;
	int64_t offset = now - target;
	int64_t limit = FLIP_SCHEDULE_MAX_LOST * nominal;
	if (offset > limit || offset < -limit) {
		s->target = now;
		return now;
	}
	s->target = target;
	return target;
}
#endif
