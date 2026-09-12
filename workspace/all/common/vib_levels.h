#ifndef VIB_LEVELS_H
#define VIB_LEVELS_H

// "Vibration strength" (Settings → System). Persisted by libmsettings as
// rumble_strength; 0 is the default so pre-existing msettings.bin files read
// as Normal. Each level is the range every non-zero rumble request is mapped
// into before it reaches the motor: the weakest request lands on the floor,
// a full request on the ceiling. Floors sit at or above the point where the
// motor is felt at all (Smart Pro S: a third of full drive is invisible, 60%
// registers — calibrated 2026-09-12); Light deliberately dips below it.
#define VIB_LEVEL_NORMAL 0
#define VIB_LEVEL_LIGHT 1
#define VIB_LEVEL_STRONG 2

static inline void VIB_levelRange(int level, int* floor_pct, int* ceil_pct) {
	switch (level) {
	case VIB_LEVEL_LIGHT:
		*floor_pct = 45;
		*ceil_pct = 80;
		break;
	case VIB_LEVEL_STRONG:
		*floor_pct = 80;
		*ceil_pct = 100;
		break;
	default:
		*floor_pct = 60;
		*ceil_pct = 100;
		break;
	}
}

#endif
