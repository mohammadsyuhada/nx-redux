// Host unit test: minarch_cpu_min / minarch_cpu_max (MHz, from a pak's
// default.cfg) -> the kHz range handed to PLAT_setCPUSpeedRange.
#include <stdio.h>
#include "ma_cpu_profile.h"

static int failures = 0;
#define CHECK(cond, ...)                                \
	do {                                                \
		if (!(cond)) {                                  \
			failures++;                                 \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);                        \
			printf("\n");                               \
		}                                               \
	} while (0)

static void range(int min_mhz, int max_mhz, int want_min, int want_max, const char* what) {
	int lo = -1, hi = -1;
	CpuProfile_rangeKhz(min_mhz, max_mhz, 408000, 2000000, &lo, &hi);
	CHECK(lo == want_min && hi == want_max, "%s: got %d-%d want %d-%d", what, lo, hi, want_min, want_max);
}

static void test_affinity_parse(void) {
	CHECK(CpuProfile_parseAffinity("big") == CPU_AFFINITY_BIG, "big");
	CHECK(CpuProfile_parseAffinity("Big") == CPU_AFFINITY_BIG, "case-insensitive");
	CHECK(CpuProfile_parseAffinity("little") == CPU_AFFINITY_LITTLE, "little");
	CHECK(CpuProfile_parseAffinity("none") == CPU_AFFINITY_NONE, "none");
	CHECK(CpuProfile_parseAffinity("") == CPU_AFFINITY_NONE, "empty = none");
	CHECK(CpuProfile_parseAffinity("performance") == CPU_AFFINITY_NONE, "unknown = none");
	CHECK(CpuProfile_parseAffinity(NULL) == CPU_AFFINITY_NONE, "NULL = none");
}

int main(void) {
	range(0, 0, 408000, 2000000, "no keys = full platform range");
	range(0, 1008, 408000, 1008000, "max only");
	range(600, 0, 600000, 2000000, "min only");
	range(816, 1416, 816000, 1416000, "both");
	range(0, 5000, 408000, 2000000, "max above hw clamps to hw max");
	range(100, 0, 408000, 2000000, "min below hw clamps to hw min");
	range(1608, 1200, 1200000, 1200000, "min above max collapses to a fixed clock at max");
	range(-5, -5, 408000, 2000000, "negative = unset");
	test_affinity_parse();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("ma_cpu_profile: all tests passed\n");
	return 0;
}
