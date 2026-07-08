#include "gameswitcher.h"
#include "config.h"
#include "defines.h"
#include "imgloader.h"
#include "launcher.h"
#include "recents.h"
#include "ui_buttonhintbar.h"
#include "ui_emptystate.h"
#include "ui_image.h"
#include "ui_message.h"
#include "utils.h"

#include <unistd.h>

static int switcher_selected = 0;

void GameSwitcher_init(void) {
	switcher_selected = 0;
}

int GameSwitcher_shouldStartInSwitcher(void) {
	if (exists(GAME_SWITCHER_PERSIST_PATH)) {
		// consider this "consumed", dont bring up the switcher next time we
		// regularly exit a game
		unlink(GAME_SWITCHER_PERSIST_PATH);
		return 1;
	}
	return 0;
}

void GameSwitcher_resetSelection(void) {
	switcher_selected = 0;
}

int GameSwitcher_getSelected(void) {
	return switcher_selected;
}

const char* GameSwitcher_getSelectedName(void) {
	static char name_buf[256];
	if (Recents_count() <= 0)
		return "Recents";
	Recent* recent = Recents_at(switcher_selected);
	if (!recent)
		return "Recents";
	if (recent->alias) {
		strncpy(name_buf, recent->alias, sizeof(name_buf) - 1);
		name_buf[sizeof(name_buf) - 1] = '\0';
		return name_buf;
	}
	char full_path[MAX_PATH];
	snprintf(full_path, sizeof(full_path), "%s%s", SDCARD_PATH, recent->path);
	getDisplayName(full_path, name_buf);
	return name_buf;
}

GameSwitcherResult GameSwitcher_handleInput(unsigned long now) {
	GameSwitcherResult result = {0};
	result.screen = SCREEN_GAMESWITCHER;
	result.gsanimdir = ANIM_NONE;

	if (PAD_justPressed(BTN_B) || PAD_tappedSelect(now)) {
		result.screen = SCREEN_GAMELIST;
		switcher_selected = 0;
		result.dirty = true;
		result.folderbgchanged = true;
	} else if (Recents_count() > 0 && PAD_justReleased(BTN_A)) {
		// this will drop us back into game switcher after leaving the game
		putFile(GAME_SWITCHER_PERSIST_PATH, "unused");
		result.startgame = true;
		Entry* selectedEntry =
			Recents_entryFromRecent(Recents_at(switcher_selected));
		resume.should_resume = resume.can_resume;
		Entry_open(selectedEntry);
		result.dirty = true;
		Entry_free(selectedEntry);
	} else if (Recents_count() > 0 && PAD_justReleased(BTN_Y)) {
		Recents_removeAt(switcher_selected);
		if (switcher_selected >= Recents_count())
			switcher_selected = Recents_count() - 1;
		if (switcher_selected < 0)
			switcher_selected = 0;
		result.dirty = true;
	} else if (PAD_justPressed(BTN_RIGHT)) {
		switcher_selected++;
		if (switcher_selected == Recents_count())
			switcher_selected = 0; // wrap
		result.dirty = true;
		result.gsanimdir = SLIDE_LEFT;
	} else if (PAD_justPressed(BTN_LEFT)) {
		switcher_selected--;
		if (switcher_selected < 0)
			switcher_selected = Recents_count() - 1; // wrap
		result.dirty = true;
		result.gsanimdir = SLIDE_RIGHT;
	}

	return result;
}

static void drawBackground(SDL_Surface* surface, int x, int y, int w, int h,
						   SDL_Surface* blackBG) {
	GFX_flipHidden();
	GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0,
					LAYER_BACKGROUND);
	GFX_drawOnLayer(surface, x, y, w, h, 1.0f, 0, LAYER_BACKGROUND);
}

static void drawCarouselAnimation(SDL_Surface* surface, int x, int y, int w,
								  int h, int gsanimdir,
								  SDL_Surface* blackBG) {
	GFX_flipHidden();
	GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0,
					LAYER_BACKGROUND);
	if (gsanimdir == SLIDE_LEFT)
		GFX_animateSurface(surface, x + screen->w, y, x, y, w, h,
						   CFG_getMenuTransitions() ? 80 : 20, 0, 255,
						   LAYER_ALL);
	else if (gsanimdir == SLIDE_RIGHT)
		GFX_animateSurface(surface, x - screen->w, y, x, y, w, h,
						   CFG_getMenuTransitions() ? 80 : 20, 0, 255,
						   LAYER_ALL);
	GFX_drawOnLayer(surface, x, y, w, h, 1.0f, 0, LAYER_BACKGROUND);
}

void GameSwitcher_render(int lastScreen, SDL_Surface* blackBG,
						 int gsanimdir) {
	GFX_clearLayers(LAYER_ALL);

	if (Recents_count() <= 0) {
		SDL_FillRect(screen, &(SDL_Rect){0, 0, screen->w, screen->h}, 0);
		UI_renderEmptyState(screen, "No Recents", "Play a game to see it here", NULL);
		GFX_flipHidden();
		return;
	}

	Entry* selectedEntry =
		Recents_entryFromRecent(Recents_at(switcher_selected));
	readyResume(selectedEntry);

	if (resume.can_resume)
		UI_renderButtonHintBar(screen, (char*[]){"Y", "REMOVE", "A", "RESUME", "B", "BACK", NULL});
	else
		UI_renderButtonHintBar(screen, (char*[]){"Y", "REMOVE", "A", "RESUME", NULL});

	if (resume.has_preview) {
		SDL_Surface* bmp = IMG_Load(resume.preview_path);
		if (bmp)
			bmp = UI_convertSurface(bmp, screen);
		if (bmp) {
			int aw = screen->w;
			int ah = screen->h;

			float aspectRatio = (float)bmp->w / (float)bmp->h;
			float screenRatio = (float)screen->w / (float)screen->h;

			if (screenRatio > aspectRatio) {
				aw = (int)(screen->h * aspectRatio);
				ah = screen->h;
			} else {
				aw = screen->w;
				ah = (int)(screen->w / aspectRatio);
			}
			int ax = (screen->w - aw) / 2;
			int ay = (screen->h - ah) / 2;

			if (lastScreen == SCREEN_GAME) {
				GFX_flipHidden();
				GFX_animateSurfaceOpacity(
					bmp, 0, 0, screen->w, screen->h, 0, 255,
					CFG_getMenuTransitions() ? 150 : 20, LAYER_ALL);
			} else if (lastScreen == SCREEN_GAMESWITCHER) {
				drawCarouselAnimation(bmp, ax, ay, aw, ah, gsanimdir, blackBG);
			} else {
				drawBackground(bmp, ax, ay, aw, ah, blackBG);
			}
			SDL_FreeSurface(bmp);
		}
	} else if (resume.has_boxart) {
		SDL_Surface* boxart = IMG_Load(resume.boxart_path);
		if (boxart)
			boxart = UI_convertSurface(boxart, screen);
		if (boxart) {
			int img_w = boxart->w;
			int img_h = boxart->h;
			int max_w = (int)(screen->w * CFG_getGameArtWidth());
			int max_h = (int)(screen->h * 0.6);
			int new_w, new_h;
			UI_calcImageFit(img_w, img_h, max_w, max_h, &new_w, &new_h);

			GFX_ApplyRoundedCorners_8888(
				boxart, &(SDL_Rect){0, 0, boxart->w, boxart->h},
				SCALE1((float)CFG_getThumbnailRadius() *
					   ((float)img_w / (float)new_w)));

			int ax = (screen->w - new_w) / 2;
			int ay = (screen->h - new_h) / 2;

			if (lastScreen == SCREEN_GAME) {
				GFX_flipHidden();
				GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0,
								LAYER_BACKGROUND);
				GFX_animateSurfaceOpacity(boxart, ax, ay, new_w, new_h, 0, 255,
										  CFG_getMenuTransitions() ? 150 : 20,
										  LAYER_ALL);
			} else if (lastScreen == SCREEN_GAMESWITCHER) {
				drawCarouselAnimation(boxart, ax, ay, new_w, new_h, gsanimdir,
									  blackBG);
			} else {
				drawBackground(boxart, ax, ay, new_w, new_h, blackBG);
			}
			SDL_FreeSurface(boxart);
		}
	} else {
		// No savestate preview and no boxart - show "No Preview"
		if (lastScreen == SCREEN_GAME) {
			SDL_Surface* tmpsur = SDL_CreateRGBSurfaceWithFormat(
				0, screen->w, screen->h, screen->format->BitsPerPixel,
				screen->format->format);
			if (tmpsur) {
				SDL_FillRect(tmpsur, &(SDL_Rect){0, 0, screen->w, screen->h},
							 SDL_MapRGBA(screen->format, 0, 0, 0, 255));
				GFX_animateSurfaceOpacity(
					tmpsur, 0, 0, screen->w, screen->h, 255, 0,
					CFG_getMenuTransitions() ? 150 : 20, LAYER_BACKGROUND);
				SDL_FreeSurface(tmpsur);
			}
		} else if (lastScreen == SCREEN_GAMESWITCHER) {
			GFX_flipHidden();
		}
		UI_renderCenteredMessage(screen, "No Preview");
	}
	Entry_free(selectedEntry);

	GFX_flipHidden();
}
