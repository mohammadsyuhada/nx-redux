// Art-path helpers, split into their own TU so host tests can link them
// without the ScreenScraper API/network stack that scraper_core.c pulls in.
#include "scraper_core.h"

#include <stdio.h>
#include <string.h>

void Scraper_variantPath(const char* out_png, const char* variant,
						 char* out, size_t out_size) {
	if (!variant || !variant[0]) {
		snprintf(out, out_size, "%s", out_png);
		return;
	}

	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", out_png);
	char* slash = strrchr(dir, '/');
	if (!slash) {
		snprintf(out, out_size, "%s/%s", variant, out_png);
		return;
	}
	*slash = '\0';
	snprintf(out, out_size, "%s/%s/%s", dir, variant, slash + 1);
}
