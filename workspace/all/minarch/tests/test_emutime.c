#include <math.h>
#include <stdio.h>
#include "ma_emutime.h"

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
#define NEAR(a, b) (fabs((a) - (b)) < 1e-6)

int main(void) {
	const double rate = 44100.0, fps = 60.0, frame = 1.0 / 60.0;

	EmuTime_reset();
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), frame), "nothing counted -> one frame");

	EmuTime_countAudio(735);
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), 735.0 / rate), "735 frames -> 16.67 ms");
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), frame), "take clears pending");

	EmuTime_countAudio(1470);
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), 1470.0 / rate), "1470 frames -> 33.3 ms (30 fps game)");

	EmuTime_countAudio(700);
	EmuTime_countAudio(770);
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), 1470.0 / rate), "counts accumulate across calls");

	EmuTime_countAudio(10000);
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), 4 * frame), "burst clamped to 4 frames");

	EmuTime_countAudio(100);
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), 0.5 * frame), "tiny count clamped to half a frame");

	EmuTime_countAudio(735);
	CHECK(NEAR(EmuTime_takeSlot(0, fps), frame), "sample rate 0 -> one frame");
	EmuTime_countAudio(735);
	CHECK(NEAR(EmuTime_takeSlot(rate, 0), 735.0 / rate), "fps 0 -> 1/60 frame for clamps, value kept");

	EmuTime_countAudio(1470);
	CHECK(NEAR(EmuTime_takeSlot(rate, 29.97), 1470.0 / rate), "reported 29.97 fps: slot still from audio");

	EmuTime_countAudio(5000);
	EmuTime_reset();
	CHECK(NEAR(EmuTime_takeSlot(rate, fps), frame), "reset drops pending");

	unsigned long long before = EmuTime_totalFrames();
	EmuTime_countAudio(735);
	EmuTime_reset();
	CHECK(EmuTime_totalFrames() == before + 735, "total is monotonic, unaffected by reset/take");

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
	return fails ? 1 : 0;
}
