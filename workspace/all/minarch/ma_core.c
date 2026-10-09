#include "ma_internal.h"
#include "utils.h"
#include "ma_core.h"
#include "ma_cheats.h"
#include "ma_saves.h"
#include "ma_input.h"
#include "ma_video.h"
#include "ma_audio.h"
#include "ma_environment.h"
#include "ma_rewind.h"
#include "ma_hwrender.h"
#include "ma_avinfo.h"
#include "ma_emutime.h"
#include "ma_runframe.h"
#include "core_netplay.h"
#include "ma_menu.h"
#include <msettings.h>
#include <dlfcn.h>
#include <libgen.h>

static void Core_getName(char* in_name, char* out_name, size_t out_size) {
	snprintf(out_name, out_size, "%s", basename(in_name));
	char* tmp = strrchr(out_name, '_');
	if (tmp)
		tmp[0] = '\0';
}

// Every core frame goes through here so the input layer can tell whether the
// core polled input this frame. Some code paths never call the poll callback
// (FBNeo's romset error screen calls input_state_callback every frame but
// never input_poll_callback), which froze the pad -- including MENU -- and
// left the device stuck until a power cycle. input_state_callback polls
// lazily when this frame has not been polled yet.
static void (*core_run_real)(void);
static void core_run_wrapped(void) {
	HWR_beforeRun();
	Input_beginFrame();
	core_run_real();
}

// GPU cores may touch GL in any entry point, not only retro_run, and the
// in-game menu leaves SDL's renderer context current: make the game context
// current first. No-ops for software cores.
static void (*core_reset_real)(void);
static void core_reset_wrapped(void) {
	HWR_makeCurrent();
	core_reset_real();
}
static size_t (*core_serialize_size_real)(void);
static size_t core_serialize_size_wrapped(void) {
	HWR_makeCurrent(); // PPSSPP flushes its GL queue when it pauses its emu thread to measure
	return core_serialize_size_real();
}
static bool (*core_serialize_real)(void* data, size_t size);
static bool core_serialize_wrapped(void* data, size_t size) {
	HWR_makeCurrent();
	return core_serialize_real(data, size);
}
static bool (*core_unserialize_real)(const void* data, size_t size);
static bool core_unserialize_wrapped(const void* data, size_t size) {
	HWR_makeCurrent();
	return core_unserialize_real(data, size);
}
void Core_open(const char* core_path, const char* tag_name) {
	core.handle = dlopen(core_path, RTLD_LAZY);

	if (!core.handle) {
		// every dlsym below would return garbage and crash a few lines later;
		// exit cleanly so the launcher regains control
		LOG_error("dlopen failed for %s: %s\n", core_path, dlerror());
		exit(EXIT_FAILURE);
	}

	core.init = dlsym(core.handle, "retro_init");
	core.deinit = dlsym(core.handle, "retro_deinit");
	core.get_system_info = dlsym(core.handle, "retro_get_system_info");
	core.get_system_av_info = dlsym(core.handle, "retro_get_system_av_info");
	core.set_controller_port_device = dlsym(core.handle, "retro_set_controller_port_device");
	core_reset_real = dlsym(core.handle, "retro_reset");
	core.reset = core_reset_real ? core_reset_wrapped : NULL;
	core_run_real = dlsym(core.handle, "retro_run");
	core.run = core_run_wrapped;
	core_serialize_size_real = dlsym(core.handle, "retro_serialize_size");
	core.serialize_size = core_serialize_size_real ? core_serialize_size_wrapped : NULL;
	core_serialize_real = dlsym(core.handle, "retro_serialize");
	core.serialize = core_serialize_real ? core_serialize_wrapped : NULL;
	core_unserialize_real = dlsym(core.handle, "retro_unserialize");
	core.unserialize = core_unserialize_real ? core_unserialize_wrapped : NULL;
	core.cheat_reset = dlsym(core.handle, "retro_cheat_reset");
	core.cheat_set = dlsym(core.handle, "retro_cheat_set");
	core.load_game = dlsym(core.handle, "retro_load_game");
	core.load_game_special = dlsym(core.handle, "retro_load_game_special");
	core.unload_game = dlsym(core.handle, "retro_unload_game");
	core.get_region = dlsym(core.handle, "retro_get_region");
	core.get_memory_data = dlsym(core.handle, "retro_get_memory_data");
	core.get_memory_size = dlsym(core.handle, "retro_get_memory_size");

	void (*set_environment_callback)(retro_environment_t);
	void (*set_video_refresh_callback)(retro_video_refresh_t);
	void (*set_audio_sample_callback)(retro_audio_sample_t);
	void (*set_audio_sample_batch_callback)(retro_audio_sample_batch_t);
	void (*set_input_poll_callback)(retro_input_poll_t);
	void (*set_input_state_callback)(retro_input_state_t);

	set_environment_callback = dlsym(core.handle, "retro_set_environment");
	set_video_refresh_callback = dlsym(core.handle, "retro_set_video_refresh");
	set_audio_sample_callback = dlsym(core.handle, "retro_set_audio_sample");
	set_audio_sample_batch_callback = dlsym(core.handle, "retro_set_audio_sample_batch");
	set_input_poll_callback = dlsym(core.handle, "retro_set_input_poll");
	set_input_state_callback = dlsym(core.handle, "retro_set_input_state");

	struct retro_system_info info = {};
	core.get_system_info(&info);

	Core_getName((char*)core_path, (char*)core.name, sizeof(core.name));
	snprintf((char*)core.version, sizeof(core.version), "%s (%s)", info.library_name, info.library_version);
	snprintf((char*)core.tag, sizeof(core.tag), "%s", tag_name);
	// valid_extensions may legally be NULL (no-content cores)
	snprintf((char*)core.extensions, sizeof(core.extensions), "%s", info.valid_extensions ? info.valid_extensions : "");

	core.need_fullpath = info.need_fullpath;

	sprintf((char*)core.config_dir, "%s/%s-%s", USERDATA_PATH, core.tag, core.name);
	// A netplay client plays on a copy of the host's save that the pre-launch
	// wizard synced into an isolated dir (NETPLAY_SAVES_DIR, set by
	// netplay-prelaunch.sh); redirect SRAM and RTC there so the player's own
	// Saves/<tag> is never read or written. Save states go to a subdir of it
	// too (and the menu's .minui slot markers, see Menu_init): anything saved
	// while on the host's data -- during the session or after the host leaves
	// and the game plays on solo -- stays in /tmp, so the player's own states
	// and auto-resume never come back with host progress. The host, and every
	// non-netplay launch, use the real dirs.
	{
		const char* netplay_saves = getenv("NETPLAY_SAVES_DIR");
		if (netplay_saves && netplay_saves[0]) {
			snprintf((char*)core.saves_dir, sizeof(core.saves_dir), "%s", netplay_saves);
			snprintf((char*)core.states_dir, sizeof(core.states_dir), "%s/states", netplay_saves);
		} else {
			sprintf((char*)core.saves_dir, "%s/Saves/%s", SDCARD_PATH, core.tag);
			sprintf((char*)core.states_dir, "%s/%s-%s", SHARED_USERDATA_PATH, core.tag, core.name);
		}
	}
	// Same for the core's system files (flycast keeps its console flash and the
	// shared second memory card beside the BIOS): NETPLAY_SYSTEM_DIR is an
	// isolated copy holding the host's files and the BIOS the session uses.
	{
		const char* netplay_system = getenv("NETPLAY_SYSTEM_DIR");
		if (netplay_system && netplay_system[0])
			snprintf((char*)core.bios_dir, sizeof(core.bios_dir), "%s", netplay_system);
		else
			sprintf((char*)core.bios_dir, "%s/Bios/%s", SDCARD_PATH, core.tag);
	}
	sprintf((char*)core.cheats_dir, "%s/Cheats/%s", SDCARD_PATH, core.tag);
	sprintf((char*)core.overlays_dir, "%s/Overlays/%s", SDCARD_PATH, core.tag);

	mkdir_p(core.config_dir);
	mkdir_p(core.states_dir);
	mkdir_p(core.saves_dir);
	mkdir_p(core.bios_dir);
	mkdir_p(core.cheats_dir);

	set_environment_callback(environment_callback);
	set_video_refresh_callback(video_refresh_callback);
	set_audio_sample_callback(audio_sample_callback);
	set_audio_sample_batch_callback(audio_sample_batch_callback);
	set_input_poll_callback(input_poll_callback);
	set_input_state_callback(input_state_callback);
}
void Core_init(void) {
	core.init();
	core.initialized = 1;
}

void Core_applyCheats(struct Cheats* cheats) {
	if (!cheats)
		return;

	if (!core.cheat_reset || !core.cheat_set)
		return;

	core.cheat_reset();
	// Mirror RetroArch: hand the core only the enabled cheats, numbered with a
	// running counter rather than their position in the .cht file. PCSX-ReARMed
	// treats the index as a slot in its freshly cleared list and silently leaves
	// a cheat disabled when the slots below it are empty (#116). Cores that ignore
	// the enabled flag would activate anything they are given, so disabled
	// cheats must not be sent at all. Entries without a code are skipped so a
	// NULL is never passed to cores that dereference it unchecked.
	unsigned idx = 0;
	for (int i = 0; i < cheats->count; i++) {
		struct Cheat* cheat = &cheats->cheats[i];
		if (!cheat->enabled || !cheat->code)
			continue;
		core.cheat_set(idx++, true, cheat->code);
	}
}

int Core_updateAVInfo(void) {
	struct retro_system_av_info av_info = {};
	core.get_system_av_info(&av_info);

	double a = av_info.geometry.aspect_ratio;
	if (a <= 0)
		a = (double)av_info.geometry.base_width / av_info.geometry.base_height;

	int changed = (core.fps != av_info.timing.fps || core.sample_rate != av_info.timing.sample_rate || core.aspect_ratio != a);

	core.fps = av_info.timing.fps;
	core.sample_rate = av_info.timing.sample_rate;
	core.aspect_ratio = a;

	return changed;
}

// GPU cores report timing and aspect changes mid-game (SET_SYSTEM_AV_INFO /
// SET_GEOMETRY) from inside retro_run. Audio re-init and re-scaling cannot run
// there, so the environment callback stores them and the main loop applies
// them once retro_run has returned.
static struct {
	int has_av;
	int has_geometry;
	struct retro_system_av_info av;
	struct retro_game_geometry geometry;
} pending_av;

void Core_setPendingAVInfo(const struct retro_system_av_info* av) {
	pending_av.av = *av;
	pending_av.has_av = 1;
}
void Core_setPendingGeometry(const struct retro_game_geometry* geometry) {
	pending_av.geometry = *geometry;
	pending_av.has_geometry = 1;
}
void Core_applyPendingAV(void) {
	if (!pending_av.has_av && !pending_av.has_geometry)
		return;
	AVState cur = {core.fps, core.sample_rate, core.aspect_ratio};
	int changes = 0;
	if (pending_av.has_av)
		changes |= AVInfo_classifyTiming(&cur, &pending_av.av);
	if (pending_av.has_geometry)
		changes |= AVInfo_classifyGeometry(&cur, &pending_av.geometry);
	if (changes & AV_CHANGE_AUDIO) {
		core.fps = pending_av.av.timing.fps;
		core.sample_rate = pending_av.av.timing.sample_rate;
		SND_resetAudio(core.sample_rate, core.fps);
		SetVolume(GetVolume());
		chooseSyncRef();
	}
	if (changes & AV_CHANGE_ASPECT) {
		// SET_GEOMETRY is the more recent, narrower update when both arrived
		core.aspect_ratio = pending_av.has_geometry ? AVInfo_aspect(&pending_av.geometry)
													: AVInfo_aspect(&pending_av.av.geometry);
		renderer.dst_p = 0; // re-run the scaler on the next frame
	}
	if (changes)
		LOG_info("[AV] fps=%.3f rate=%.0f aspect=%.4f\n", core.fps, core.sample_rate, core.aspect_ratio);
	pending_av.has_av = 0;
	pending_av.has_geometry = 0;
}

void Core_load(void) {
	core.has_netpacket = false;

	struct retro_game_info game_info = {};
	game_info.path = game.tmp_path[0] ? game.tmp_path : game.path;
	game_info.data = game.data;
	game_info.size = game.size;
	if (!core.load_game(&game_info)) {
		// running a core with no loaded game is a guaranteed crash inside the core
		LOG_error("core refused to load game: %s\n", game_info.path);
		if (CoreNetplay_isActive()) // the core gave up waiting for the peer
			Menu_netplayNotice("Netplay failed", "Couldn't connect to the other player.", 3000);
		exit(EXIT_FAILURE);
	}
	{
		// GPU cores: the context must be live before the first retro_run and
		// before State_resume loads a state into the core's renderer.
		struct retro_system_av_info av = {0};
		core.get_system_av_info(&av);
		HWR_contextReset(av.geometry.max_width, av.geometry.max_height);
		if (HWR_contextFailed()) {
			// the core was promised a GL context it will never get: its GL
			// calls would fail and its frames are not pixel data
			LOG_error("GPU core context could not be started: %s\n", game_info.path);
			exit(EXIT_FAILURE);
		}
	}

	if (Cheats_load())
		Core_applyCheats(&cheatcodes);

	SRAM_read();
	RTC_read();
	// NOTE: must be called after core.load_game!
	// set a default, may update after loading configs. Port 1 too: a core that
	// leaves it unplugged (SwanStation) has no player 2 for netplay otherwise
	core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	core.set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
	Core_updateAVInfo();
}
void Core_reset(void) {
	core.reset();
	EmuTime_reset();
	// the undo snapshot belongs to the abandoned pre-reset session
	State_invalidateUndo();
	Rewind_on_state_change();
}
void Core_unload(void) {
	// Disabling this is a dumb hack for bluetooth, we should really be using
	// bluealsa with --keep-alive=-1 - but SDL wont reconnect the stream on next start.
	// Reenable as soon as we have a more recent SDL available, if ever.
	//SND_quit();
}
void Core_quit(void) {
	if (core.initialized) {
		SRAM_write();
		Cheats_free();
		RTC_write();
		HWR_makeCurrent();
		// Destroy the hw context BEFORE unloading, as RetroArch does: PPSSPP frees the
		// object its context_destroy callback dereferences in retro_unload_game, so the
		// reverse order crashed (SIGSEGV) on every quit.
		HWR_contextDestroy();
		core.unload_game();
		core.deinit();
		core.initialized = 0;
	}
}
void Core_close(void) {
	if (core.handle)
		dlclose(core.handle);
}
