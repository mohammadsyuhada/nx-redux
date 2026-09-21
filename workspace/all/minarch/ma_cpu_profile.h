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

// Per-pak thread affinity, from default.cfg (or the user's minarch.cfg):
//   minarch_cpu_affinity = none | big | little
// "big": the emulation thread and every core-created thread on the device's
// FAST set, minarch's helpers and the GPU driver's threads on SLOW; "little":
// the whole process on SLOW; "none": today's behaviour (no explicit pinning).
// See PLAT_pinToCoreSet (api.h) for the per-device FAST/SLOW sets.
typedef enum { CPU_AFFINITY_NONE = 0,
			   CPU_AFFINITY_BIG,
			   CPU_AFFINITY_LITTLE } CpuAffinity;
extern int cpu_profile_affinity; // CpuAffinity, default NONE. Set by Config_readOptionsString.

// "none"/"big"/"little", case-insensitive; anything else (incl. NULL) -> NONE.
// The pure parser lives in ma_cpu_profile.c (host-testable, no PLAT/PWR deps).
CpuAffinity CpuProfile_parseAffinity(const char* value);

// Apply the parsed affinity. Call once, after config is read and BEFORE
// Core_init so the core's threads inherit the main thread's placement.
// No-op for NONE. Defined in ma_cpu_affinity.c (pulls in api.h / PLAT / PWR).
void CpuProfile_applyAffinity(void);

// Second sweep ~2 s after the first frame for the GPU driver's lazily created
// mali-* threads. No-op unless BIG. Defined in ma_cpu_affinity.c.
void CpuProfile_lateSweep(void);

#endif
