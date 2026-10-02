#ifndef XTRAS_COMPAT_H
#define XTRAS_COMPAT_H
#include <stdbool.h>

// Is a catalog entry whose meta.txt "platforms=" value is `platforms`
// compatible with a build whose PLATFORM string is `plat`?
//
// `platforms` is a space/tab-separated list of platform tokens (e.g.
// "tg5040 tg5050"). NULL, empty, or all-blank means compatible everywhere
// (fail-open). Otherwise compatible iff at least one token equals `plat`.
// Exact, case-sensitive.
bool xtras_platform_compatible(const char* platforms, const char* plat);

#endif
