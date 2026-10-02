// Host test for the screenshot resolver (utils.c): the scraped .media/screenshot/<name>.png first, then the root
// .media/<name>.png (PortMaster's convention, hand-made art, older scrapes), else a miss that still names the
// screenshot path.
#include "../utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkfile(const char* path) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs("png", f);
	fclose(f);
}

int main(void) {
	char root[256];
	snprintf(root, sizeof(root), "/tmp/nx_test_art_path_%d", (int)getpid());
	char dir[512], media[512], shot_dir[512], rom[512], out[512], want[512];
	snprintf(dir, sizeof(dir), "%s/Roms/Ports (PORTS)", root);
	snprintf(media, sizeof(media), "%s/.media", dir);
	snprintf(shot_dir, sizeof(shot_dir), "%s/screenshot", media);
	char mk[1024];
	snprintf(mk, sizeof(mk), "mkdir -p '%s'", shot_dir);
	assert(system(mk) == 0);
	snprintf(rom, sizeof(rom), "%s/StardewValley.sh", dir);
	mkfile(rom);

	// 1. nothing in .media: a miss, out = the screenshot variant path (stable for cache keys)
	assert(!ROM_findScreenshot(rom, out, sizeof(out)));
	snprintf(want, sizeof(want), "%s/StardewValley.png", shot_dir);
	assert(strcmp(out, want) == 0);

	// 2. only the root .media/<name>.png (a Port): found there
	snprintf(want, sizeof(want), "%s/StardewValley.png", media);
	mkfile(want);
	assert(ROM_findScreenshot(rom, out, sizeof(out)));
	assert(strcmp(out, want) == 0);

	// 3. a scraped screenshot wins over the root picture
	snprintf(want, sizeof(want), "%s/StardewValley.png", shot_dir);
	mkfile(want);
	assert(ROM_findScreenshot(rom, out, sizeof(out)));
	assert(strcmp(out, want) == 0);

	// 4. the extension is dropped whatever it is, and a name with dots keeps all but the last
	char rom2[512];
	snprintf(rom2, sizeof(rom2), "%s/Dr. Mario (USA).gb", dir);
	snprintf(want, sizeof(want), "%s/Dr. Mario (USA).png", media);
	mkfile(want);
	assert(ROM_findScreenshot(rom2, out, sizeof(out)));
	assert(strcmp(out, want) == 0);

	snprintf(mk, sizeof(mk), "rm -rf '%s'", root);
	assert(system(mk) == 0);
	printf("test_art_path: ok\n");
	return 0;
}
