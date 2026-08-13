// Host-compiled unit test for net_cache.c (wget stubbed).
// Build & run:
//   cc -I. -I../common ../common/net_cache.c tests/test_net_cache.c -o /tmp/test_net_cache && /tmp/test_net_cache
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../../common/net_cache.h"

// --- stub wget: "download" = write the URL string into filepath ---
static int g_wget_calls = 0;
int wget_download_file(const char* url, const char* filepath,
					   volatile int* progress_pct, volatile bool* should_stop,
					   volatile int* speed, volatile int* eta) {
	(void)progress_pct;
	(void)should_stop;
	(void)speed;
	(void)eta;
	g_wget_calls++;
	FILE* f = fopen(filepath, "w");
	if (!f)
		return -1;
	fputs(url, f);
	fclose(f);
	return (int)strlen(url);
}

static int exists(const char* p) {
	struct stat s;
	return stat(p, &s) == 0;
}

int main(void) {
	system("rm -rf /tmp/iptv-net-test");
	assert(NetCache_ensureDir("/tmp/iptv-net-test") == 0);
	assert(exists("/tmp/iptv-net-test"));
	assert(NetCache_ensureDir("/tmp/iptv-net-test") == 0); // idempotent

	const char* cache = "/tmp/iptv-net-test/c.json";

	// Miss -> downloads.
	g_wget_calls = 0;
	assert(NetCache_ensure("http://example/c.json", cache, false, NULL, NULL) == 0);
	assert(g_wget_calls == 1);
	assert(exists(cache));
	assert(!exists("/tmp/iptv-net-test/c.json.tmp")); // temp renamed away

	// Hit -> no download.
	g_wget_calls = 0;
	assert(NetCache_ensure("http://example/c.json", cache, false, NULL, NULL) == 0);
	assert(g_wget_calls == 0);

	// force -> re-download even though cached.
	g_wget_calls = 0;
	assert(NetCache_ensure("http://example/c.json", cache, true, NULL, NULL) == 0);
	assert(g_wget_calls == 1);

	printf("test_net_cache: OK\n");
	return 0;
}
