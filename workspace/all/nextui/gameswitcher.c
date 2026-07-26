#include "gameswitcher.h"
#include "api.h"
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

// Filtered view of the recents list: gs_indices[i] holds the recents index of
// the i-th switcher entry. Identity mapping when the resumable-only setting is
// off, so behavior matches the unfiltered switcher exactly.
static int gs_indices[MAX_RECENTS];
static int gs_count = 0;

// Clobbers the shared `resume` global while probing each recent; safe because
// GameSwitcher_render re-runs readyResume for the selected entry on every
// dirty frame, and the A-press handler reads `resume` only after a render
// has refreshed it for the current selection.
static void gs_rebuildIndices(void) {
	gs_count = 0;
	bool resumable_only = CFG_getGameSwitcherResumableOnly();
	for (int i = 0; i < Recents_count() && gs_count < MAX_RECENTS; i++) {
		if (resumable_only) {
			Entry* entry = Recents_entryFromRecent(Recents_at(i));
			if (!entry)
				continue; // emulator no longer available
			readyResume(entry);
			Entry_free(entry);
			if (!resume.can_resume)
				continue;
		}
		gs_indices[gs_count++] = i;
	}
	if (gs_count == 0)
		readyResume(NULL); // leave a known-false resume state, not the last probe's
}

// Single-entry cache of the decoded+converted preview/boxart for the selected
// recent. GameSwitcher_render runs on every dirty frame (battery/status ticks,
// carousel steps), and re-decoding the full-size PNG on the UI thread each time
// stutters the carousel — key by source path so we only decode on change.
static char gs_img_path[MAX_PATH] = {0};
static SDL_Surface* gs_img_surf = NULL;

static SDL_Surface* gs_get_cached_image(const char* path) {
	if (gs_img_surf && strcmp(gs_img_path, path) == 0)
		return gs_img_surf;
	if (gs_img_surf) {
		SDL_FreeSurface(gs_img_surf);
		gs_img_surf = NULL;
	}
	SDL_Surface* raw = IMG_Load(path);
	if (raw)
		raw = UI_convertSurface(raw, screen);
	gs_img_surf = raw;
	strncpy(gs_img_path, path, sizeof(gs_img_path) - 1);
	gs_img_path[sizeof(gs_img_path) - 1] = '\0';
	return gs_img_surf;
}

void GameSwitcher_init(void) {
	switcher_selected = 0;
	gs_rebuildIndices();
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
	gs_rebuildIndices();
}

int GameSwitcher_getSelected(void) {
	return switcher_selected;
}

const char* GameSwitcher_getSelectedName(void) {
	static char name_buf[MAX_PATH]; // getDisplayName requires a MAX_PATH out buffer
	if (gs_count <= 0)
		return "Recents";
	Recent* recent = Recents_at(gs_indices[switcher_selected]);
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
	} else if (gs_count > 0 && PAD_justReleased(BTN_A)) {
		Entry* selectedEntry =
			Recents_entryFromRecent(Recents_at(gs_indices[switcher_selected]));
		// NULL when the recent's emulator is no longer available
		if (selectedEntry) {
			// this will drop us back into game switcher after leaving the game
			putFile(GAME_SWITCHER_PERSIST_PATH, "unused");
			result.startgame = true;
			resume.should_resume = resume.can_resume;
			Entry_open(selectedEntry);
			result.dirty = true;
			Entry_free(selectedEntry);
		}
	} else if (gs_count > 0 && PAD_justReleased(BTN_Y)) {
		Recents_removeAt(gs_indices[switcher_selected]);
		gs_rebuildIndices();
		if (switcher_selected >= gs_count)
			switcher_selected = gs_count - 1;
		if (switcher_selected < 0)
			switcher_selected = 0;
		result.dirty = true;
	} else if (gs_count > 0 && PAD_justPressed(BTN_RIGHT)) {
		switcher_selected++;
		if (switcher_selected >= gs_count)
			switcher_selected = 0; // wrap
		result.dirty = true;
		result.gsanimdir = SLIDE_LEFT;
	} else if (gs_count > 0 && PAD_justPressed(BTN_LEFT)) {
		switcher_selected--;
		if (switcher_selected < 0)
			switcher_selected = gs_count - 1; // wrap
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

	if (gs_count <= 0) {
		SDL_FillRect(screen, &(SDL_Rect){0, 0, screen->w, screen->h}, 0);
		if (Recents_count() > 0)
			UI_renderEmptyState(screen, "No Resumable Games", "Suspend a game to see it here", NULL);
		else
			UI_renderEmptyState(screen, "No Recents", "Play a game to see it here", NULL);
		GFX_flipHidden();
		return;
	}

	Entry* selectedEntry =
		Recents_entryFromRecent(Recents_at(gs_indices[switcher_selected]));
	readyResume(selectedEntry);

	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "Y", "REMOVE", "A", resume.can_resume ? "RESUME" : "START", NULL});

	if (resume.has_preview) {
		SDL_Surface* bmp = gs_get_cached_image(resume.preview_path);
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
		}
	} else if (resume.has_boxart) {
		SDL_Surface* boxart = gs_get_cached_image(resume.boxart_path);
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
