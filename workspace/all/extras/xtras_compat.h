#ifndef XTRAS_COMPAT_H
#define XTRAS_COMPAT_H
#include <stdbool.h>

// Is a catalog entry whose meta.txt "platforms=" value is `platforms`
// compatible with a build whose PLATFORM string is `plat` and whose
// desktop-OS token is `os` ("" when this is not a desktop build)?
//
// `platforms` is a space/tab-separated list of platform tokens (e.g.
// "tg5040 tg5050", "macos linux", "desktop"). NULL, empty, or all-blank
// means compatible everywhere (fail-open). Otherwise compatible iff at
// least one token equals `plat`, or equals a non-empty `os`. Exact,
// case-sensitive.
bool xtras_platform_compatible(const char* platforms, const char* plat,
							   const char* os);

#endif
