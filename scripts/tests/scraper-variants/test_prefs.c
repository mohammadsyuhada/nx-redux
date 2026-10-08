// Host test for the scraper download preferences
// (workspace/all/scraper/scraper_prefs.c): the key=0|1 file the Artwork
// Manager keeps under creds_dir()/download.txt. Fixture files are made with
// mkstemp in the directory the test script passes in.
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scraper_core.h" // creds_dir
#include "scraper_prefs.h"

// api.c (the logger) is not linked into this host test.
void LOG_note(int level, const char* fmt, ...) {
	(void)level;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static int failures = 0;
#define CHECK(cond, msg)                \
	do {                                \
		if (cond)                       \
			printf("ok   - %s\n", msg); \
		else {                          \
			printf("FAIL - %s\n", msg); \
			failures++;                 \
		}                               \
	} while (0)

// Make a scratch file under `dir` holding `contents`; its path goes in `out`.
static bool make_file(const char* dir, const char* contents, char* out, size_t out_size) {
	snprintf(out, out_size, "%s/prefsXXXXXX", dir);
	int fd = mkstemp(out);
	if (fd < 0)
		return false;
	size_t len = strlen(contents);
	bool ok = write(fd, contents, len) == (ssize_t)len;
	close(fd);
	return ok;
}

// Pre-fill with true so a load that leaves fields untouched is caught.
static void load(const char* path, ScraperPrefs* p) {
	p->box2d = p->wheel = p->mix = true;
	ScraperPrefs_loadFrom(path, p);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <tmpdir>\n", argv[0]);
		return 2;
	}
	const char* dir = argv[1];
	char path[512];
	ScraperPrefs p;

	// --- 1. Missing file: all off ---
	snprintf(path, sizeof(path), "%s/does-not-exist.txt", dir);
	load(path, &p);
	CHECK(!p.box2d && !p.wheel && !p.mix, "missing file gives all false");
	CHECK(!ScraperPrefs_wantWheel(&p), "missing file: wantWheel false");

	// --- 2. mix alone implies the wheel download ---
	CHECK(make_file(dir, "mix=1\n", path, sizeof(path)), "fixture mix=1");
	load(path, &p);
	CHECK(p.mix && !p.box2d && !p.wheel, "mix=1 alone gives mix only");
	CHECK(ScraperPrefs_wantWheel(&p), "mix=1: wantWheel true");
	unlink(path);

	// --- 3. Unknown keys ignored, 0 is false ---
	CHECK(make_file(dir, "box2d=1\nfoo=1\nwheel=0\n", path, sizeof(path)), "fixture box2d/foo/wheel");
	load(path, &p);
	CHECK(p.box2d && !p.wheel && !p.mix, "box2d=1 foo=1 wheel=0 gives box2d only");
	CHECK(!ScraperPrefs_wantWheel(&p), "box2d only: wantWheel false");
	unlink(path);

	// --- 4. Values other than 1 are false; garbage lines are skipped ---
	CHECK(make_file(dir, "box2d=2\nwheel=yes\ngarbage\nmix=1\n", path, sizeof(path)), "fixture odd values");
	load(path, &p);
	CHECK(!p.box2d && !p.wheel && p.mix, "values other than 1 are false");
	unlink(path);

	// --- 5. CRLF line endings ---
	CHECK(make_file(dir, "box2d=1\r\nwheel=1\r\nmix=0\r\n", path, sizeof(path)), "fixture CRLF");
	load(path, &p);
	CHECK(p.box2d && p.wheel && !p.mix, "CRLF line endings are tolerated");
	unlink(path);

	// --- 6. Save/load round trip ---
	snprintf(path, sizeof(path), "%s/roundtrip.txt", dir);
	ScraperPrefs out = {true, false, true};
	CHECK(ScraperPrefs_saveTo(path, &out), "saveTo succeeds");
	FILE* f = fopen(path, "r");
	char buf[128] = {0};
	if (f) {
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = '\0';
		fclose(f);
	}
	CHECK(strcmp(buf, "box2d=1\nwheel=0\nmix=1\n") == 0, "saveTo writes key=0|1 lines");
	load(path, &p);
	CHECK(p.box2d && !p.wheel && p.mix, "round trip of {1,0,1}");
	unlink(path);

	// --- 7. Default-path wrappers create creds_dir() ---
	ScraperPrefs def = {false, true, false};
	CHECK(ScraperPrefs_save(&def), "save to creds_dir()/download.txt succeeds");
	p.box2d = p.wheel = p.mix = true;
	ScraperPrefs_load(&p);
	CHECK(!p.box2d && p.wheel && !p.mix, "load reads creds_dir()/download.txt");
	snprintf(path, sizeof(path), "%s/download.txt", creds_dir());
	CHECK(access(path, F_OK) == 0, "download.txt lands in creds_dir()");

	// --- 8. Completeness: a screenshot or box art, plus the mix when on ---
	ScraperPrefs off = {false, false, false};
	CHECK(ScraperPrefs_isComplete(&off, true, false, false), "mix off: screenshot only is complete");
	CHECK(ScraperPrefs_isComplete(&off, false, true, false), "mix off: box art only is complete");
	CHECK(!ScraperPrefs_isComplete(&off, false, false, false), "mix off: neither is incomplete");
	CHECK(!ScraperPrefs_isComplete(&off, false, false, true), "mix off: mix alone is incomplete");
	ScraperPrefs on = {false, false, true};
	CHECK(!ScraperPrefs_isComplete(&on, true, true, false), "mix on: screenshot + box art without mix is incomplete");
	CHECK(ScraperPrefs_isComplete(&on, true, false, true), "mix on: screenshot + mix is complete");

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	return 0;
}
