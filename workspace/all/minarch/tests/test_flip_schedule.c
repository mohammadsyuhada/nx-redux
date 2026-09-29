#include <stdio.h>
#include "flip_schedule.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

int main(void) {
	const int64_t d = 1000; // nominal frame in ticks
	FlipSchedule s = {0};

	CHECK(FlipSchedule_next(&s, 5000, d, d) == 5000, "first call anchors at now (no wait)");
	CHECK(FlipSchedule_next(&s, 5100, d, d) == 6000, "on time: target = previous + slot");
	CHECK(FlipSchedule_next(&s, 6500, d, d) == 7000, "ahead of schedule within 2 frames: wait for the target");

	// 1000 steady slots from a fresh anchor: no drift
	FlipSchedule t = {0};
	int64_t now = 0, target = FlipSchedule_next(&t, now, d, d);
	for (int i = 0; i < 1000; i++)
		target = FlipSchedule_next(&t, target, d, d);
	CHECK(target == 1000 * d, "1000 steady slots advance exactly 1000*d");

	FlipSchedule u = {0};
	FlipSchedule_next(&u, 0, d, d);
	CHECK(FlipSchedule_next(&u, 3500, d, d) == 3500, "more than 2 frames late resets to now");
	CHECK(FlipSchedule_next(&u, 3600, d, d) == 4500, "after a late reset the schedule continues from now");

	FlipSchedule v = {0};
	FlipSchedule_next(&v, 10000, d, d);
	FlipSchedule_next(&v, 10000, d, d); // target 11000
	CHECK(FlipSchedule_next(&v, 7000, d, d) == 7000, "more than 2 frames early resets to now");

	FlipSchedule w = {0};
	FlipSchedule_next(&w, 0, d, d);
	CHECK(FlipSchedule_next(&w, 100, 2 * d, d) == 2 * d, "a 2-frame slot schedules two frames ahead");
	CHECK(FlipSchedule_next(&w, 2100, d, d) == 3 * d, "varying slots keep the anchor (no reset)");

	FlipSchedule x = {0};
	FlipSchedule_next(&x, 0, d, d);
	CHECK(FlipSchedule_next(&x, 50, 2 * d, 2 * d) == 50, "nominal (fps) change re-anchors at now");

	FlipSchedule y = {0};
	FlipSchedule_next(&y, 0, d, d);
	CHECK(FlipSchedule_next(&y, 400, 0, d) == 0, "zero slot: target not in the future (no wait)");

	// a 3-vblank slot (20 fps game) must be waited for, not treated as "early"
	FlipSchedule z = {0};
	FlipSchedule_next(&z, 0, d, d);
	CHECK(FlipSchedule_next(&z, 100, 3 * d, d) == 3 * d, "3-frame slot: wait for it (no early reset)");
	CHECK(FlipSchedule_next(&z, 3100, 4 * d, d) == 7 * d, "4-frame slot (clamp limit): wait for it");
	FlipSchedule e = {0};
	FlipSchedule_next(&e, 10000, d, d);
	CHECK(FlipSchedule_next(&e, 9500, 3 * d, d) == 13000, "3-frame slot, 3.5 frames ahead: still scheduled");
	FlipSchedule g = {0};
	FlipSchedule_next(&g, 10000, d, d);
	CHECK(FlipSchedule_next(&g, 4000, 3 * d, d) == 4000, "far beyond slot + 2 frames early still resets");

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
	return fails ? 1 : 0;
}
