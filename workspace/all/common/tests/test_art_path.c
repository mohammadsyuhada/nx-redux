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

	// 5. a folder game's .m3u (as Home's recents and pins store it): art beside the folder
	char ps[512], game[512], m3u[512], disc[512], ps_media[512];
	snprintf(ps, sizeof(ps), "%s/Roms/Sony PlayStation (PS)", root);
	snprintf(ps_media, sizeof(ps_media), "%s/.media", ps);
	snprintf(game, sizeof(game), "%s/Final Fantasy VII", ps);
	snprintf(mk, sizeof(mk), "mkdir -p '%s' '%s/screenshot' '%s/boxart'", game, ps_media, ps_media);
	assert(system(mk) == 0);
	snprintf(m3u, sizeof(m3u), "%s/Final Fantasy VII.m3u", game);
	snprintf(disc, sizeof(disc), "%s/Final Fantasy VII (Disc 2).chd", game);
	mkfile(m3u);
	mkfile(disc);

	// nothing yet: a miss, out still the disc's own screenshot path
	assert(!ROM_findScreenshot(m3u, out, sizeof(out)));
	snprintf(want, sizeof(want), "%s/.media/screenshot/Final Fantasy VII.png", game);
	assert(strcmp(out, want) == 0);
	ROM_displayArtPath(m3u, 2, false, out, sizeof(out));
	snprintf(want, sizeof(want), "%s/.media/boxart/Final Fantasy VII.png", game);
	assert(strcmp(out, want) == 0);

	// the root picture beside the folder
	snprintf(want, sizeof(want), "%s/Final Fantasy VII.png", ps_media);
	mkfile(want);
	assert(ROM_findScreenshot(m3u, out, sizeof(out)));
	assert(strcmp(out, want) == 0);

	// the scraped screenshot beside the folder wins, for the .m3u and any disc in the folder
	snprintf(want, sizeof(want), "%s/screenshot/Final Fantasy VII.png", ps_media);
	mkfile(want);
	assert(ROM_findScreenshot(m3u, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	assert(ROM_findScreenshot(disc, out, sizeof(out)));
	assert(strcmp(out, want) == 0);

	// box art beside the folder (the Backdrop's box)
	snprintf(want, sizeof(want), "%s/boxart/Final Fantasy VII.png", ps_media);
	mkfile(want);
	ROM_displayArtPath(m3u, 2, false, out, sizeof(out));
	assert(strcmp(out, want) == 0);
	ROM_displayArtPath(game, 2, false, out, sizeof(out)); // the game list's folder entry: unchanged
	assert(strcmp(out, want) == 0);

	// 6. a plain subfolder (no folder-named .m3u/.cue) never borrows the folder's art
	char sub[512], subrom[512];
	snprintf(sub, sizeof(sub), "%s/RPG", ps);
	snprintf(mk, sizeof(mk), "mkdir -p '%s'", sub);
	assert(system(mk) == 0);
	snprintf(subrom, sizeof(subrom), "%s/Xenogears.chd", sub);
	mkfile(subrom);
	snprintf(want, sizeof(want), "%s/screenshot/RPG.png", ps_media);
	mkfile(want);
	assert(!ROM_findScreenshot(subrom, out, sizeof(out)));
	snprintf(want, sizeof(want), "%s/.media/screenshot/Xenogears.png", sub);
	assert(strcmp(out, want) == 0);

	// 7. a .cue folder game counts too
	char cuegame[512], cue[512];
	snprintf(cuegame, sizeof(cuegame), "%s/Vagrant Story", ps);
	snprintf(mk, sizeof(mk), "mkdir -p '%s'", cuegame);
	assert(system(mk) == 0);
	snprintf(cue, sizeof(cue), "%s/Vagrant Story.cue", cuegame);
	mkfile(cue);
	snprintf(want, sizeof(want), "%s/boxart/Vagrant Story.png", ps_media);
	mkfile(want);
	ROM_displayArtPath(cue, 2, false, out, sizeof(out));
	assert(strcmp(out, want) == 0);

	// 8. the List's art (ROM_findListArt; art = GAME_LIST_ART_*: 0 Screenshot, 1 Mix, 2 2D box art, 3 Wheel,
	// 4 3D box art). A fitted
	// picture returns true; otherwise ROM_findScreenshot's pick, returning false.
	char gb[512], gb_media[512], gbrom[512], shot[512];
	snprintf(gb, sizeof(gb), "%s/Roms/Game Boy (GB)", root);
	snprintf(gb_media, sizeof(gb_media), "%s/.media", gb);
	snprintf(mk, sizeof(mk), "mkdir -p '%s/screenshot' '%s/mix' '%s/boxart2d' '%s/wheel' '%s/boxart'", gb_media,
			 gb_media, gb_media, gb_media, gb_media);
	assert(system(mk) == 0);
	snprintf(gbrom, sizeof(gbrom), "%s/Tetris.gb", gb);
	mkfile(gbrom);
	// only a screenshot: every type falls back to it
	snprintf(shot, sizeof(shot), "%s/screenshot/Tetris.png", gb_media);
	mkfile(shot);
	for (int art = 0; art <= 4; art++) {
		assert(!ROM_findListArt(gbrom, art, out, sizeof(out)));
		assert(strcmp(out, shot) == 0);
	}
	// Mix: the legacy root picture (an old library's mix, a Port's picture) when there is no .media/mix one
	snprintf(want, sizeof(want), "%s/Tetris.png", gb_media);
	mkfile(want);
	assert(ROM_findListArt(gbrom, 1, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	// Mix: .media/mix/ wins over the legacy root picture
	snprintf(want, sizeof(want), "%s/mix/Tetris.png", gb_media);
	mkfile(want);
	assert(ROM_findListArt(gbrom, 1, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	// 2D box art, Wheel and 3D box art: their own folders only (the mix and the root picture never stand in)
	assert(!ROM_findListArt(gbrom, 2, out, sizeof(out)));
	assert(strcmp(out, shot) == 0);
	assert(!ROM_findListArt(gbrom, 3, out, sizeof(out)));
	assert(strcmp(out, shot) == 0);
	assert(!ROM_findListArt(gbrom, 4, out, sizeof(out)));
	assert(strcmp(out, shot) == 0);
	snprintf(want, sizeof(want), "%s/boxart2d/Tetris.png", gb_media);
	mkfile(want);
	assert(ROM_findListArt(gbrom, 2, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	snprintf(want, sizeof(want), "%s/wheel/Tetris.png", gb_media);
	mkfile(want);
	assert(ROM_findListArt(gbrom, 3, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	snprintf(want, sizeof(want), "%s/boxart/Tetris.png", gb_media);
	mkfile(want);
	assert(ROM_findListArt(gbrom, 4, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	// Screenshot (and an out-of-range type): the screenshot even though every fitted picture exists
	assert(!ROM_findListArt(gbrom, 0, out, sizeof(out)));
	assert(strcmp(out, shot) == 0);
	assert(!ROM_findListArt(gbrom, 7, out, sizeof(out)));
	assert(strcmp(out, shot) == 0);

	// a folder game's disc: each picture beside its folder. The legacy root mix is FF VII's root picture (made in 5.)
	snprintf(want, sizeof(want), "%s/Final Fantasy VII.png", ps_media);
	assert(ROM_findListArt(disc, 1, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	assert(!ROM_findListArt(disc, 2, out, sizeof(out))); // no 2D box art yet: its screenshot instead
	snprintf(want, sizeof(want), "%s/screenshot/Final Fantasy VII.png", ps_media);
	assert(strcmp(out, want) == 0);
	assert(!ROM_findListArt(disc, 0, out, sizeof(out)));
	assert(strcmp(out, want) == 0);
	snprintf(mk, sizeof(mk), "mkdir -p '%s/mix' '%s/boxart2d' '%s/wheel'", ps_media, ps_media, ps_media);
	assert(system(mk) == 0);
	// (3D box art: FF VII's box art beside the folder, made in 5.)
	const char* folders[] = {NULL, "mix", "boxart2d", "wheel", "boxart"};
	for (int art = 1; art <= 4; art++) {
		snprintf(want, sizeof(want), "%s/%s/Final Fantasy VII.png", ps_media, folders[art]);
		mkfile(want);
		assert(ROM_findListArt(disc, art, out, sizeof(out)));
		assert(strcmp(out, want) == 0);
		assert(ROM_findListArt(m3u, art, out, sizeof(out)));
		assert(strcmp(out, want) == 0);
	}

	snprintf(mk, sizeof(mk), "rm -rf '%s'", root);
	assert(system(mk) == 0);
	printf("test_art_path: ok\n");
	return 0;
}
