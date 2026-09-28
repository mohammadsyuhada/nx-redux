#include <math.h>
#include "ma_avinfo.h"

#define AV_EPSILON 0.001

double AVInfo_aspect(const struct retro_game_geometry* g) {
	if (g->aspect_ratio > 0)
		return g->aspect_ratio;
	if (g->base_height == 0)
		return 0;
	return (double)g->base_width / g->base_height;
}

static int differs(double a, double b) {
	return fabs(a - b) >= AV_EPSILON;
}

int AVInfo_classifyGeometry(const AVState* cur, const struct retro_game_geometry* next) {
	double aspect = AVInfo_aspect(next);
	return (aspect > 0 && differs(aspect, cur->aspect)) ? AV_CHANGE_ASPECT : 0;
}

int AVInfo_classifyTiming(const AVState* cur, const struct retro_system_av_info* next) {
	int changes = AVInfo_classifyGeometry(cur, &next->geometry);
	if (differs(next->timing.fps, cur->fps) || differs(next->timing.sample_rate, cur->sample_rate))
		changes |= AV_CHANGE_AUDIO;
	return changes;
}

int SyncRef_useCoreFps(int sync_ref, int is_pal, double core_fps, double screen_fps) {
	switch (sync_ref) {
	case AVSYNC_SCREEN:
		return 0;
	case AVSYNC_CORE:
		return 1;
	default:
		// A core far below the panel rate (a 30 fps game reported by flycast's
		// frame-rate detection) cannot pace by vsync: every swap would run it
		// again at the panel rate. PAL keeps its existing core-fps pacing.
		return is_pal || core_fps < screen_fps * 0.75;
	}
}
