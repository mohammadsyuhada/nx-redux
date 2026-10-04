#include "api.h"
#include "config.h"
#include "defines.h"
#include "shortcuts.h"
#include "ui_menubar.h"
#include "utils.h"
#include <assert.h>
#include <msettings.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "content.h"
#include "contentdim.h"
#include "display_helper.h"
#include "collcount.h"
#include "gameinfo.h"
#include "home_stats.h"
#include "gamelist.h"
#include "gridview.h"
#include "home.h"
#include "homeart.h"
#include "infoband.h"
#include "gameswitcher.h"
#include "imgloader.h"
#include "launcher.h"
#include "menuart.h"
#include "controller_art.h"
#include "menutabs.h"
#include "ui_font.h"
#include "search.h"
#include "tiles.h"
#include "ui_contextmenu.h"
#include "ui_fade.h"
#include "ui_listview.h"
#include "recents.h"
#include "rowview.h"
#include "types.h"
#include "cpu_policy.h"

// Boot-phase stamps into the same file the launch scripts write, so a slow
// boot can be attributed (script phase vs GFX init vs content scan vs first
// frame) straight from /tmp/nextui_boottime on the device.
static void bootStamp(const char* tag) {
	FILE* file = fopen("/tmp/nextui_boottime", "a");
	if (!file)
		return;
	char uptime[64] = "?";
	FILE* up = fopen("/proc/uptime", "r");
	if (up) {
		if (fgets(uptime, sizeof(uptime), up))
			trimTrailingNewlines(uptime);
		fclose(up);
	}
	fprintf(file, "nextui %s %s\n", tag, uptime);
	fclose(file);
}

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

	MenuTabs_init();
	char last_path[MAX_PATH] = "";
	if (exists(LAST_PATH))
		getFile(LAST_PATH, last_path, sizeof(last_path));
	MenuTabs_openRoot(MenuTabs_initialTab(last_path));
	loadLast(); // restore state when available

	Search_init();
	GameList_init(simple_mode);
}
static void Menu_quit(void) {
	Recents_quit();
	Shortcuts_quit();
	DirectoryArray_free(stack);
	MenuTabs_quit(); // the parked roots (never in the stack)

	Search_quit();
	InfoBand_quit();
	UI_fadeCacheClear();
	MenuArt_quit();
	ControllerArt_quit();
}

///////////////////////////////////////

static bool dirty = true;

#define IDLE_TIMEOUT_MS 3000 // 3 seconds of no input
#define IDLE_FRAME_MS 100	 // ~10 FPS when idle
#define PREFETCH_IDLE_MS 8	 // of an idle 16 ms slot, what building ahead may use (the rest sleeps)
#define PREFETCH_MARGIN_MS 2 // a dirty frame's leftover kept free, so the next frame's input and render start on time
static uint32_t last_active_input = 0;

// CPU frequency policy: full range through the boot-time init, the menu cap
// while navigating, a lower cap when idle. Pure state machine in cpu_policy.c;
// this is the only place it touches the hardware.
static CPUPolicy cpu_policy;
static bool cpu_policy_started = false;
static bool cpuBootDone(void) {
	return exists(CPU_POLICY_BOOT_MARKER);
}
static void startCPUPolicy(bool boot_done);
static void applyCPUPolicy(CPUPolicyAction action) {
	// Big core (tg5050): online for the boot phase, offline for the rest of the
	// launcher's life — see PLAT_setBigCoreOnline. Order matters: online before
	// driving its policy, drive it (harmless once gone) before taking it down.
	switch (action) {
	case CPU_POLICY_SET_AUTO:
		PLAT_setBigCoreOnline(true);
		PWR_setCPUSpeedAuto();
		break;
	case CPU_POLICY_SET_MENU:
		PWR_setCPUSpeed(CPU_SPEED_MENU);
		PLAT_setBigCoreOnline(false);
		break;
	case CPU_POLICY_SET_IDLE:
		PWR_setCPUSpeed(CPU_SPEED_MENU_IDLE);
		PLAT_setBigCoreOnline(false);
		break;
	case CPU_POLICY_KEEP:
		break;
	}
}
static void startCPUPolicy(bool boot_done) {
	// GFX_init's 1 s startup boost would otherwise restore the pre-boost cap
	// under the policy's full-range boot phase (seen as a 0.4 s dip to the
	// menu cap right before the first frame).
	GFX_endStartupBoost();
	applyCPUPolicy(CPUPolicy_start(&cpu_policy, SDL_GetTicks(), boot_done));
	cpu_policy_started = true;
}

SDL_Surface* screen = NULL;
static SDL_Surface* blackBG = NULL;

// Game list screen (input + render) lives in gamelist.c

// Crop away the top menu-bar strip of a captured full-screen surface, returning
// just the content region below it. The vertical (game switcher) screen
// transition slides these cropped pages so they carry no menu bar of their own —
// the fixed LAYER_OVERLAY bar owns the top strip and stays put while only the
// content beneath it slides. Caller frees the returned surface.
static SDL_Surface* cropBelowMenuBar(SDL_Surface* src, int bar_h) {
	if (!src || bar_h < 0 || bar_h >= src->h)
		return NULL;
	int cw = src->w;
	int ch = src->h - bar_h;
	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(
		0, cw, ch, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!out)
		return NULL;
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(src, &(SDL_Rect){0, bar_h, cw, ch}, out, NULL);
	SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_NONE);
	return out;
}

// A game list's title names its parent (LIST-LAYOUT §10.1): a console's list "Consoles | <console>", a
// collection's "Collections | <name>", a folder deeper in a console "<console> | <folder>"; any other list (Tools,
// a folder outside Roms) keeps its plain name.
static const char* listTitle(char* out, size_t size) {
	char* name = top->name;
	trimSortingMeta(&name);
	if (prefixMatch(COLLECTIONS_PATH, top->path) && !exactMatch(COLLECTIONS_PATH, top->path))
		return UI_pageTitle(out, size, "Collections", name);
	size_t roms_len = strlen(ROMS_PATH);
	if (strncmp(top->path, ROMS_PATH, roms_len) != 0 || top->path[roms_len] != '/') {
		snprintf(out, size, "%s", name);
		return out;
	}
	const char* seg = top->path + roms_len + 1;
	const char* slash = strchr(seg, '/');
	if (!slash)
		return UI_pageTitle(out, size, "Consoles", name);

	// deeper: the console is the stack's ROMS_PATH/<console> entry (its display name), else that path's name
	char console_path[MAX_PATH];
	snprintf(console_path, sizeof(console_path), "%.*s", (int)(slash - top->path), top->path);
	char console_buf[MAX_PATH];
	char* console = NULL;
	for (int i = 0; i < stack->count && !console; i++) {
		Directory* d = stack->items[i];
		if (exactMatch(d->path, console_path))
			console = d->name;
	}
	if (!console) {
		getDisplayName(console_path, console_buf);
		console = console_buf;
	}
	trimSortingMeta(&console);
	return UI_pageTitle(out, size, console, name);
}

int main(int argc, char* argv[]) {
	// Must precede autoResume(): that path returns before the rest of init, so
	// a stale flag would ride into the auto-resumed game as a silent netplay
	// launch. Stale = a previous launch never consumed it.
	unlink(NETPLAY_LAUNCH_PATH);

	if (autoResume())
		return 0; // nothing to do

	// Lift the CPU cap before InitSettings, not just inside GFX_init: on tg5050
	// the settings library alone is ~0.45s of the ~1s start-to-first-frame.
	GFX_startStartupBoost(MODE_MAIN);
	// Fresh boot: take over from that 1 s boost right away and run the whole
	// start-up (InitSettings, GFX_init, menu init incl. a first-boot ROM
	// rescan) at full range — on tg5050 GFX_init alone outlasts the boost,
	// which showed as a 0.8 s dip to the floor before the first frame. A
	// relaunch (marker present) keeps the boost and caps at its first frame.
	if (!cpuBootDone())
		startCPUPolicy(false);

	simple_mode = exists(SIMPLE_MODE_PATH);
	Content_setSimpleMode(simple_mode);

	bootStamp("start");
	InitSettings();

	screen = GFX_init(MODE_MAIN);
	bootStamp("after gfx init");

	PAD_init();
	VIB_init();
	PWR_init();
	if (!HAS_POWER_BUTTON && !simple_mode)
		PWR_disableSleep();

	initImageLoaderPool();
	GameInfo_init();
	HomeStats_init();
	CollCount_init();
	Menu_init();
	Home_reset(); // Continue and the pins for this menu show (nextui restarts after every game); the stats are
				  // requested when Home is first shown
	bootStamp("after menu init");
	GameSwitcher_init();
	int lastScreen = SCREEN_OFF;
	int currentScreen = CFG_getDefaultView();

	if (GameSwitcher_shouldStartInSwitcher())
		currentScreen = SCREEN_GAMESWITCHER;

	// add a nice fade into the game switcher
	if (currentScreen == SCREEN_GAMESWITCHER)
		lastScreen = SCREEN_GAME;

	// make sure we have no running games logged as active anymore (we might be
	// launching back into the UI here) — backgrounded: it finishes long before
	// a human can navigate to a game, and waiting on it held up the first frame
	system("gametimectl.elf stop_all &");

	GFX_setVsync(VSYNC_STRICT);

	PAD_reset();
	GFX_clearLayers(LAYER_ALL);
	GFX_clear(screen);

	IndicatorType show_setting = INDICATOR_NONE;

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
		if (cpu_policy_started) // idle drop / wake / end of boot phase, before this frame renders
			applyCPUPolicy(CPUPolicy_update(&cpu_policy, now, PAD_anyPressed(), cpuBootDone));

		// External pak-launch request: a file naming a pak directory, written
		// by something outside nextui (the OSD Music widget asks for the Music
		// Player this way). Consumed here so the pak goes through the same
		// in-place launch as the F1/F2 shortcuts and nextui exits cleanly
		// instead of being killed around a hand-written /tmp/next.
		if (exists(OPEN_PAK_REQUEST_PATH)) {
			char pak_path[MAX_PATH] = {0};
			FILE* request = fopen(OPEN_PAK_REQUEST_PATH, "r");
			if (request) {
				if (!fgets(pak_path, sizeof(pak_path), request))
					pak_path[0] = '\0';
				fclose(request);
			}
			unlink(OPEN_PAK_REQUEST_PATH);
			pak_path[strcspn(pak_path, "\r\n")] = '\0';
			char launch_path[MAX_PATH];
			if (pak_path[0] == '/' && snprintf(launch_path, sizeof(launch_path), "%s/launch.sh", pak_path) < (int)sizeof(launch_path) && exists(launch_path))
				openPakInPlace(pak_path);
		}

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
				GameList_contextMenuClosed(); // drop Home's entry copy
				dirty = true;				  // redraw underlying screen after close
			} else if (PAD_anyJustPressed() || PAD_justRepeated(BTN_UP) ||
					   PAD_justRepeated(BTN_DOWN)) {
				// Redraw overlay only when navigating (selection changed).
				// ContextMenu_handleInput moves cm_selected via PAD_navigateMenu,
				// which fires on PAD_justRepeated — a held direction changed the
				// selection with no redraw until the next unrelated dirty event.
				dirty = true;
			}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		// Check if a thumbnail finished loading asynchronously
		if (thumbCheckAsyncLoaded())
			dirty = true;
		// Game info (play time, achievements) finished on its worker
		if (GameInfo_checkAsyncLoaded())
			dirty = true;
		// A collection's game count finished on its worker
		if (CollCount_checkAsyncLoaded())
			dirty = true;
		// Home: a picture finished loading, or the stats card's numbers arrived
		if (HomeArt_checkAsyncLoaded())
			dirty = true;
		if (HomeStats_checkAsyncLoaded())
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
				if (currentScreen == SCREEN_GAMELIST) {
					animationdirection = SLIDE_DOWN;
					// the switcher's readyResume clobbered the shared resume
					// state; recompute it for the list's own selection so the
					// hint bar doesn't show a stale RESUME
					readyResume(top->entries->count > 0
									? top->entries->items[top->selected]
									: NULL);
				}
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
			bool was_backdrop = RowView_paintsScreen();
			GameListResult glr =
				GameList_handleInput(now, currentScreen, show_setting, &dirty);
			// into or out of a Backdrop game list: no page slide (the Menu transitions setting's), its picture fades
			// in from / out to black instead; the new list or tab is rebuilt in its own layout at once
			if ((glr.animdir == SLIDE_LEFT || glr.animdir == SLIDE_RIGHT) && (was_backdrop || RowView_paintsScreen()))
				glr.animdir = ANIM_NONE;
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

		// Keep redrawing while the selection pill glides to its new row, the
		// tab underline to its new tab, a Grid slides or crossfades its lit tile,
		// a Carousel/Backdrop row slides or crossfades its picture, or the content
		// dims for (or lights up from) tab-row focus. Settled, nothing redraws.
		if (currentScreen == SCREEN_GAMELIST && !ContextMenu_isOpen() &&
			(GameList_pillAnimating() || MenuTabs_animating() || Home_animating() || GridView_animating() ||
			 RowView_animating() || MenuTabs_dimAnimating()))
			dirty = true;

		// Search's dirty signal comes entirely from sr.dirty above
		// (selection travel + glide, set in Search_handleInput). Do NOT add
		// UI_listViewBusy(Search_view()) here: its needsRender term would run
		// full renders through the marquee's 1s pre-scroll delay, re-uploading
		// the thumbnail layer every frame - visible artwork flicker.

		if (dirty) {
			SDL_Surface* tmpOldScreen = NULL;
			if (animationdirection != ANIM_NONE) {
				tmpOldScreen = GFX_captureRendererToSurface();
				if (tmpOldScreen)
					SDL_SetSurfaceBlendMode(tmpOldScreen, SDL_BLENDMODE_BLEND);
			}

			if (lastScreen == SCREEN_GAME || lastScreen == SCREEN_OFF) {
				GFX_clearLayers(LAYER_ALL);
				if (blackBG)
					GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0,
									LAYER_BACKGROUND);
			} else {
				GFX_clearLayers(LAYER_TRANSITION);
				if (lastScreen != SCREEN_GAMELIST)
					GFX_clearLayers(LAYER_THUMBNAIL);
				GFX_clearLayers(LAYER_SCROLLTEXT);
				GFX_clearLayers(LAYER_OVERLAY);
			}
			// a Backdrop game row's picture paints every pixel: no clear under it
			if (!(currentScreen == SCREEN_GAMELIST && !startgame && RowView_paintsScreen()))
				GFX_clear(screen);

			// A Backdrop game list's picture: the bottom-most layer, under the band and the bar. With it on screen
			// the eased top band is skipped and the bar's text gets the dark "over art" shadows. (Never at the root:
			// a main-menu tab has no picture.)
			bool over_art = currentScreen == SCREEN_GAMELIST && !startgame && RowView_renderPicture(screen);

			// render top menu bar
			char list_title[MAX_PATH * 2];
			const char* menu_title;
			if (currentScreen == SCREEN_GAMESWITCHER)
				menu_title = GameSwitcher_getSelectedName();
			else if (currentScreen == SCREEN_SEARCH)
				menu_title = "Search";
			else if (stack->count > 1)
				menu_title = listTitle(list_title, sizeof(list_title));
			else
				menu_title = NULL; // the root draws the tab row in the bar instead
			int ow;
			if (currentScreen == SCREEN_GAMELIST || currentScreen == SCREEN_GAMESWITCHER) {
				// an eased fade from the top edge replaces the bar's flat scrim: over the art, under the text
				int bar_h = BAR_HEIGHT;
				int fade_h = currentScreen == SCREEN_GAMESWITCHER
								 ? bar_h + (font.tiny ? TTF_FontHeight(font.tiny) : 0) + NX_DP(64) // 64 dp below the subtitle
								 : bar_h + NX_DP(48);
				// Home shows it only while its page is scrolled
				bool home = currentScreen == SCREEN_GAMELIST && Home_active();
				SDL_Surface* fade = !over_art && (!home || Home_scrolled())
										? UI_easedFadeSurface(screen->w, fade_h, 0.9f, 3.5f, true)
										: NULL;
				// cached: blit right away. Home draws the part below the strip itself, over its page; a Grid screen
				// paints its body plain black, so the part below the strip would only be painted over.
				bool strip_only = home || (currentScreen == SCREEN_GAMELIST && GridView_active());
				if (fade)
					UI_blitFade(fade, strip_only ? &(SDL_Rect){0, 0, screen->w, bar_h} : NULL, screen, 0, 0);
				// a game list's title starts where its content does: the List rows' 14 dp inset, the 24 dp gutter of
				// Grid, Carousel and Backdrop (LIST-LAYOUT §10.1)
				int title_x = currentScreen == SCREEN_GAMELIST && GameList_currentStyle() != MENU_STYLE_LIST
								  ? NX_NATIVE_DP(NX_MENU_GUTTER_DP)
								  : -1;
				ow = UI_renderMenuBarAt(screen, menu_title, NULL, title_x, false, over_art);
			} else {
				ow = UI_renderMenuBar(screen, menu_title);
			}
			if (currentScreen == SCREEN_GAMELIST && stack->count == 1)
				MenuTabs_renderRow(screen, ow); // the root has no picture: the plain tab row

			// capture menu bar for fixed overlay during animation
			SDL_Surface* menuBarSurface = NULL;
			if (animationdirection != ANIM_NONE)
				menuBarSurface = UI_captureMenuBar(screen);

			if (currentScreen == SCREEN_SEARCH) {
				Search_render(screen, lastScreen);
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
				RowView_renderExit(screen); // B's fade out of a Backdrop game list: the whole frame toward black
				lastScreen = SCREEN_GAMELIST;
			}

			if (animationdirection != ANIM_NONE) {
				if (CFG_getMenuTransitions()) {
					if (lastScreen != SCREEN_GAMESWITCHER) {
						if (blackBG)
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
					if (tmpNewScreen) {
						SDL_SetSurfaceBlendMode(tmpNewScreen, SDL_BLENDMODE_BLEND);
						GFX_clearLayers(LAYER_THUMBNAIL);
						// The captured page already holds the list's info lines (LAYER_OVERLAY); keep
						// only the fixed menu bar on that layer while the pages slide.
						GFX_clearLayers(LAYER_OVERLAY);
						if (menuBarSurface)
							GFX_drawOnLayer(menuBarSurface, 0, 0, screen->w,
											menuBarSurface->h, 1.0f, 0, LAYER_OVERLAY);
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
						if (animationdirection == SLIDE_DOWN ||
							animationdirection == SLIDE_UP) {
							// Vertical (game switcher) transitions must hold the
							// top menu bar fixed, matching the horizontal folder
							// slides. Horizontal works because each sliding page's
							// own menu bar never leaves the top strip the fixed
							// LAYER_OVERLAY bar covers; a vertical slide would drag
							// those bars down through the content region where the
							// overlay no longer masks them. So slide only the
							// region *below* the bar and let the fixed overlay own
							// the top strip.
							int bar_h = SCALE1(BUTTON_SIZE) +
										SCALE1(BUTTON_MARGIN * 2);
							SDL_Surface* oldContent =
								cropBelowMenuBar(tmpOldScreen, bar_h);
							SDL_Surface* newContent =
								cropBelowMenuBar(tmpNewScreen, bar_h);
							if (oldContent && newContent) {
								int cw = newContent->w;
								int ch = newContent->h;
								if (animationdirection == SLIDE_UP)
									GFX_animateSlidePages(
										oldContent, 0, bar_h, 0, bar_h - ch,
										newContent, 0, bar_h + ch, 0, bar_h,
										cw, ch, 250, LAYER_THUMBNAIL);
								else
									GFX_animateSlidePages(
										oldContent, 0, bar_h, 0, bar_h + ch,
										newContent, 0, bar_h - ch, 0, bar_h,
										cw, ch, 250, LAYER_THUMBNAIL);
							}
							if (oldContent)
								SDL_FreeSurface(oldContent);
							if (newContent)
								SDL_FreeSurface(newContent);
						}
						GFX_clearLayers(LAYER_THUMBNAIL);
						GFX_clearLayers(LAYER_OVERLAY);
						// the slide is over: the list's info lines go back on their layer
						if (lastScreen == SCREEN_GAMELIST)
							GameList_renderInfoLayer();
						SDL_FreeSurface(tmpNewScreen);
					}
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
				// Both GPU-layer uploads are deferred while the selection pill
				// is gliding: a full-screen layer upload costs ~25ms in one
				// frame — a visible mid-glide hitch. folderbgchanged /
				// thumbchanged stay latched, so the new background/thumbnail
				// lands on the first frame after the glide settles (via the
				// idle thumbchanged branch below).
				if (!GameList_pillAnimating() && !MenuTabs_underlineGliding()) {
					updateBackgroundLayer(blackBG);
					renderThumbnail(1, false);
				}

				GFX_clearLayers(LAYER_TRANSITION);
				if (!GameList_scrollIsScrolling())
					GFX_clearLayers(LAYER_SCROLLTEXT);
			}
			if (ContextMenu_isOpen()) {
				GFX_clearLayers(LAYER_SCROLLTEXT);
				ContextMenu_render(screen);
			}
			if (!startgame) {								  // dont flip if game gonna start
				unsigned long work_ms = SDL_GetTicks() - now; // this frame's input and render
				GFX_flip(screen);
				static bool first_frame_stamped = false;
				if (!first_frame_stamped) {
					first_frame_stamped = true;
					bootStamp("first frame");
					if (!cpu_policy_started)
						startCPUPolicy(cpuBootDone());
				}
				// build ahead in what's left of the frame, assuming the next costs what this one did: a held D-pad
				// redraws every frame, and this is what lands its steps on cached items. No redraw: the next frame
				// picks them up.
				if (currentScreen == SCREEN_GAMELIST && !ContextMenu_isOpen() && work_ms + PREFETCH_MARGIN_MS < 16)
					GameList_prefetchIdle(SDL_GetTicks() + (Uint32)(16 - PREFETCH_MARGIN_MS - work_ms));
			}

			if (tmpOldScreen)
				SDL_FreeSurface(tmpOldScreen);

			dirty = false;
		} else if (folderbgchanged || thumbchanged ||
				   (GameList_scrollBusy() && !ContextMenu_isOpen()) ||
				   (currentScreen == SCREEN_SEARCH &&
					UI_listViewMarqueeBusy(Search_view()))) {
			updateBackgroundLayer(blackBG);
			renderThumbnail(1, false);
			// Same flash as the dirty pass (see gamelist.c / 81a0c40a): the idle
			// marquee tick presents the screen itself (ScrollText_animateOnly ->
			// GFX_scrollTextTexture -> PLAT_GPU_Flip), which would blink the
			// marquee band out from under the context menu's scrim. Gate the
			// scrollBusy *term* too, not just the tick: needs_scroll stays
			// latched while the menu is open, so an otherwise-idle loop must
			// fall through to the sleeping path below instead of spinning here.
			if (currentScreen != SCREEN_GAMESWITCHER &&
				currentScreen != SCREEN_SEARCH && !ContextMenu_isOpen()) {
				GameList_scrollTickIdle();
			} else if (currentScreen == SCREEN_SEARCH &&
					   UI_listViewMarqueeBusy(Search_view())) {
				UI_listViewTickIdle(Search_view());
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
				// build ahead what the view's next move draws first, in part of the slot (no redraw: the next real
				// frame picks it up). Done (or nothing to build), it returns at once; while more remains the 16 ms
				// cadence holds, and only then does the deep-idle one take over.
				bool prefetching = currentScreen == SCREEN_GAMELIST && !ContextMenu_isOpen() &&
								   GameList_prefetchIdle((Uint32)now + PREFETCH_IDLE_MS);
				unsigned long elapsed = SDL_GetTicks() - now;
				int frame_target =
					(!prefetching && SDL_GetTicks() - last_active_input > IDLE_TIMEOUT_MS) ? IDLE_FRAME_MS : 16;
				if (elapsed < frame_target)
					SDL_Delay(frame_target - elapsed);
			}
		}

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
				saveLast(entry->path);
			sleep(4);
			quit = true;
		}
	}

	// External launches replace this process, so the OS can reclaim its memory,
	// descriptors, and worker threads. The parent reads /tmp/next only after exit.
	if (startgame || exists("/tmp/next")) {
		GFX_quit();
		_exit(0);
	}

	Menu_quit();
	PWR_quit();
	PAD_quit();

	// Cleanup scroll text state
	GameList_clearScroll();
	Home_quit();
	ContentDim_quit();
	GridView_quit();
	RowView_quit();
	Tiles_quit();
	UIFont_quit(); // before GFX_quit: TTF fonts close while the TTF state is intact

	// Cleanup worker threads and their synchronization primitives
	GameInfo_quit();
	HomeStats_quit();
	HomeArt_quit();
	CollCount_quit(); // writes the count cache when it changed
	cleanupImageLoaderPool();

	GFX_quit(); // Cleanup video subsystem first to stop GPU threads

	// Now safe to free surfaces after GPU threads are stopped
	if (blackBG)
		SDL_FreeSurface(blackBG);
	if (folderbgbmp)
		SDL_FreeSurface(folderbgbmp);
	if (thumbbmp)
		SDL_FreeSurface(thumbbmp);

	// Stop the rumble thread before QuitSettings() munmaps the libmsettings
	// shared memory it polls (GetRumble()), or its next poll segfaults.
	VIB_quit();
	QuitSettings();
}