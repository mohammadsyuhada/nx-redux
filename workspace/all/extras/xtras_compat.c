#include "xtras_compat.h"
#include <string.h>

static bool span_eq(const char* s, size_t n, const char* t) {
	return t && strlen(t) == n && strncmp(s, t, n) == 0;
}

bool xtras_platform_compatible(const char* platforms, const char* plat,
							   const char* os) {
	if (!platforms)
		return true;
	const char* p = platforms;
	bool any = false;
	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		const char* start = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		size_t n = (size_t)(p - start);
		if (n == 0)
			continue;
		any = true;
		if (span_eq(start, n, plat))
			return true;
		if (os && *os && span_eq(start, n, os))
			return true;
	}
	return !any; // whitespace-only / empty -> fail-open compatible
}
