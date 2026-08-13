#define _GNU_SOURCE
#include "net_cache.h"
#include "wget_fetch.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

static bool file_nonempty(const char* p) {
	struct stat st;
	return stat(p, &st) == 0 && st.st_size > 0;
}

int NetCache_ensureDir(const char* dir) {
	if (mkdir(dir, 0755) == 0)
		return 0;
	return (errno == EEXIST) ? 0 : -1;
}

int NetCache_ensure(const char* url, const char* cache_path, bool force,
					volatile bool* should_stop, volatile int* progress_pct) {
	if (!force && file_nonempty(cache_path))
		return 0;

	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", cache_path);
	unlink(tmp);

	int n = wget_download_file(url, tmp, progress_pct, should_stop, NULL, NULL);
	if (n < 0 || !file_nonempty(tmp)) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, cache_path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}
