#include "settings_bootlogo.h"
#include "defines.h"
#include "api.h"
#include "ui_components.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdbool.h>
#include <unistd.h>
#include <msettings.h>

// ============================================
// Globals
// ============================================

static SDL_Surface** images;
static char** image_paths;
static int selected = 0;
static int count = 0;

// ============================================
// Image loading
// ============================================

static int loadImages(void) {
	char* device = getenv("DEVICE");
	char basepath[MAX_PATH];
	if (device && (exactMatch("brick", device) || exactMatch("smartpros", device))) {
		snprintf(basepath, sizeof(basepath), "%s/Settings.pak/bootlogo/brick/", TOOLS_PATH);
	} else {
		snprintf(basepath, sizeof(basepath), "%s/Settings.pak/bootlogo/smartpro/", TOOLS_PATH);
	}

	DIR* dir;
	struct dirent* ent;
	if ((dir = opendir(basepath)) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strstr(ent->d_name, ".bmp") != NULL) {
				char path[MAX_PATH];
				snprintf(path, sizeof(path), "%s%s", basepath, ent->d_name);
				SDL_Surface* bmp = IMG_Load(path);
				if (bmp) {
					count++;
					SDL_Surface** new_images = realloc(images, sizeof(SDL_Surface*) * count);
					char** new_paths = realloc(image_paths, sizeof(char*) * count);
					if (!new_images || !new_paths) {
						SDL_FreeSurface(bmp);
						count--;
						break;
					}
					images = new_images;
					image_paths = new_paths;
					images[count - 1] = bmp;
					image_paths[count - 1] = strdup(path);
				}
			}
		}
		closedir(dir);
	} else {
		LOG_error("could not open bootlogo directory");
		if (CFG_getHaptics()) {
			VIB_triplePulse(5, 150, 200);
		}
		return 0;
	}
	return count;
}

static void unloadImages(void) {
	for (int i = 0; i < count; i++) {
		SDL_FreeSurface(images[i]);
		free(image_paths[i]);
	}
	free(images);
	free(image_paths);
	images = NULL;
	image_paths = NULL;
	selected = 0;
	count = 0;
}

// ============================================
// Bootlogo selector main loop
// ============================================

void bootlogo_run(SDL_Surface* screen) {
	loadImages();

	bool quit = false;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!quit && !app_quit) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justRepeated(BTN_LEFT)) {
			selected -= 1;
			if (selected < 0)
				selected = count - 1;
			dirty = true;
		} else if (PAD_justRepeated(BTN_RIGHT)) {
			selected += 1;
			if (selected >= count)
				selected = 0;
			dirty = true;
		} else if (PAD_justPressed(BTN_A) && count > 0) {
			char* boot_path = "/mnt/boot/";
			char* logo_path = image_paths[selected];
			char cmd[512];
			snprintf(cmd, sizeof(cmd), "mkdir -p %s && mount -t vfat /dev/mmcblk0p1 %s && cp '%s' %s/bootlogo.bmp && sync && umount %s && reboot", boot_path, boot_path, logo_path, boot_path, boot_path);
			system(cmd);
		} else if (PAD_justPressed(BTN_B)) {
			quit = true;
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (dirty) {
			GFX_clear(screen);

			if (count > 0) {
				SDL_Surface* image = images[selected];
				SDL_Rect image_rect = {
					screen->w / 2 - image->w / 2,
					screen->h / 2 - image->h / 2,
					image->w,
					image->h};
				SDL_BlitSurface(image, NULL, screen, &image_rect);
			}

			UI_renderMenuBar(screen, "Bootlogo");
			UI_renderButtonHintBar(screen, (char*[]){"A", "SET", "B", "BACK", "L/R", "SCROLL", NULL});

			GFX_flip(screen);
			dirty = false;
		} else
			GFX_sync();
	}

	unloadImages();
}
