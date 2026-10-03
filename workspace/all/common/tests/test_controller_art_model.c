// The Consoles tab's controller art (docs/controller-art.md): which console gets which pad, its boxes and opacity,
// the List's baked placements, and that every pad in the table has its images in res/menu.
#include "../../nextui/controller_art_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int is(const char* folder, const char* id) {
	const char* got = Pad_idForFolder(folder);
	return id ? (got && strcmp(got, id) == 0) : got == NULL;
}

static int near(float a, float b) {
	return fabsf(a - b) < 1e-4f;
}

static int exists(const char* id, const char* suffix) {
	char path[256];
	snprintf(path, sizeof(path), "../../../../skeleton/SYSTEM/res/menu/menu_pad_%s%s.png", id, suffix);
	return access(path, R_OK) == 0;
}

int main(void) {
	// a console's own pad, through its logo id and every tag that names it
	assert(is("Sony PlayStation (PS)", "ps"));
	assert(is("Game Boy Advance (GBA)", "gba") && is("Game Boy Advance (MGBA)", "gba"));
	assert(is("Super Nintendo (SFC)", "sfc") && is("Super Nintendo ES (SUPA)", "sfc"));
	assert(is("Sega Genesis (MD)", "md") && is("Sega Genesis (GPGX)", "md"));
	assert(is("Sega Master System (GPGX)", "sms") && is("Sega Game Gear (GPGX)", "gg"));
	assert(is("Nintendo Entertainment System (FC)", "fc") && is("Dreamcast (DC)", "dc"));
	// reuse: Super Game Boy on the SNES pad, Sega CD and 32X on the Mega Drive's, Neo Geo Pocket on the Color's
	assert(is("Super Game Boy (SGB)", "sfc"));
	assert(is("Sega CD (SEGACD)", "md") && is("Sega CD (GPGX)", "md") && is("Sega 32X (32X)", "md"));
	assert(is("Neo Geo Pocket (NGP)", "ngpc") && is("Neo Geo Pocket Color (NGPC)", "ngpc"));
	// no hardware of their own, or no image: keep the logo
	assert(is("Pico-8 (P8)", NULL) && is("Doom (PRBOOM)", NULL));
	assert(is("Arcade (FBN)", "c64")); // the Competition Pro, an arcade stick
	assert(is("Ports (PORTS)", NULL) && is("Commodore PET (PET)", NULL) && is("Nintendo 3DS (3DS)", NULL));
	assert(is("Amstrad CPC (CPC)", NULL) && is("Ms-Dos (DOS)", NULL));
	// the Commodores: the VIC-20 itself; the C64, C128, Plus/4 and Amiga on the Competition Pro
	assert(is("Commodore VIC20 (VIC)", "vic20") && is("Commodore 64 (C64)", "c64") && is("Commodore 128 (C128)", "c64"));
	assert(is("Commodore Plus4 (PLUS4)", "c64") && is("Amiga (PUAE)", "c64"));
	assert(is("No Tag", NULL) && is(NULL, NULL));

	// the table: the doc's 25 consoles and the two Commodores, each with k in 0.5..2.0, an aspect, and all three images on disk
	const char* ids[] = {"ps", "psp", "nds", "n64", "gb", "gbc", "gba", "fc", "sfc", "md", "sms", "gg", "sg1000",
						 "fds", "ngpc", "pkm", "coleco", "pce", "vb", "lynx", "a2600", "a5200", "a7800", "wsc", "dc", "vic20", "c64"};
	for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		const PadTableRow* r = Pad_row(ids[i]);
		assert(r && r->k >= 0.5f && r->k <= 2.0f && r->aspect > 0.2f && r->aspect < 5.0f);
		assert(exists(ids[i], "") && exists(ids[i], "_list_1024x768") && exists(ids[i], "_list_1280x720"));
	}
	assert(near(Pad_row("ps")->k, 1.00f) && near(Pad_row("gba")->k, 1.15f) && near(Pad_row("md")->k, 1.80f));
	assert(Pad_row("fbn") == NULL && Pad_row(NULL) == NULL);

	// boxes and fit: a wide pad fills the width, a tall one the height
	PadSize b = Pad_rowBox(100);
	assert(near(b.w, 280) && near(b.h, 180));
	b = Pad_stackBox(100);
	assert(near(b.w, 260) && near(b.h, 170));
	PadSize f = Pad_fit(2.0f, 280, 180);
	assert(near(f.w, 280) && near(f.h, 140));
	f = Pad_fit(1.0f, 280, 180);
	assert(near(f.w, 180) && near(f.h, 180));
	f = Pad_fit(0, 280, 180);
	assert(f.w == 0 && f.h == 0);

	// opacity: 0.6 focused, half at half a step, gone from one step out (either side)
	assert(near(Pad_alpha(0), 0.6f) && near(Pad_alpha(0.5f), 0.3f) && near(Pad_alpha(-0.5f), 0.3f));
	assert(Pad_alpha(1) == 0 && Pad_alpha(-2.5f) == 0);

	// the List: a baked image for the two screens, none for others
	const PadTableRow* ps = Pad_row("ps");
	const char* sfx = NULL;
	int x = -1, y = -1;
	assert(Pad_listPos(ps, 1024, 768, &sfx, &x, &y) && strcmp(sfx, "1024x768") == 0 && x == ps->x1024 && y == ps->y1024);
	assert(Pad_listPos(ps, 1280, 720, &sfx, &x, &y) && strcmp(sfx, "1280x720") == 0 && x == ps->x1280 && y == ps->y1280);
	assert(x > 1280 / 4 && x < 1280 && y >= 0 && y < 720);
	assert(!Pad_listPos(ps, 1920, 1080, &sfx, &x, &y) && !Pad_listPos(NULL, 1024, 768, &sfx, &x, &y));

	printf("test_controller_art_model: all passed\n");
	return 0;
}
