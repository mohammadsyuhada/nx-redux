#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#if defined(PLATFORM_TG5050)
#include "../../tg5050/platform/platform.h"
#else
#include "../../tg5040/platform/platform.h"
#endif
#include "../common/api.h"

extern int psa_crypto_init(void);
#include "music_client.h"
#include "album_art.h"

// UI modules
#include "ui_icons.h"
#include "ui_podcast.h"
#include "../common/ui/ui_splash.h"
#include "../common/ui/ui_confirmdialog.h"
#include "../common/wifi.h"

// Module architecture
#include "module_common.h"
#include "module_menu.h"
#include "module_library.h"
#include "module_player.h"
#include "module_radio.h"
#include "module_podcast.h"
#include "module_settings.h"
#include "settings.h"
#include "resume.h"
#include "background.h"
#include "../common/display_helper.h"

void InitSettings(void);
void QuitSettings(void);

// Global quit flag. sig_atomic_t + volatile is the only type a signal handler
// may portably write and the main loop reliably re-read.
static volatile sig_atomic_t quit = 0;
static SDL_Surface* screen;

static void sigHandler(int sig) {
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		quit = 1;
		break;
	default:
		break;
	}
}

int main(int argc, char* argv[]) {
	(void)argc;

	bool settings_ready = false;

	screen = GFX_init(MODE_MAIN);
	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	UI_showSplashScreen(screen, "Music Player");

	InitSettings();
	PAD_init();
	PWR_init();
	WIFI_init();
	psa_crypto_init();
	Icons_init();

	// The connecting screen must also reset the podcast title scroll state
	Wifi_setConnectScreenHook(Podcast_clearTitleScroll);

	signal(SIGINT, sigHandler);
	signal(SIGTERM, sigHandler);

	// Seed random number generator for shuffle
	srand((unsigned int)time(NULL));

	/* The owner is packaged in the system bin directory, not beside this UI
	 * pak. Keep startup independent of argv[0] and of the launch cwd. */
	album_art_init();
	if (MusicClient_init(SDCARD_PATH "/.system/bin/musicplayerd.elf") != 0) {
		LOG_error("Failed to connect to music service\n");
		(void)UI_confirmModal(screen, "Music service unavailable",
							  "The background music service did not start. Reinstall or update NX Redux and try again.",
							  NULL, true, true);
		goto cleanup;
	}

	// Initialize common module (global input handling)
	ModuleCommon_init();

	// Initialize app-specific settings
	Settings_init();
	settings_ready = true;

	// Initialize resume state
	Resume_init();


	// Main application loop
	while (!quit) {
		// Run main menu - returns selected item or MENU_QUIT
		int selection = MenuModule_run(screen);

		if (selection == MENU_QUIT) {
			quit = 1;
			continue;
		}

		// Run the selected module
		ModuleExitReason reason = MODULE_EXIT_TO_MENU;

		switch (selection) {
		case MENU_RESUME: { // Also MENU_NOW_PLAYING (same slot)
			/* The daemon owns the decoder. If it already restored a source, attach
			 * the matching UI without loading, seeking, or changing transport. This
			 * also covers a stopped local resume, which is not "playing" but still
			 * has a loaded owner track. */
			const MusicSnapshotWire* owner = MusicClient_snapshot();
			if (owner->source == MUSIC_SOURCE_LOCAL && owner->current_file[0])
				reason = PlayerModule_run(screen);
			else if (owner->source == MUSIC_SOURCE_RADIO && owner->current_file[0])
				reason = RadioModule_run(screen);
			else if (owner->source == MUSIC_SOURCE_PODCAST && owner->current_file[0])
				reason = PodcastModule_run(screen);
			else if (Background_isPlaying()) {
				// "Now Playing" — route to the active background module.
				switch (Background_getActive()) {
				case BG_MUSIC:
					reason = PlayerModule_run(screen);
					break;
				case BG_RADIO:
					reason = RadioModule_run(screen);
					break;
				case BG_PODCAST:
					reason = PodcastModule_run(screen);
					break;
				default:
					break;
				}
			} else {
				// "Resume" — load saved local state when no owner is attached.
				const ResumeState* rs = Resume_getState();
				if (rs)
					reason = PlayerModule_runResume(screen, rs);
			}
			break;
		}
		case MENU_LIBRARY:
			reason = LibraryModule_run(screen);
			break;
		case MENU_RADIO:
			reason = RadioModule_run(screen);
			break;
		case MENU_PODCAST:
			reason = PodcastModule_run(screen);
			break;
		case MENU_SETTINGS:
			reason = SettingsModule_run(screen);
			break;
		}

		// TG5050: modules may have triggered display recovery (new screen surface)
		{
			SDL_Surface* ns = DisplayHelper_getReinitScreen();
			if (ns)
				screen = ns;
		}

		if (reason == MODULE_EXIT_QUIT) {
			quit = 1;
		}
	}

cleanup:
	GFX_clear(screen);
	GFX_flip(screen);

	// Closing the client detaches the UI; the daemon remains the playback owner.
	// Only persist settings if Settings_init actually ran. A failed service
	// connection must not save the all-zero settings struct over user state.
	if (settings_ready)
		Settings_quit();
	ModuleCommon_quit();
	MusicClient_quit();
	album_art_cleanup();
	Icons_quit();

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
