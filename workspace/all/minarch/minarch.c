#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <msettings.h>

#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libgen.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <zip.h>
#include <pthread.h>
#include <glob.h>
#include <lz4.h>

// libretro-common
#include "libretro.h"
#ifdef HAS_SRM
#include "streams/rzip_stream.h"
#include "streams/file_stream.h"
#endif

#include "defines.h"
#include "api.h"
#include "audio_manager.h"
#include "utils.h"
#include "notification.h"
#include "config.h"
#include "ra_integration.h"
#include "ma_internal.h"
#include "ma_game.h"
#include "ma_saves.h"
#include "ma_rewind.h"
#include "ma_config.h"
#include "ma_shaders.h"
#include "ma_options.h"
#include "ma_opts_dump.h"
#include "ma_input.h"
#include "ma_video.h"
#include "ma_audio.h"
#include "ma_core.h"
#include "ma_menu.h"
#include "ma_frontend_opts.h"
#include "ma_runframe.h"
#include "minarch.h"
#include "netplay.h"
#include "gbalink.h"
#include "gblink.h"
#include "netplay_helper.h"
#include "netplay_boot.h"
#include <dirent.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL.h>
#include <rcheevos/rc_client.h>

///////////////////////////////////////

SDL_Surface* screen;
int quit = 0;
int newScreenshot = 0;
int show_menu = 0;
int simple_mode = 0;
enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

// default frontend options
int screen_scaling = SCALE_ASPECT;
int resampling_quality = 2;
int ambient_mode = 0;
int screen_sharpness = SHARPNESS_SOFT;
int screen_effect = EFFECT_NONE;
int cfg_screenx = 64;
int cfg_screeny = 64;
int overlay = 0;
int use_core_fps = 0;
int sync_ref = 0;
int show_debug = 0;
int max_ff_speed = 3; // 4x
int ff_audio = 0;
int fast_forward = 0;
int rewind_pressed = 0;
int rewind_toggle = 0;
int ff_toggled = 0;
int ff_hold_active = 0;
int ff_paused_by_rewind_hold = 0;
int rewinding = 0;
int rewind_cfg_enable = MINARCH_DEFAULT_REWIND_ENABLE;
int rewind_cfg_buffer_mb = MINARCH_DEFAULT_REWIND_BUFFER_MB;
int rewind_cfg_granularity = MINARCH_DEFAULT_REWIND_GRANULARITY;
int rewind_cfg_audio = MINARCH_DEFAULT_REWIND_AUDIO;
int rewind_cfg_compress = 1;
int rewind_cfg_lz4_acceleration = MINARCH_DEFAULT_REWIND_LZ4_ACCELERATION;
// Gates Rewind_init from Config_syncFrontend until startup reaches the
// explicit Rewind_init in main() — config reads before that point must not
// allocate the rewind buffers (they'd do it once per rewind key).
int rewind_init_ready = 0;
int overclock = 3; // auto
int has_custom_controllers = 0;
int gamepad_type = 0; // index in gamepad_labels/gamepad_values

// When set, video_refresh_callback drops the frame. minarch_forceCoreOptionUpdate()
// uses this to run one core frame purely to trigger check_variables() without flashing.
int skip_video_output = 0;

// these are no longer constants as of the RG CubeXX (even though they look like it)
int DEVICE_WIDTH = 0;
int DEVICE_HEIGHT = 0;
int DEVICE_PITCH = 0;
int shader_reset_suppressed = 0;

GFX_Renderer renderer;

///////////////////////////////////////

struct Core core;

struct Game game;
// moved to ma_game.c


///////////////////////////////////////
// based on picoarch/cheat.c

// moved to ma_cheats.c


///////////////////////////////////////
// moved to ma_saves.c


///////////////////////////////
// Rewind buffer (in-memory, compressed)

// moved to ma_rewind.c


///////////////////////////////

// Option/OptionCategory/OptionList types live in ma_internal.h

// moved to ma_config.c


// moved to ma_shaders.c


///////////////////////////////
static struct Special {
	int palette_updated;
} special;
void Special_updatedDMGPalette(int frames) {
	special.palette_updated = frames; // must wait a few frames
}
static void Special_refreshDMGPalette(void) {
	special.palette_updated -= 1;
	if (special.palette_updated > 0)
		return;

	int rgb = getInt("/tmp/dmg_grid_color");
	GFX_setEffectColor(rgb);
}
static void Special_init(void) {
	if (special.palette_updated > 1)
		special.palette_updated = 1;
	// else if (exactMatch((char*)core.tag, "GBC"))  {
	// 	putInt("/tmp/dmg_grid_color",0xF79E);
	// 	special.palette_updated = 1;
	// }
}
void Special_render(void) {
	if (special.palette_updated)
		Special_refreshDMGPalette();
}
static void Special_quit(void) {
	unlink("/tmp/dmg_grid_color"); // avoid spawning a shell on every quit
}
///////////////////////////////

// moved to ma_options.c


///////////////////////////////

// moved to ma_input.c


// moved to ma_environment.c


///////////////////////////////

void hdmimon(void) {
	// handle HDMI change
	static int had_hdmi = -1;
	int has_hdmi = GetHDMI();
	if (had_hdmi == -1)
		had_hdmi = has_hdmi;
	if (has_hdmi != had_hdmi) {
		had_hdmi = has_hdmi;

		Menu_beforeSleep();
		sleep(4);
		show_menu = 0;
		quit = 1;
	}
}

///////////////////////////////

// TODO: this is a dumb API
// moved to ma_menu.c


///////////////////////////////

// moved to ma_video.c

///////////////////////////////

// moved to ma_audio.c

///////////////////////////////////////

// moved to ma_core.c


///////////////////////////////////////

// moved to ma_menu.c


// MenuList/MenuItem types live in ma_frontend_opts.h

// moved to ma_frontend_opts.c


// Achievement sorting comparison function
// moved to ma_menu.c


// moved to ma_runframe.c


#define PWR_UPDATE_FREQ 5
#define PWR_UPDATE_FREQ_INGAME 20

int main(int argc, char* argv[]) {
	if (argc == 4 && !strcmp(argv[1], "--dump-options"))
		return OptsDump_run(argv[2], argv[3]);

	pthread_t cpucheckthread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&cpucheckthread, &attr, PLAT_cpu_monitor, NULL);
	pthread_attr_destroy(&attr);

	PWR_setCPUSpeed(CPU_SPEED_PERFORMANCE); // start up in performance mode, faster init
	PWR_pinToCores(CPU_CORE_PERFORMANCE);	// thread affinity

	char core_path[MAX_PATH];
	char rom_path[MAX_PATH];
	char tag_name[MAX_PATH];

	if (argc < 3)
		return EXIT_FAILURE;

	strcpy(core_path, argv[1]);
	strcpy(rom_path, argv[2]);
	getEmuName(rom_path, tag_name);

	screen = GFX_init(MODE_MENU);

	// initialize default shaders
	GFX_initShaders();
	PLAT_initNotificationTexture();

	PAD_init();
	DEVICE_WIDTH = screen->w;
	DEVICE_HEIGHT = screen->h;
	DEVICE_PITCH = screen->pitch;

	LEDS_initLeds();
	VIB_init();
	PWR_init();
	if (!HAS_POWER_BUTTON)
		PWR_disableSleep();
	MSG_init();
	IMG_Init(IMG_INIT_PNG);
	Core_open(core_path, tag_name);

	Game_open(rom_path); // nes tries to load gamegenie setting before this returns ffs
	if (!game.is_open)
		goto finish;

	simple_mode = exists(SIMPLE_MODE_PATH);

	// restore options
	Config_load(); // before init?
	Config_init();
	Config_readOptions();	 // cores with boot logo option (eg. gb) need to load options early
	setOverclock(overclock); // why twice?

	Core_init();

	// Initialize RetroAchievements after core.init() but before Core_load()
	// Set up memory accessors for achievement memory reading
	RA_setMemoryAccessors(core.get_memory_data, core.get_memory_size);
	RA_init();

	Menu_setCoreVersionDesc(core.version);
	Core_load();

	Input_init(NULL);
	Config_readOptions();  // but others load and report options later (eg. nes)
	Config_readControls(); // restore controls (after the core has reported its defaults)

	// Mute audio during startup to avoid pops (InitSettings would be logical, but too late)
	SND_overrideMute(1);
	SND_init(core.sample_rate, core.fps);
	InitSettings(); // after we initialize audio
	AudioMgr_init();
	AudioMgr_setCallback(Audio_onSinkChanged);
	Menu_init();
	Notification_init();

	// Load game for RetroAchievements tracking (must be after Notification_init)
	// Pass ROM data if available, otherwise just path (for cores that load from file)
	{
		char* rom_path_for_ra = game.tmp_path[0] ? game.tmp_path : game.path;
		RA_loadGame(rom_path_for_ra, game.data, game.size, core.tag);
	}

	State_resume();
	Menu_initState(); // make ready for state shortcuts

	PWR_disableAutosleep();
	// Wizard-launched netplay: start the link engine before the first frame.
	// On failure, exit via the normal quit route (quit=1 skips the main loop
	// and runs the full epilogue) instead of `goto finish` — the finish label
	// alone would skip Menu_quit/Notification_quit/QuitSettings/Video_cleanup,
	// all of which pair with initialization done above.
	if (NetplayBoot_startFromEnv(core.name) != 0) {
		Menu_message("Netplay connection failed.", (char*[]){"A", "OKAY", NULL});
		quit = 1;
	}

	// we dont need five second updates while ingame, and wifi status isnt displayed either
	PWR_updateFrequency(PWR_UPDATE_FREQ, 0);

	// force a vsync immediately before loop
	// for better frame pacing?
	GFX_clearAll();
	GFX_clearLayers(0);
	GFX_clear(screen);

	// need to draw real black background first otherwise u get weird pixels sometimes

	GFX_flip(screen);

	Special_init(); // after config

	chooseSyncRef();

	int has_pending_opt_change = 0;

	// then initialize custom  shaders from settings
	initShaders();
	Config_readOptions();
	applyShaderSettings();
	int rewind_initialized = Rewind_init(core.serialize_size ? core.serialize_size() : 0);
	rewind_init_ready = 1; // from here on Config_syncFrontend may re-init rewind
	if (rewind_initialized && core.serialize_size)
		Rewind_on_state_change();
	// release config when all is loaded
	Config_free();

	while (!quit) {
		GFX_startFrame();

		// Netplay: synchronize inputs BEFORE running the core. If we're still waiting
		// on the peer this frame, poll input (so menu/quit stay responsive) and skip it.
		if (!Netplay_update((uint16_t)Input_getButtons(), core.serialize_size, core.serialize, core.unserialize)) {
			input_poll_callback();
			continue;
		}

		GBALink_update();
		GBALink_pollAndDeliverPackets();
		GBLink_pollConnectionState(); // GB Link: detect connect/disconnect from the socket table

		if (Multiplayer_isActive()) {
			core.run(); // link/netplay drives timing; rewind & FF are disabled
		} else {
			run_frame();
		}
		if (Netplay_isActive()) {
			Netplay_postFrame();
		}

		// Process RetroAchievements for this frame
		RA_doFrame();

		// Update and render notifications overlay
		Notification_update(SDL_GetTicks());

		// Poll for volume/brightness/colortemp changes and show system indicators.
		// Guard the whole block on the setting so we skip three sysfs/msettings
		// reads every frame when the feature is off (it previously polled
		// unconditionally and only gated the notification).
		if (CFG_getNotifyAdjustments()) {
			static int last_volume = -1;
			static int last_brightness = -1;
			static int last_colortemp = -1;

			int cur_volume = GetVolume();
			int cur_brightness = GetBrightness();
			int cur_colortemp = GetColortemp();

			if (last_volume == -1) {
				// First frame - just initialize cached values, don't show indicator
				last_volume = cur_volume;
				last_brightness = cur_brightness;
				last_colortemp = cur_colortemp;
			} else {
				// Check for changes
				if (cur_volume != last_volume) {
					last_volume = cur_volume;
					Notification_showSystemIndicator(SYSTEM_INDICATOR_VOLUME);
				}
				if (cur_brightness != last_brightness) {
					last_brightness = cur_brightness;
					Notification_showSystemIndicator(SYSTEM_INDICATOR_BRIGHTNESS);
				}
				if (cur_colortemp != last_colortemp) {
					last_colortemp = cur_colortemp;
					Notification_showSystemIndicator(SYSTEM_INDICATOR_COLORTEMP);
				}
			}
		}

		Notification_renderToLayer(5); // Always call - handles cleanup when inactive

		if (has_pending_opt_change) {
			has_pending_opt_change = 0;
			if (Core_updateAVInfo()) {
				SND_resetAudio(core.sample_rate, core.fps);
				SetVolume(GetVolume());
			}
			chooseSyncRef();
		}

		if (show_menu) {
			if (Netplay_isConnected()) {
				Netplay_pause();
			}
			PWR_updateFrequency(PWR_UPDATE_FREQ, 1);
			Menu_loop();
			// Process RA async operations while menu is shown
			RA_idle();
			if (Netplay_isPaused()) {
				Netplay_resume();
			}
			PWR_updateFrequency(PWR_UPDATE_FREQ_INGAME, 0);
			has_pending_opt_change = config.core.changed;
			chooseSyncRef();

			// Clear FF/rewind state if multiplayer is now active
			if (Multiplayer_isActive()) {
				State_invalidateUndo();
				fast_forward = setFastForward(0);
				ff_toggled = 0;
				ff_hold_active = 0;
				rewind_toggle = 0;
				rewind_pressed = 0;
				rewinding = 0;
			}
		}

		AudioMgr_pollEvents(); // checks msettings for sink change, invokes callback
		Audio_checkAndResetIfNeeded();

		hdmimon();
	}
	int cw, ch;
	unsigned char* pixels = GFX_GL_screenCapture(&cw, &ch);

	renderer.dst = pixels;
	SDL_Surface* rawSurface = SDL_CreateRGBSurfaceWithFormatFrom(
		pixels, cw, ch, 32, cw * 4, SDL_PIXELFORMAT_ABGR8888);
	SDL_Surface* converted = SDL_ConvertSurfaceFormat(rawSurface, screen->format->format, 0);
	screen = converted;
	SDL_FreeSurface(rawSurface);
	free(pixels);
	GFX_animateSurfaceOpacity(converted, 0, 0, cw, ch, 255, 0, CFG_getMenuTransitions() ? 200 : 20, 1);
	SDL_FreeSurface(converted);

	Video_cleanup();

	PLAT_clearTurbo();

	Menu_quit();
	Notification_quit();
	QuitSettings();

finish:

	Netplay_quitAll();

	// Unload game and shutdown RetroAchievements before Core_quit
	RA_unloadGame();
	RA_quit();

	Game_close();
	State_freeUndo();
	Rewind_free();
	Core_unload();
	Core_quit();
	Core_close();
	Config_quit();
	Special_quit();
	MSG_quit();
	PWR_quit();
	VIB_quit();
	AudioMgr_quit();
	// Disabling this is a dumb hack for bluetooth, we should really be using
	// bluealsa with --keep-alive=-1 - but SDL wont reconnect the stream on next start.
	// Reenable as soon as we have a more recent SDL available, if ever.
	//SND_quit();
	PAD_quit();
	GFX_quit();
	SDL_WaitThread(screenshotsavethread, NULL);
	return EXIT_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////
// Accessor functions for external modules (netplay), prototypes in minarch.h
//////////////////////////////////////////////////////////////////////////////

// Screen/display accessors
SDL_Surface* minarch_getScreen(void) {
	return screen;
}
int minarch_getDeviceWidth(void) {
	return DEVICE_WIDTH;
}
int minarch_getDeviceHeight(void) {
	return DEVICE_HEIGHT;
}
// minarch_getMenuBitmap() is defined next to the menu state, which is file-static.

// Game state accessors
const char* minarch_getCoreTag(void) {
	return core.tag;
}
const char* minarch_getGameName(void) {
	return game.name;
}
void* minarch_getGameData(void) {
	return game.data;
}
size_t minarch_getGameSize(void) {
	return game.size;
}

// Core option accessors
char* minarch_getCoreOptionValue(const char* key) {
	return OptionList_getOptionValue(&config.core, key);
}
void minarch_setCoreOptionValue(const char* key, const char* value) {
	OptionList_setOptionValue(&config.core, key, value);
}

// Sleep state accessors
void minarch_beforeSleep(void) {
	Menu_beforeSleep();
}
void minarch_afterSleep(void) {
	Menu_afterSleep();
}

// Platform accessors
void minarch_hdmimon(void) {
	hdmimon();
}

// Menu accessors
int minarch_menuMessage(char* message, char** pairs) {
	return Menu_message(message, pairs);
}

// Save current config to file (used before core reset to preserve option changes)
void minarch_saveConfig(void) {
	Config_write(CONFIG_WRITE_ALL);
}

void minarch_beginOptionsBatch(void) {
	option_batch_mode = 1;
	option_batch_changed = 0;
}
void minarch_endOptionsBatch(void) {
	option_batch_mode = 0;
	if (option_batch_changed) {
		config.core.changed = 1;
		option_batch_changed = 0;
	}
}

// Force core to process option changes immediately (used by gblink.c and netplay_helper.c)
// Runs one frame with video output suppressed to trigger check_variables()
void minarch_forceCoreOptionUpdate(void) {
	skip_video_output = 1;
	core.run();
	skip_video_output = 0;
}

// Reload the game to reinitialize core state (e.g., for gpSP serial mode changes)
// Unloads and reloads the ROM so the core re-reads options during load_game()
void minarch_reloadGame(void) {
	SRAM_write();
	core.unload_game();

	struct retro_game_info game_info = {};
	game_info.path = game.tmp_path[0] ? game.tmp_path : game.path;
	game_info.data = game.data;
	game_info.size = game.size;
	if (!core.load_game(&game_info)) {
		LOG_error("core refused to reload game: %s\n", game_info.path);
		return;
	}

	SRAM_read();
	Core_updateAVInfo();
}
