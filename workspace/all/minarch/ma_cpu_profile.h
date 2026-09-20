#ifndef MA_CPU_PROFILE_H
#define MA_CPU_PROFILE_H

// Per-pak CPU range, from the pak's default.cfg (or the user's minarch.cfg):
//   minarch_cpu_min = <MHz>   optional, default: platform CPU_FREQ_MIN
//   minarch_cpu_max = <MHz>   optional, default: hardware max
// Consumed by setOverclock(3) ("Auto"): schedutil between min and max on the
// policies minarch's platform drives (PLAT_setCPUSpeedRange). Not a menu item.
// 0 (or negative) = unset. Set by Config_readOptionsString.
extern int cpu_profile_min_mhz;
extern int cpu_profile_max_mhz;

// Validated kHz range: unset -> platform default, clamped into [hw_min, hw_max],
// a crossed range collapses to a fixed clock at max.
void CpuProfile_rangeKhz(int min_mhz, int max_mhz, int hw_min_khz, int hw_max_khz, int* out_min_khz, int* out_max_khz);

#endif
