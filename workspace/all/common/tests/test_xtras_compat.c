#include <assert.h>
#include <stdio.h>
#include "../../extras/xtras_compat.h"

int main(void) {
	// fail-open: absent / blank / NULL
	assert(xtras_platform_compatible("", "tg5040", ""));
	assert(xtras_platform_compatible("   ", "desktop", "macos"));
	assert(xtras_platform_compatible(NULL, "desktop", "linux"));
	// device-only entry (the 3 current entries)
	assert(xtras_platform_compatible("tg5040 tg5050", "tg5040", ""));
	assert(xtras_platform_compatible("tg5040 tg5050", "tg5050", ""));
	assert(!xtras_platform_compatible("tg5040 tg5050", "desktop", "macos"));
	assert(!xtras_platform_compatible("tg5040 tg5050", "desktop", "linux"));
	// umbrella "desktop"
	assert(xtras_platform_compatible("desktop", "desktop", "macos"));
	assert(xtras_platform_compatible("desktop", "desktop", "linux"));
	assert(!xtras_platform_compatible("desktop", "tg5040", ""));
	// explicit OS granularity (the point of macos/linux)
	assert(xtras_platform_compatible("macos linux", "desktop", "macos"));
	assert(xtras_platform_compatible("macos linux", "desktop", "linux"));
	assert(xtras_platform_compatible("linux", "desktop", "linux"));
	assert(!xtras_platform_compatible("linux", "desktop", "macos"));
	assert(!xtras_platform_compatible("macos", "desktop", "linux"));
	assert(!xtras_platform_compatible("linux", "tg5040", "")); // device != linux
	// all four
	assert(xtras_platform_compatible("tg5040 tg5050 macos linux", "tg5040", ""));
	assert(xtras_platform_compatible("tg5040 tg5050 macos linux", "desktop", "macos"));
	// whitespace tolerance
	assert(xtras_platform_compatible("  tg5040   tg5050  ", "tg5050", ""));
	// exact-match guard (span_eq strlen==n): a token that is a prefix or a
	// proper superstring of plat must NOT match.
	assert(!xtras_platform_compatible("tg50", "tg5040", ""));
	assert(!xtras_platform_compatible("tg5040", "tg5", ""));
	// a tab is tolerated as a token separator (not just a space)
	assert(xtras_platform_compatible("tg5040\ttg5050", "tg5050", ""));
	// matching is case-sensitive
	assert(!xtras_platform_compatible("DESKTOP", "desktop", "macos"));
	printf("test_xtras_compat: OK\n");
	return 0;
}
