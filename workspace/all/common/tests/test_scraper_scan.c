// Host test for scraper_scan.c: the Artwork Manager must see exactly what the
// nextui game list shows — any non-hidden file, folder games as one entry,
// nested folders recursed — regardless of extension.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../scraper/scraper_scan.h"

static char root[512];

static void touch(const char* rel) {
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", root, rel);
	FILE* f = fopen(p, "w");
	assert(f);
	fputs("x", f);
	fclose(f);
}

static void mkd(const char* rel) {
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", root, rel);
	assert(mkdir(p, 0755) == 0);
}

#define MAX_SEEN 32
static ScanGame seen[MAX_SEEN];
static int seen_count;
static int stop_after = -1;

static bool collect(const ScanGame* g, void* ud) {
	(void)ud;
	assert(seen_count < MAX_SEEN);
	seen[seen_count++] = *g;
	return stop_after < 0 || seen_count < stop_after;
}

static const ScanGame* find(const char* label) {
	for (int i = 0; i < seen_count; i++)
		if (strcmp(seen[i].label, label) == 0)
			return &seen[i];
	return NULL;
}

static void expect_path(const char* got, const char* rel) {
	char want[1024];
	snprintf(want, sizeof(want), "%s/%s", root, rel);
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "  got  %s\n  want %s\n", got, want);
		assert(0);
	}
}

int main(void) {
	char tmpl[] = "/tmp/nx_scan_XXXXXX";
	assert(mkdtemp(tmpl));
	snprintf(root, sizeof(root), "%s/Virtual Boy (VB)", tmpl);
	assert(mkdir(root, 0755) == 0);

	// flat games with extensions the old allowlist did not know
	touch("Wario Land.vb");
	touch("Teleroboxer.zip");
	touch("noext");
	// hidden / non-game files nextui hides too
	touch(".DS_Store");
	touch("map.txt");
	touch("Broken.vb.disabled");
	mkd(".media");
	touch(".media/Wario Land.png"); // existing art must not count as a game
	// nested collection folder
	mkd("Homebrew");
	touch("Homebrew/Demo.vb");
	// folder games (multi-disc): cue and m3u variants, files inside are not games
	mkd("Discs");
	mkd("Discs/Game A");
	touch("Discs/Game A/Game A.cue");
	touch("Discs/Game A/Game A (Track 1).bin");
	mkd("Discs/Game B");
	touch("Discs/Game B/Game B.m3u");
	touch("Discs/Game B/Game B (Disc 1).chd");
	touch("Discs/Game B/Game B (Disc 2).chd");
	// a hidden directory is skipped entirely
	mkd(".hidden");
	touch(".hidden/Ghost.vb");

	seen_count = 0;
	int n = Scan_walk(root, collect, NULL);
	assert(n == 6);
	assert(seen_count == 6);

	const ScanGame* g;
	g = find("Wario Land");
	assert(g && !g->folder_game);
	assert(strcmp(g->filename, "Wario Land.vb") == 0);
	expect_path(g->path, "Wario Land.vb");
	expect_path(g->art_png, ".media/Wario Land.png");

	g = find("Teleroboxer");
	assert(g);
	g = find("noext");
	assert(g);
	expect_path(g->art_png, ".media/noext.png");

	g = find("Homebrew/Demo");
	assert(g && !g->folder_game);
	expect_path(g->path, "Homebrew/Demo.vb");
	expect_path(g->art_png, "Homebrew/.media/Demo.png");

	g = find("Discs/Game A");
	assert(g && g->folder_game);
	assert(strcmp(g->filename, "Game A.cue") == 0);
	expect_path(g->path, "Discs/Game A/Game A.cue");
	expect_path(g->art_png, "Discs/.media/Game A.png"); // beside the folder, like ROM_findArt

	g = find("Discs/Game B");
	assert(g && g->folder_game);
	assert(strcmp(g->filename, "Game B.m3u") == 0);

	assert(!find("Broken.vb"));
	assert(!find("map"));
	assert(!find(".hidden/Ghost"));
	assert(!find("Discs/Game A/Game A (Track 1)"));

	// deterministic order: case-insensitive by name at each level
	assert(strcmp(seen[0].label, "Discs/Game A") == 0);
	assert(strcmp(seen[1].label, "Discs/Game B") == 0);
	assert(strcmp(seen[2].label, "Homebrew/Demo") == 0);

	// early stop is honoured
	seen_count = 0;
	stop_after = 2;
	n = Scan_walk(root, collect, NULL);
	assert(n == 2 && seen_count == 2);

	// helpers
	assert(Scan_hidden(".x") && Scan_hidden("a.disabled") && Scan_hidden("map.txt"));
	assert(!Scan_hidden("map.txt.bak") && !Scan_hidden("Game.vb"));
	char out[512];
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s/Discs/Game A", root);
	assert(Scan_folderGameFile(dir, out, sizeof(out)));
	snprintf(dir, sizeof(dir), "%s/Homebrew", root);
	assert(!Scan_folderGameFile(dir, out, sizeof(out)) && out[0] == '\0');

	char rm[1100];
	snprintf(rm, sizeof(rm), "rm -rf '%s'", tmpl);
	system(rm);
	printf("test_scraper_scan: OK\n");
	return 0;
}
