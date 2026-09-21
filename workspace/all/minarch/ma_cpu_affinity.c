// Thread-placement side of the per-pak minarch_cpu_affinity key. The parser and
// the cpu_profile_affinity state live in the host-testable ma_cpu_profile.c;
// these two functions do the actual pinning, so they pull in api.h (PLAT_* /
// PWR_*) and stay out of the pure module the host unit test links.
#include "ma_cpu_profile.h"
#include "api.h"

void CpuProfile_applyAffinity(void) {
	switch (cpu_profile_affinity) {
	case CPU_AFFINITY_BIG:
		// Everything alive now is minarch/SDL/driver (no core thread exists
		// before Core_init): sweep them to SLOW, then pin this (main) thread to
		// FAST so the core's threads inherit it. Helpers created later pin
		// themselves via PWR_pinHelperThread.
		PWR_setHelperThreadCoreSet(CPU_SET_SLOW);
		PLAT_pinOtherThreadsToCoreSet(CPU_SET_SLOW);
		PLAT_pinToCoreSet(CPU_SET_FAST);
		break;
	case CPU_AFFINITY_LITTLE:
		PWR_setHelperThreadCoreSet(CPU_SET_SLOW);
		PLAT_pinOtherThreadsToCoreSet(CPU_SET_SLOW);
		PLAT_pinToCoreSet(CPU_SET_SLOW);
		break;
	default:
		break;
	}
}

void CpuProfile_lateSweep(void) {
	if (cpu_profile_affinity == CPU_AFFINITY_BIG)
		PLAT_pinThreadsByCommToCoreSet("mali-", CPU_SET_SLOW);
}
