#include "ma_archive_cache.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// FNV-1a, 64-bit
static uint64_t hash_bytes(uint64_t h, const void* data, size_t len) {
	const unsigned char* p = data;
	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

int ArchiveCache_dir(char* out, size_t size, const char* root, const char* archive_path) {
	struct stat st;
	if (stat(archive_path, &st) != 0)
		return -1;

	// size and mtime too, so a replaced archive at the same path is unpacked again
	int64_t stamp[2] = {(int64_t)st.st_size, (int64_t)st.st_mtime};
	uint64_t h = hash_bytes(0xcbf29ce484222325ULL, archive_path, strlen(archive_path));
	h = hash_bytes(h, stamp, sizeof(stamp));

	int n = snprintf(out, size, "%s/%016llx", root, (unsigned long long)h);
	return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static int has_extension(const char* name, char** extensions) {
	size_t len = strlen(name);
	for (int e = 0; extensions[e]; e++) {
		size_t ext_len = strlen(extensions[e]);
		if (len > ext_len + 1 && name[len - ext_len - 1] == '.' && !strcmp(name + len - ext_len, extensions[e]))
			return 1;
	}
	return 0;
}

int ArchiveCache_find(const char* dir, char** extensions, char* out, size_t size) {
	DIR* d = opendir(dir);
	if (!d)
		return 0;

	int found = 0;
	struct dirent* entry;
	while (!found && (entry = readdir(d))) {
		if (entry->d_name[0] == '.' || !has_extension(entry->d_name, extensions))
			continue;
		int n = snprintf(out, size, "%s/%s", dir, entry->d_name);
		if (n < 0 || (size_t)n >= size)
			continue;
		struct stat st;
		found = stat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
	}
	closedir(d);
	if (!found && size)
		out[0] = '\0';
	return found;
}
