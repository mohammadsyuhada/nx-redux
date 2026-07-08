#include "api.h"
#include "config.h"
#include "defines.h"
#include "shortcuts.h"
#include "ui_menubar.h"
#include "utils.h"
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <msettings.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include "content.h"
#include "display_helper.h"
#include "gamelist.h"
#include "gameswitcher.h"
#include "imgloader.h"
#include "launcher.h"
#include "search.h"
#include "ui_list.h"
#include "ui_contextmenu.h"
#include "recents.h"
#include "types.h"

Directory* top;
Array* stack; // DirectoryArray

bool quit = false;
bool startgame = false;
ResumeState resume = {0};
RestoreState restore = {.depth = -1, .relative = -1};
static bool simple_mode = false;
static int animationdirection = 0;

static void Menu_init(void) {
	stack = Array_new(); // array of open Directories
	Recents_init();
	Recents_setHasEmu(hasEmu);
	Recents_setHasM3u(hasM3u);
	Shortcuts_init();

	openDirectory(SDCARD_PATH, 0);
	loadLast(); // restore state when available

	Search_init();
	GameList_init(simple_mode);
}
static void Menu_quit(void) {
	Recents_quit();
	Shortcuts_quit();
	DirectoryArray_free(stack);

	Search_quit();
}

///////////////////////////////////////

static bool dirty = true;

#define IDLE_TIMEOUT_MS 3000 // 3 seconds of no input
#define IDLE_FRAME_MS 100	 // ~10 FPS when idle
static uint32_t last_active_input = 0;

SDL_Surface* screen = NULL;
static SDL_Surface* blackBG = NULL;

// Game list screen (input + render) lives in gamelist.c

int main(int argc, char* argv[]) {
	if (autoResume())
		return 0; // nothing to do

	simple_mode = exists(SIMPLE_MODE_PATH);
	Content_setSimpleMode(simple_mode);

	InitSettings();

	screen = GFX_init(MODE_MAIN);

	PAD_init();
	VIB_init();
	PWR_init();
	if (!HAS_POWER_BUTTON && !simple_mode)
		PWR_disableSleep();

	initImageLoaderPool();
	Menu_init();
	GameSwitcher_init();
	int lastScreen = SCREEN_OFF;
	int currentScreen = CFG_getDefaultView();

	if (GameSwitcher_shouldStartInSwitcher())
		currentScreen = SCREEN_GAMESWITCHER;

	// add a nice fade into the game switcher
	if (currentScreen == SCREEN_GAMESWITCHER)
		lastScreen = SCREEN_GAME;

	// make sure we have no running games logged as active anymore (we might be
	// launching back into the UI here)
	system("gametimectl.elf stop_all");

	GFX_setVsync(VSYNC_STRICT);

	PAD_reset();
	GFX_clearLayers(LAYER_ALL);
	GFX_clear(screen);

	IndicatorType show_setting = INDICATOR_NONE;
	PWR_setCPUSpeed(CPU_SPEED_MENU);

	int selected_row = top->selected - top->start;
	bool list_show_entry_names = true;

	char folderBgPath[1024] = {0};
	folderbgbmp = NULL;

	blackBG = SDL_CreateRGBSurfaceWithFormat(
		0, screen->w, screen->h, screen->format->BitsPerPixel,
		screen->format->format);
	if (blackBG)
		SDL_FillRect(blackBG, NULL, SDL_MapRGBA(screen->format, 0, 0, 0, 255));

	while (!quit) {
		GFX_startFrame();
		unsigned long now = SDL_GetTicks();

		PAD_poll();

		if (PAD_anyPressed())
			last_active_input = SDL_GetTicks();

		// Handle context menu input (consumes input when open)
		if (ContextMenu_isOpen()) {
			// Keep long-press state machine ticking so it doesn't fire on stale state after close
			PAD_longPressedMenu(now);
			PAD_tappedMenu(now);

			ContextMenuResult cmr = ContextMenu_handleInput();
			if (cmr.action == CONTEXTMENU_SELECTED) {
				GameList_runContextAction(cmr.id); // may run a blocking modal
				dirty = true;
			} else if (cmr.action != CONTEXTMENU_NONE) {
				dirty = true; // redraw underlying screen after close
			} else if (PAD_anyJustPressed()) {
				// Redraw overlay only when navigating (selection changed)
				dirty = true;
			}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		// Check if a thumbnail finished loading asynchronously
		if (thumbCheckAsyncLoaded())
			dirty = true;

		int gsanimdir = ANIM_NONE;

		if (currentScreen == SCREEN_GAMESWITCHER) {
			GameSwitcherResult gsr = GameSwitcher_handleInput(now);
			if (gsr.dirty)
				dirty = true;
			if (gsr.folderbgchanged)
				folderbgchanged = 1;
			if (gsr.startgame)
				startgame = true;
			if (gsr.screen != SCREEN_GAMESWITCHER) {
				currentScreen = gsr.screen;
				if (currentScreen == SCREEN_GAMELIST)
					animationdirection = SLIDE_DOWN;
			}
			gsanimdir = gsr.gsanimdir;
		} else if (currentScreen == SCREEN_SEARCH) {
			SearchResult sr = Search_handleInput(now);
			if (sr.dirty)
				dirty = true;
			if (sr.folderbgchanged)
				folderbgchanged = 1;
			if (sr.startgame)
				startgame = true;
			if (sr.screen != SCREEN_SEARCH) {
				currentScreen = sr.screen;
				if (currentScreen == SCREEN_GAMELIST)
					animationdirection = SLIDE_RIGHT;
			}
		} else if (!ContextMenu_isOpen()) {
			GameListResult glr =
				GameList_handleInput(now, currentScreen, show_setting, &dirty);
			currentScreen = glr.screen;
			if (glr.animdir != ANIM_NONE)
				animationdirection = glr.animdir;
			if (glr.folderbgchanged)
				folderbgchanged = 1;
		}

		// TG5050: search keyboard may have triggered display recovery (new screen surface)
		{
			SDL_Surface* ns = DisplayHelper_getReinitScreen();
			if (ns) {
				screen = ns;
				dirty = true;
			}
		}

		if (dirty) {
			SDL_Surface* tmpOldScreen = NULL;
			if (animationdirection != ANIM_NONE) {
				tmpOldScreen = GFX_captureRendererToSurface();
				if (tmpOldScreen)
					SDL_SetSurfaceBlendMode(tmpOldScreen, SDL_BLENDMODE_BLEND);
			}

			if (lastScreen == SCREEN_GAME || lastScreen == SCREEN_OFF) {
				GFX_clearLayers(LAYER_ALL);
				GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0,
								LAYER_BACKGROUND);
			} else {
				GFX_clearLayers(LAYER_TRANSITION);
				if (lastScreen != SCREEN_GAMELIST)
					GFX_clearLayers(LAYER_THUMBNAIL);
				GFX_clearLayers(LAYER_SCROLLTEXT);
				GFX_clearLayers(LAYER_OVERLAY);
			}
			GFX_clear(screen);

			// render top menu bar
			const char* menu_title;
			if (currentScreen == SCREEN_GAMESWITCHER)
				menu_title = GameSwitcher_getSelectedName();
			else if (currentScreen == SCREEN_SEARCH)
				menu_title = "Search";
			else
				menu_title = stack->count > 1 ? top->name : "NX Redux";
			int ow = UI_renderMenuBar(screen, menu_title);

			// capture menu bar for fixed overlay during animation
			SDL_Surface* menuBarSurface = NULL;
			if (animationdirection != ANIM_NONE)
				menuBarSurface = UI_captureMenuBar(screen);

			if (currentScreen == SCREEN_SEARCH) {
				Search_render(screen, blackBG, lastScreen);
				lastScreen = SCREEN_SEARCH;
			} else if (startgame) {
				GFX_clearLayers(LAYER_ALL);
				GFX_clear(screen);
				GFX_flipHidden();
			} else if (currentScreen == SCREEN_GAMESWITCHER) {
				GameSwitcher_render(lastScreen, blackBG, gsanimdir);
				lastScreen = SCREEN_GAMESWITCHER;
			} else {
				GameList_render(screen, lastScreen, show_setting, blackBG);
				lastScreen = SCREEN_GAMELIST;
			}

			if (animationdirection != ANIM_NONE) {
				if (CFG_getMenuTransitions()) {
					if (lastScreen != SCREEN_GAMESWITCHER) {
						GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0,
										LAYER_BACKGROUND);
						folderbgchanged = 1;
					}
					GFX_clearLayers(LAYER_TRANSITION);
					GFX_clearLayers(LAYER_THUMBNAIL);
					if (menuBarSurface)
						GFX_drawOnLayer(menuBarSurface, 0, 0, screen->w,
										menuBarSurface->h, 1.0f, 0, LAYER_OVERLAY);
					GFX_flipHidden();
					SDL_Surface* tmpNewScreen = GFX_captureRendererToSurface();
					SDL_SetSurfaceBlendMode(tmpNewScreen, SDL_BLENDMODE_BLEND);
					GFX_clearLayers(LAYER_THUMBNAIL);
					if (animationdirection == SLIDE_LEFT)
						GFX_animateSlidePages(
							tmpOldScreen, 0, 0, 0 - FIXED_WIDTH, 0,
							tmpNewScreen, FIXED_WIDTH, 0, 0, 0,
							FIXED_WIDTH, FIXED_HEIGHT, 250, LAYER_THUMBNAIL);
					if (animationdirection == SLIDE_RIGHT)
						GFX_animateSlidePages(
							tmpOldScreen, 0, 0, FIXED_WIDTH, 0,
							tmpNewScreen, 0 - FIXED_WIDTH, 0, 0, 0,
							FIXED_WIDTH, FIXED_HEIGHT, 250, LAYER_THUMBNAIL);
					if (animationdirection == SLIDE_DOWN)
						GFX_animateSlidePages(
							tmpOldScreen, 0, 0, 0, FIXED_HEIGHT,
							tmpNewScreen, 0, 0 - FIXED_HEIGHT, 0, 0,
							FIXED_WIDTH, FIXED_HEIGHT, 250, LAYER_THUMBNAIL);
					if (animationdirection == SLIDE_UP)
						GFX_animateSlidePages(
							tmpOldScreen, 0, 0, 0, 0 - FIXED_HEIGHT,
							tmpNewScreen, 0, FIXED_HEIGHT, 0, 0,
							FIXED_WIDTH, FIXED_HEIGHT, 250, LAYER_THUMBNAIL);
					GFX_clearLayers(LAYER_THUMBNAIL);
					GFX_clearLayers(LAYER_OVERLAY);
					SDL_FreeSurface(tmpNewScreen);
				}
				// animation done
				animationdirection = ANIM_NONE;
			}
			if (menuBarSurface)
				SDL_FreeSurface(menuBarSurface);

			if (lastScreen == SCREEN_SEARCH) {
				updateBackgroundLayer(blackBG);
				renderThumbnail(1, false);
			} else if (lastScreen == SCREEN_GAMELIST) {
				updateBackgroundLayer(blackBG);
				renderThumbnail(1, false);

				GFX_clearLayers(LAYER_TRANSITION);
				if (!GameList_scrollIsScrolling())
					GFX_clearLayers(LAYER_SCROLLTEXT);
			}
			if (ContextMenu_isOpen()) {
				GFX_clearLayers(LAYER_SCROLLTEXT);
				ContextMenu_render(screen);
			}
			if (!startgame) // dont flip if game gonna start
				GFX_flip(screen);

			if (tmpOldScreen)
				SDL_FreeSurface(tmpOldScreen);

			dirty = false;
		} else if (folderbgchanged || thumbchanged || GameList_scrollBusy()) {
			updateBackgroundLayer(blackBG);
			renderThumbnail(1, false);
			if (currentScreen != SCREEN_GAMESWITCHER &&
				currentScreen != SCREEN_SEARCH) {
				GameList_scrollTickIdle();
			} else {
				SDL_Delay(16);
			}
			// Flush layer changes (e.g. new thumbnail) to screen
			if (getNeedDraw()) {
				PLAT_GPU_Flip();
				setNeedDraw(0);
			}
			dirty = false;
		} else {
			// want to draw only if needed. getNeedDraw() is an SDL atomic, so it
			// needs no locking — holding the loader queue mutexes here (across the
			// idle SDL_Delay below) only blocked the worker threads from dequeuing
			// pending thumbnail/background loads for up to IDLE_FRAME_MS.
			if (getNeedDraw()) {
				PLAT_GPU_Flip();
				setNeedDraw(0);
			} else {
				unsigned long elapsed = SDL_GetTicks() - now;
				int frame_target = (SDL_GetTicks() - last_active_input > IDLE_TIMEOUT_MS) ? IDLE_FRAME_MS : 16;
				if (elapsed < frame_target)
					SDL_Delay(frame_target - elapsed);
			}
		}

		SDL_LockMutex(frameMutex);
		frameReady = true;
		SDL_CondSignal(flipCond);
		SDL_UnlockMutex(frameMutex);

		// animation does not carry over between loops, this should only ever be set
		// by input handling and directly consumed by the following render pass
		assert(animationdirection == ANIM_NONE);

		// handle HDMI change
		static int had_hdmi = -1;
		int has_hdmi = GetHDMI();
		if (had_hdmi == -1)
			had_hdmi = has_hdmi;
		if (has_hdmi != had_hdmi) {
			had_hdmi = has_hdmi;

			Entry* entry = top->entries->count > 0
							   ? top->entries->items[top->selected]
							   : NULL;
			LOG_info("restarting after HDMI change... (%s)\n",
					 entry ? entry->path : "no selection");
			if (entry)
				saveLast(entry->path); // NOTE: doesn't work in Recents (by design)
			sleep(4);
			quit = true;
		}
	}

	// Fast exit when launching a game — skip full cleanup to minimize
	// delay. The OS reclaims all memory/FDs on process exit. The parent
	// shell script reads /tmp/next only after nextui.elf exits.
	if (startgame) {
		GFX_quit();
		_exit(0);
	}

	Menu_quit();
	PWR_quit();
	PAD_quit();

	// Cleanup scroll text state
	GameList_clearScroll();

	// Cleanup worker threads and their synchronization primitives
	cleanupImageLoaderPool();

	GFX_quit(); // Cleanup video subsystem first to stop GPU threads

	// Now safe to free surfaces after GPU threads are stopped
	if (blackBG)
		SDL_FreeSurface(blackBG);
	if (folderbgbmp)
		SDL_FreeSurface(folderbgbmp);
	if (thumbbmp)
		SDL_FreeSurface(thumbbmp);

	QuitSettings();
}