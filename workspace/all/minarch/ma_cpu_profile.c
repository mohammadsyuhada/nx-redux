#include "ma_cpu_profile.h"
#include <strings.h>

int cpu_profile_min_mhz = 0;
int cpu_profile_max_mhz = 0;

int cpu_profile_affinity = CPU_AFFINITY_NONE;

CpuAffinity CpuProfile_parseAffinity(const char* value) {
	if (!value)
		return CPU_AFFINITY_NONE;
	if (strcasecmp(value, "big") == 0)
		return CPU_AFFINITY_BIG;
	if (strcasecmp(value, "little") == 0)
		return CPU_AFFINITY_LITTLE;
	return CPU_AFFINITY_NONE;
}

static int clampKhz(int khz, int lo, int hi) {
	if (khz < lo)
		return lo;
	if (khz > hi)
		return hi;
	return khz;
}

void CpuProfile_rangeKhz(int min_mhz, int max_mhz, int hw_min_khz, int hw_max_khz, int* out_min_khz, int* out_max_khz) {
	int lo = min_mhz > 0 ? clampKhz(min_mhz * 1000, hw_min_khz, hw_max_khz) : hw_min_khz;
	int hi = max_mhz > 0 ? clampKhz(max_mhz * 1000, hw_min_khz, hw_max_khz) : hw_max_khz;
	if (lo > hi)
		lo = hi;
	*out_min_khz = lo;
	*out_max_khz = hi;
}
