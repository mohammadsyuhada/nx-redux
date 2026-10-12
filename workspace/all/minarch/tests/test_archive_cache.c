// Host test for ma_archive_cache.c: per-archive extraction folders (issue #222).
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I workspace/all/minarch \
//      workspace/all/minarch/ma_archive_cache.c workspace/all/minarch/tests/test_archive_cache.c \
//      -o /tmp/test_archive_cache && /tmp/test_archive_cache
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "ma_archive_cache.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	fputs(content, f);
	fclose(f);
}

static void set_mtime(const char* path, long sec) {
	struct timeval tv[2] = {{sec, 0}, {sec, 0}};
	utimes(path, tv);
}

int main(void) {
	char root[] = "/tmp/nx_test_archive_cacheXXXXXX";
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	char roms[600], cache[600];
	snprintf(roms, sizeof(roms), "%s/Roms", root);
	snprintf(cache, sizeof(cache), "%s/FC", root);
	mkdir(roms, 0777);
	mkdir(cache, 0777);

	char dd[700], dd2[700], dd_hack[700];
	snprintf(dd, sizeof(dd), "%s/Double Dragon.zip", roms);
	snprintf(dd2, sizeof(dd2), "%s/Double Dragon II.zip", roms);
	snprintf(dd_hack, sizeof(dd_hack), "%s/Hacks", roms);
	mkdir(dd_hack, 0777);
	strcat(dd_hack, "/Double Dragon.zip");
	write_file(dd, "zip one");
	write_file(dd2, "zip two");
	write_file(dd_hack, "zip one"); // same name and size, other folder
	set_mtime(dd, 1000);
	set_mtime(dd2, 1000);
	set_mtime(dd_hack, 1000);

	char dir_dd[800], dir_dd2[800], dir_hack[800], again[800];
	CHECK(ArchiveCache_dir(dir_dd, sizeof(dir_dd), cache, dd) == 0, "dir for Double Dragon");
	CHECK(ArchiveCache_dir(dir_dd2, sizeof(dir_dd2), cache, dd2) == 0, "dir for Double Dragon II");
	CHECK(ArchiveCache_dir(dir_hack, sizeof(dir_hack), cache, dd_hack) == 0, "dir for the hack");
	CHECK(ArchiveCache_dir(again, sizeof(again), cache, dd) == 0 && !strcmp(again, dir_dd), "same archive, same dir");
	CHECK(strcmp(dir_dd, dir_dd2) != 0, "sequel gets its own dir");
	CHECK(strcmp(dir_dd, dir_hack) != 0, "same file name in another folder gets its own dir");
	CHECK(!strncmp(dir_dd, cache, strlen(cache)) && dir_dd[strlen(cache)] == '/', "dir sits under the root");

	set_mtime(dd, 2000);
	CHECK(ArchiveCache_dir(again, sizeof(again), cache, dd) == 0 && strcmp(again, dir_dd) != 0, "replaced archive gets a new dir");
	set_mtime(dd, 1000);

	char missing[700];
	snprintf(missing, sizeof(missing), "%s/nope.zip", roms);
	CHECK(ArchiveCache_dir(again, sizeof(again), cache, missing) == -1, "missing archive fails");
	char tiny[8];
	CHECK(ArchiveCache_dir(tiny, sizeof(tiny), cache, dd) == -1, "too small buffer fails");

	// Issue #222: only Double Dragon II was extracted so far; Double Dragon
	// must not find it.
	char* exts[] = {"fds", "nes", "unf", NULL};
	char found[900], path[900];
	mkdir(dir_dd2, 0777);
	snprintf(path, sizeof(path), "%s/Double Dragon II - The Revenge (USA).nes", dir_dd2);
	write_file(path, "rom two");
	mkdir(dir_dd, 0777);
	CHECK(!ArchiveCache_find(dir_dd, exts, found, sizeof(found)) && found[0] == '\0', "Double Dragon does not see the sequel");
	CHECK(ArchiveCache_find(dir_dd2, exts, found, sizeof(found)) && !strcmp(found, path), "Double Dragon II finds its rom");

	// leftovers that must never count as the rom
	char other[900];
	snprintf(other, sizeof(other), "%s/readme.txt", dir_dd);
	write_file(other, "hi");
	snprintf(other, sizeof(other), "%s/Empty.nes", dir_dd);
	write_file(other, "");
	snprintf(other, sizeof(other), "%s/.hidden.nes", dir_dd);
	write_file(other, "x");
	snprintf(other, sizeof(other), "%s/7zAbCdEf", dir_dd);
	mkdir(other, 0777);
	snprintf(other, sizeof(other), "%s/dir.nes", dir_dd);
	mkdir(other, 0777);
	snprintf(other, sizeof(other), "%s/nes", dir_dd);
	write_file(other, "x");
	CHECK(!ArchiveCache_find(dir_dd, exts, found, sizeof(found)), "ignores non-roms, empty, hidden and dirs");

	snprintf(path, sizeof(path), "%s/Double Dragon (USA).nes", dir_dd);
	write_file(path, "rom one");
	CHECK(ArchiveCache_find(dir_dd, exts, found, sizeof(found)) && !strcmp(found, path), "Double Dragon finds its own rom");

	snprintf(other, sizeof(other), "%s/nowhere", root);
	CHECK(!ArchiveCache_find(other, exts, found, sizeof(found)), "missing dir finds nothing");

	char cmd[700];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	system(cmd);

	printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
	return fails ? 1 : 0;
}
