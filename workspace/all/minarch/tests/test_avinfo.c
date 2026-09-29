#include <stdio.h>
#include <string.h>
#include "ma_avinfo.h"

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

static struct retro_system_av_info av(double fps, double rate, float aspect, unsigned bw, unsigned bh) {
	struct retro_system_av_info a;
	memset(&a, 0, sizeof(a));
	a.timing.fps = fps;
	a.timing.sample_rate = rate;
	a.geometry.aspect_ratio = aspect;
	a.geometry.base_width = bw;
	a.geometry.base_height = bh;
	return a;
}

int main(void) {
	// 1. aspect: explicit kept, <= 0 falls back to base size
	{
		struct retro_system_av_info a = av(60, 44100, 16.0f / 9.0f, 640, 480);
		CHECK(AVInfo_aspect(&a.geometry) > 1.777 && AVInfo_aspect(&a.geometry) < 1.778, "explicit 16:9 kept");
		a.geometry.aspect_ratio = 0;
		CHECK(AVInfo_aspect(&a.geometry) > 1.333 && AVInfo_aspect(&a.geometry) < 1.334, "aspect 0 -> base 640/480");
		a.geometry.aspect_ratio = -1;
		CHECK(AVInfo_aspect(&a.geometry) > 1.333 && AVInfo_aspect(&a.geometry) < 1.334, "negative aspect -> base");
	}

	// 2. timing classification
	{
		AVState cur = {60.0, 44100.0, 4.0 / 3.0};
		struct retro_system_av_info a = av(60, 44100, 4.0f / 3.0f, 640, 480);
		CHECK(AVInfo_classifyTiming(&cur, &a) == 0, "identical info -> no change");
		a = av(30, 44100, 4.0f / 3.0f, 640, 480);
		CHECK(AVInfo_classifyTiming(&cur, &a) == AV_CHANGE_AUDIO, "fps 60 -> 30 -> AUDIO");
		a = av(60, 48000, 4.0f / 3.0f, 640, 480);
		CHECK(AVInfo_classifyTiming(&cur, &a) == AV_CHANGE_AUDIO, "rate 44100 -> 48000 -> AUDIO");
		a = av(60, 44100, 16.0f / 9.0f, 640, 480);
		CHECK(AVInfo_classifyTiming(&cur, &a) == AV_CHANGE_ASPECT, "aspect 4:3 -> 16:9 -> ASPECT");
		a = av(30, 44100, 16.0f / 9.0f, 640, 480);
		CHECK(AVInfo_classifyTiming(&cur, &a) == (AV_CHANGE_AUDIO | AV_CHANGE_ASPECT), "fps + aspect -> both");
		a = av(60.0004, 44100, 4.0f / 3.0f, 640, 480);
		CHECK(AVInfo_classifyTiming(&cur, &a) == 0, "fps delta < 0.001 -> no change");
	}

	// 3. geometry classification
	{
		AVState cur = {60.0, 44100.0, 4.0 / 3.0};
		struct retro_system_av_info a = av(60, 44100, 4.0f / 3.0f, 640, 480);
		CHECK(AVInfo_classifyGeometry(&cur, &a.geometry) == 0, "same aspect -> no change");
		a.geometry.aspect_ratio = 16.0f / 9.0f;
		CHECK(AVInfo_classifyGeometry(&cur, &a.geometry) == AV_CHANGE_ASPECT, "geometry aspect change -> ASPECT");
	}

	// 4. sync source decision
	CHECK(SyncRef_useCoreFps(AVSYNC_SCREEN, 1, 30.0, 60.235) == 0, "SCREEN always screen");
	CHECK(SyncRef_useCoreFps(AVSYNC_CORE, 0, 60.0, 60.235) == 1, "CORE always core");
	CHECK(SyncRef_useCoreFps(AVSYNC_AUTO, 1, 50.0, 60.235) == 1, "AUTO PAL -> core");
	CHECK(SyncRef_useCoreFps(AVSYNC_AUTO, 0, 60.0, 60.235) == 0, "AUTO NTSC 60 on 60.235 -> screen");
	CHECK(SyncRef_useCoreFps(AVSYNC_AUTO, 0, 59.94, 62.948) == 0, "AUTO NTSC 59.94 on 62.948 -> screen");
	CHECK(SyncRef_useCoreFps(AVSYNC_AUTO, 0, 30.0, 60.235) == 1, "AUTO 30 fps on 60.235 -> core");
	CHECK(SyncRef_useCoreFps(AVSYNC_AUTO, 0, 30.0, 62.948) == 1, "AUTO 30 fps on 62.948 -> core");

	CHECK(SyncRef_useCoreFps(AVSYNC_EMULATED, 0, 60.0, 60.235) == 1, "EMULATED paces by core fps for audio");

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
	return fails ? 1 : 0;
}
