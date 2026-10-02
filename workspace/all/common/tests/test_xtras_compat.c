#include <assert.h>
#include <stdio.h>
#include "../../extras/xtras_compat.h"

int main(void) {
	// fail-open: absent / blank / NULL
	assert(xtras_platform_compatible("", "tg5040"));
	assert(xtras_platform_compatible("   ", "tg5050"));
	assert(xtras_platform_compatible(NULL, "tg5040"));
	// device entries
	assert(xtras_platform_compatible("tg5040 tg5050", "tg5040"));
	assert(xtras_platform_compatible("tg5040 tg5050", "tg5050"));
	assert(xtras_platform_compatible("tg5040", "tg5040"));
	assert(!xtras_platform_compatible("tg5040", "tg5050"));
	// tokens for other platforms never match a device build
	assert(!xtras_platform_compatible("linux", "tg5040"));
	assert(!xtras_platform_compatible("macos linux", "tg5050"));
	// whitespace tolerance
	assert(xtras_platform_compatible("  tg5040   tg5050  ", "tg5050"));
	// exact-match guard (span_eq strlen==n): a token that is a prefix or a
	// proper superstring of plat must NOT match.
	assert(!xtras_platform_compatible("tg50", "tg5040"));
	assert(!xtras_platform_compatible("tg5040", "tg5"));
	// a tab is tolerated as a token separator (not just a space)
	assert(xtras_platform_compatible("tg5040\ttg5050", "tg5050"));
	// matching is case-sensitive
	assert(!xtras_platform_compatible("TG5040", "tg5040"));
	printf("test_xtras_compat: OK\n");
	return 0;
}
