#ifndef MA_AVINFO_H
#define MA_AVINFO_H
// Classifies a core's SET_SYSTEM_AV_INFO / SET_GEOMETRY against the current
// state, and decides the frame-pacing source. Pure, so it is host-testable.
#include "libretro.h"

typedef struct {
	double fps;
	double sample_rate;
	double aspect;
} AVState;

enum {
	AV_CHANGE_AUDIO = 1, // fps or sample rate: audio re-init + sync re-pick
	AV_CHANGE_ASPECT = 2 // display aspect: re-run the scaler
};

// Same order as SYNC_SRC_* in ma_internal.h (asserted in ma_runframe.c).
enum {
	AVSYNC_AUTO = 0,
	AVSYNC_SCREEN = 1,
	AVSYNC_CORE = 2,
	AVSYNC_EMULATED = 3 // GPU cores: presents paced by emulated time (ma_emutime)
};

double AVInfo_aspect(const struct retro_game_geometry* g);
int AVInfo_classifyTiming(const AVState* cur, const struct retro_system_av_info* next);
int AVInfo_classifyGeometry(const AVState* cur, const struct retro_game_geometry* next);
int SyncRef_useCoreFps(int sync_ref, int is_pal, double core_fps, double screen_fps);
#endif
