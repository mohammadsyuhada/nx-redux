#include "gameswitcher.h"
#include "api.h"
#include "config.h"
#include "defines.h"
#include "imgloader.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "home.h"
#include "infoband.h"
#include "launcher.h"
#include "recents.h"
#include "ui_buttonhintbar.h"
#include "ui_emptystate.h"
#include "ui_image.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "ui_fade.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>

#define GS_SCRIM_ALPHA 230 // the switcher's hint bar: 90%

// A resumable game's box art (no resume preview): fitted in this share of the screen width and 60% of its height
// (the retired "Game art width" setting's default).
#define GS_BOXART_WIDTH 0.45f

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
// rounded corners mutate the cached surface, so apply them once per cache fill
// (the render runs every dirty frame — battery ticks included)

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
		Home_reset(); // Continue may have been the removed game
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

// The console a recent belongs to: its Roms/<Console> folder (else its parent folder), shown as the list
// title shows it (getDisplayName drops the "(TAG)", trimSortingMeta the "001) " prefix).
static void consoleName(const char* rom_path, char* out, size_t size) {
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", rom_path);
	size_t roms_len = strlen(ROMS_PATH);
	char* cut = NULL;
	if (strncmp(dir, ROMS_PATH, roms_len) == 0 && dir[roms_len] == '/')
		cut = strchr(dir + roms_len + 1, '/');
	if (!cut)
		cut = strrchr(dir, '/');
	if (cut)
		*cut = '\0';
	char display[MAX_PATH];
	getDisplayName(dir, display);
	char* name = display;
	trimSortingMeta(&name);
	snprintf(out, size, "%s", name);
}

// "<console>   <i> / <n>" under the title, in font.tiny grey.
static void drawSubtitle(const char* rom_path) {
	if (!font.tiny)
		return;
	char console[MAX_PATH];
	consoleName(rom_path, console, sizeof(console));
	char line[MAX_PATH + 32];
	snprintf(line, sizeof(line), "%s   %d / %d", console, switcher_selected + 1, gs_count);
	int x = UI_pageTitleX(); // under the title's first letter
	char cut[sizeof(line)];
	GFX_truncateText(font.tiny, line, cut, screen->w - x - SCALE1(PADDING), 0);
	SDL_Surface* text = GFX_getCachedText(font.tiny, cut, COLOR_GRAY);
	if (text)
		SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){x, SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2)});
}

// The selected game's one-line info (time, achievements, Next), bottom-left over an eased fade that rises
// from the hint bar. The recent's own path, so a game whose emulator is gone still shows its history.
static void drawInfo(const char* rom_path) {
	GameInfo info;
	if (!font.small || !GameInfo_get(rom_path, &info) || !(info.has_time || info.has_ra))
		return;
	InfoSeg segs[3];
	int n = GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0, info.has_time ? info.seconds : -1,
							  info.has_ra ? info.unlocked : 0, info.has_ra ? info.total : 0,
							  info.has_ra ? info.next : NULL, true, segs);
	if (n <= 0)
		return;
	int bar_top = screen->h - SCALE1(BUTTON_SIZE + BUTTON_MARGIN * 2);
	int text_h = TTF_FontHeight(font.small);
	int fade_h = NX_DP(64) + text_h;
	SDL_Surface* fade = UI_easedFadeSurface(screen->w, fade_h, 0.9f, 2.0f, false);
	if (fade) // cached: blit right away
		SDL_BlitSurface(fade, NULL, screen, &(SDL_Rect){0, bar_top - fade_h});
	int y = bar_top - SCALE1(BUTTON_MARGIN) - text_h;
	InfoBand_drawSegments(screen, segs, n, SCALE1(PADDING + BUTTON_MARGIN), false, y, screen->w * 60 / 100,
						  font.small);
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
		if (Recents_count() > 0) {
			UI_renderEmptyState(screen, "No Resumable Games", "Suspend a game to see it here", NULL);
		} else {
			// nothing played yet: centred grey text, B BACK only
			SDL_Surface* text = font.large ? GFX_getCachedText(font.large, "Nothing played yet", COLOR_GRAY) : NULL;
			if (text)
				SDL_BlitSurface(text, NULL, screen,
								&(SDL_Rect){(screen->w - text->w) / 2, (screen->h - text->h) / 2});
			UI_renderButtonHintBarEx(screen, (char*[]){"B", "BACK", NULL}, GS_SCRIM_ALPHA);
		}
		GFX_flipHidden();
		return;
	}

	Entry* selectedEntry =
		Recents_entryFromRecent(Recents_at(gs_indices[switcher_selected]));
	readyResume(selectedEntry);

	// on `screen` with the hint bar; the preview/box art goes to LAYER_BACKGROUND underneath
	Recent* recent = Recents_at(gs_indices[switcher_selected]);
	if (recent) {
		char rom_path[MAX_PATH];
		snprintf(rom_path, sizeof(rom_path), "%s%s", SDCARD_PATH, recent->path);
		drawSubtitle(rom_path);
		drawInfo(rom_path);
	}
	UI_renderButtonHintBarEx(screen,
							 (char*[]){"B", "BACK", "Y", "REMOVE", "A", resume.can_resume ? "RESUME" : "START", NULL},
							 GS_SCRIM_ALPHA);

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
			int max_w = (int)(screen->w * GS_BOXART_WIDTH);
			int max_h = (int)(screen->h * 0.6);
			int new_w, new_h;
			UI_calcImageFit(img_w, img_h, max_w, max_h, &new_w, &new_h);


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
