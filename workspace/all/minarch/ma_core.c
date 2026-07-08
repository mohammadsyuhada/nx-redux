#include "ma_internal.h"
#include "ma_core.h"
#include "ma_cheats.h"
#include "ma_saves.h"
#include "ma_options.h"
#include "ma_input.h"
#include "ma_video.h"
#include "ma_audio.h"
#include "ma_environment.h"
#include "ma_rewind.h"
#include "netplay_helper.h"
#include <dlfcn.h>
#include <libgen.h>

void Core_getName(char* in_name, char* out_name) {
	strcpy(out_name, basename(in_name));
	char* tmp = strrchr(out_name, '_');
	if (tmp)
		tmp[0] = '\0';
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
	core.reset = dlsym(core.handle, "retro_reset");
	core.run = dlsym(core.handle, "retro_run");
	core.serialize_size = dlsym(core.handle, "retro_serialize_size");
	core.serialize = dlsym(core.handle, "retro_serialize");
	core.unserialize = dlsym(core.handle, "retro_unserialize");
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

	Core_getName((char*)core_path, (char*)core.name);
	snprintf((char*)core.version, sizeof(core.version), "%s (%s)", info.library_name, info.library_version);
	strcpy((char*)core.tag, tag_name);
	// valid_extensions may legally be NULL (no-content cores)
	snprintf((char*)core.extensions, sizeof(core.extensions), "%s", info.valid_extensions ? info.valid_extensions : "");

	core.need_fullpath = info.need_fullpath;

	sprintf((char*)core.config_dir, USERDATA_PATH "/%s-%s", core.tag, core.name);
	sprintf((char*)core.states_dir, SHARED_USERDATA_PATH "/%s-%s", core.tag, core.name);
	sprintf((char*)core.saves_dir, SDCARD_PATH "/Saves/%s", core.tag);
	sprintf((char*)core.bios_dir, SDCARD_PATH "/Bios/%s", core.tag);
	sprintf((char*)core.cheats_dir, SDCARD_PATH "/Cheats/%s", core.tag);
	sprintf((char*)core.overlays_dir, SDCARD_PATH "/Overlays/%s", core.tag);

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
	for (int i = 0; i < cheats->count; i++) {
		if (cheats->cheats[i].enabled) {
			core.cheat_set(i, cheats->cheats[i].enabled, cheats->cheats[i].code);
		}
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

void Core_load(void) {
	core.has_netpacket = false;
	core.has_gblink = false;
	core.show_netplay = false;

	struct retro_game_info game_info = {};
	game_info.path = game.tmp_path[0] ? game.tmp_path : game.path;
	game_info.data = game.data;
	game_info.size = game.size;
	if (!core.load_game(&game_info)) {
		// running a core with no loaded game is a guaranteed crash inside the core
		LOG_error("core refused to load game: %s\n", game_info.path);
		exit(EXIT_FAILURE);
	}

	CoreLinkSupport link_support = checkCoreLinkSupport(core.name);
	core.show_netplay = link_support.show_netplay;
	core.has_netpacket = link_support.has_netpacket;
	core.has_gblink = link_support.has_gblink;

	if (Cheats_load())
		Core_applyCheats(&cheatcodes);

	SRAM_read();
	RTC_read();
	// NOTE: must be called after core.load_game!
	core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD); // set a default, may update after loading configs
	Core_updateAVInfo();
}
void Core_reset(void) {
	core.reset();
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
		core.unload_game();
		core.deinit();
		core.initialized = 0;
	}
}
void Core_close(void) {
	if (core.handle)
		dlclose(core.handle);
}
