#include "ma_internal.h"
#include "ma_environment.h"
#include "ma_game.h"
#include "ma_input.h"
#include "ma_config.h"
#include "ma_options.h"
#include "ma_video.h"
#include "ma_core.h"
#include "ra_integration.h"
#include "gbalink.h"

static bool set_rumble_state(unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
	(void)port; // single motor, port is not distinguishable
	// track the strong and weak effects separately and drive the one motor at
	// the stronger of the two — previously a weak-effect request drove the
	// motor at full strength (and clobbered the strong value)
	static uint16_t rumble_strong = 0;
	static uint16_t rumble_weak = 0;
	if (effect == RETRO_RUMBLE_STRONG)
		rumble_strong = strength;
	else if (effect == RETRO_RUMBLE_WEAK)
		rumble_weak = strength;
	VIB_setStrength(rumble_strong > rumble_weak ? rumble_strong : rumble_weak);
	return 1;
}
bool environment_callback(unsigned cmd, void* data) { // copied from picoarch initially

	switch (cmd) {
	// case RETRO_ENVIRONMENT_SET_ROTATION: { /* 1 */
	// 	break;
	// }
	case RETRO_ENVIRONMENT_GET_OVERSCAN: { /* 2 */
		bool* out = (bool*)data;
		if (out)
			*out = true;
		break;
	}
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: { /* 3 */
		bool* out = (bool*)data;
		if (out)
			*out = true;
		break;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE: { /* 6 */
		const struct retro_message* message = (const struct retro_message*)data;
		if (message)
			LOG_info("%s\n", message->msg);
		break;
	}
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: { /* 8 */
													// puts("RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL");
													// TODO: used by fceumm at least
		break;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: { /* 9 */
		const char** out = (const char**)data;
		if (out) {
			*out = core.bios_dir;
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: { /* 10 */
		const enum retro_pixel_format* format = (const enum retro_pixel_format*)data;
		// Check if the requested format is supported
		if (*format == RETRO_PIXEL_FORMAT_XRGB8888) {
			fmt = RETRO_PIXEL_FORMAT_XRGB8888;
			return true; // Indicate success
		} else if (*format == RETRO_PIXEL_FORMAT_RGB565) {
			fmt = RETRO_PIXEL_FORMAT_RGB565;
			return true; // Indicate success
		}
		fmt = RETRO_PIXEL_FORMAT_RGB565;
		return false; // Indicate failure
	}
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: { /* 11 */
		// puts("RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS\n");
		Input_init((const struct retro_input_descriptor*)data);
		return false;
		break;
	}
	case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: { /* 13 */
		const struct retro_disk_control_callback* var =
			(const struct retro_disk_control_callback*)data;

		if (var) {
			memset(&disk_control_ext, 0, sizeof(struct retro_disk_control_ext_callback));
			memcpy(&disk_control_ext, var, sizeof(struct retro_disk_control_callback));
		}
		break;
	}

	// TODO: this is called whether using variables or options
	case RETRO_ENVIRONMENT_GET_VARIABLE: { /* 15 */
		// puts("RETRO_ENVIRONMENT_GET_VARIABLE ");
		struct retro_variable* var = (struct retro_variable*)data;
		if (var && var->key) {
			var->value = OptionList_getOptionValue(&config.core, var->key);
			// printf("\t%s = \"%s\"\n", var->key, var->value);
		}
		// fflush(stdout);
		break;
	}
	// TODO: I think this is where the core reports its variables (the precursor to options)
	// TODO: this is called if RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION sets out to 0
	// TODO: not used by anything yet
	case RETRO_ENVIRONMENT_SET_VARIABLES: { /* 16 */
		// puts("RETRO_ENVIRONMENT_SET_VARIABLES");
		const struct retro_variable* vars = (const struct retro_variable*)data;
		if (vars) {
			OptionList_reset();
			OptionList_vars(vars);
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: { /* 18 */
		bool flag = *(bool*)data;
		break;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: { /* 17 */
		bool* out = (bool*)data;
		if (out) {
			*out = config.core.changed;
			config.core.changed = 0;
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: { /* 21 */
		break;
	}
	case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK: { /* 22 */
		break;
	}
	case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: { /* 23 */
		struct retro_rumble_interface* iface = (struct retro_rumble_interface*)data;
		iface->set_rumble_state = set_rumble_state;
		break;
	}
	case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: {
		unsigned* out = (unsigned*)data;
		if (out)
			*out = (1 << RETRO_DEVICE_JOYPAD) | (1 << RETRO_DEVICE_ANALOG);
		break;
	}
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: { /* 27 */
		struct retro_log_callback* log_cb = (struct retro_log_callback*)data;
		if (log_cb)
			log_cb->log = (void (*)(enum retro_log_level, const char*, ...))LOG_note; // same difference
		break;
	}
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: { /* 31 */
		const char** out = (const char**)data;
		if (out)
			*out = core.saves_dir; // save_dir;
		break;
	}
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: { /* 35 */
		const struct retro_controller_info* infos = (const struct retro_controller_info*)data;
		if (infos) {
			// TODO: store to gamepad_values/gamepad_labels for gamepad_device
			const struct retro_controller_info* info = &infos[0];
			for (int i = 0; i < info->num_types; i++) {
				const struct retro_controller_description* type = &info->types[i];
				if (exactMatch((char*)type->desc, "dualshock")) { // currently only enabled for PlayStation
					has_custom_controllers = 1;
					break;
				}
				// printf("\t%i: %s\n", type->id, type->desc);
			}
		}
		fflush(stdout);
		return false; // TODO: tmp
		break;
	}
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: { /* 36 | RETRO_ENVIRONMENT_EXPERIMENTAL */
		// Core is providing its memory map for achievement checking
		const struct retro_memory_map* mmap = (const struct retro_memory_map*)data;
		RA_setMemoryMap(mmap);
		break;
	}
	case RETRO_ENVIRONMENT_GET_LANGUAGE: { /* 39 */
		// puts("RETRO_ENVIRONMENT_GET_LANGUAGE");
		if (data)
			*(int*)data = RETRO_LANGUAGE_ENGLISH;
		break;
	}
	case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: { /* (40 | RETRO_ENVIRONMENT_EXPERIMENTAL) */
		// puts("RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER");
		break;
	}

	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: {
		// fixes fbneo save state graphics corruption
		// puts("RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE");
		int* out_p = (int*)data;
		if (out_p) {
			int out = 0;
			out |= RETRO_AV_ENABLE_VIDEO;
			out |= RETRO_AV_ENABLE_AUDIO;
			*out_p = out;
		}
		break;
	}

	// RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS (42 | RETRO_ENVIRONMENT_EXPERIMENTAL)
	// RETRO_ENVIRONMENT_GET_VFS_INTERFACE (45 | RETRO_ENVIRONMENT_EXPERIMENTAL)
	// RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE (47 | RETRO_ENVIRONMENT_EXPERIMENTAL)
	// RETRO_ENVIRONMENT_GET_INPUT_BITMASKS (51 | RETRO_ENVIRONMENT_EXPERIMENTAL)
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: { /* 51 | RETRO_ENVIRONMENT_EXPERIMENTAL */
		bool* out = (bool*)data;
		if (out)
			*out = true;
		break;
	}
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: { /* 52 */
		// puts("RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION");
		if (data)
			*(unsigned*)data = 2;
		break;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: { /* 53 */
		// puts("RETRO_ENVIRONMENT_SET_CORE_OPTIONS");
		if (data) {
			OptionList_reset();
			OptionList_init((const struct retro_core_option_definition*)data);
			Config_readOptions();
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: { /* 54 */
		// puts("RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL");
		const struct retro_core_options_intl* options = (const struct retro_core_options_intl*)data;
		if (options && options->us) {
			OptionList_reset();
			OptionList_init(options->us);
			Config_readOptions();
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: { /* 55 */
													   // puts("RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY");
		if (data) {
			const struct retro_core_option_display* display = (const struct retro_core_option_display*)data;
			OptionList_setOptionVisibility(&config.core, display->key, display->visible);
		}
		break;
	}
	case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION: { /* 57 */
		unsigned* out = (unsigned*)data;
		if (out)
			*out = 1;
		break;
	}
	case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE: { /* 58 */
		const struct retro_disk_control_ext_callback* var =
			(const struct retro_disk_control_ext_callback*)data;

		if (var) {
			memcpy(&disk_control_ext, var, sizeof(struct retro_disk_control_ext_callback));
		}
		break;
	}
	// TODO: RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION 59
	// TODO: used by mgba, (but only during frameskip?)
	// case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: { /* 62 */
	// 	const struct retro_audio_buffer_status_callback *cb = (const struct retro_audio_buffer_status_callback *)data;
	// 	if (cb) {
	// 		core.audio_buffer_status = cb->callback;
	// 	} else {
	// 		core.audio_buffer_status = NULL;
	// 	}
	// 	break;
	// }
	// TODO: used by mgba, (but only during frameskip?)
	// case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY: { /* 63 */
	//
	// 	const unsigned *latency_ms = (const unsigned *)data;
	// 	if (latency_ms) {
	// 		unsigned frames = *latency_ms * core.fps / 1000;
	// 		if (frames < 30)
	// 			// audio_buffer_size_override = frames;
	// 		else
	// 	}
	// 	break;
	// }

	// TODO: RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE 64
	case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: { /* 65 */
		// const struct retro_system_content_info_override* info = (const struct retro_system_content_info_override* )data;
		break;
	}
	// RETRO_ENVIRONMENT_GET_GAME_INFO_EXT 66
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: { /* 67 */
		// puts("RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2");
		if (data) {
			OptionList_reset();
			OptionList_v2_init((const struct retro_core_options_v2*)data);
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: { /* 68 */
		// puts("RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL");
		if (data) {
			const struct retro_core_options_v2_intl* intl = (const struct retro_core_options_v2_intl*)data;
			OptionList_reset();
			OptionList_v2_init(intl->us);
		}
		break;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK: { /* 69 */
		// puts("RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK");
		if (data) {
			struct retro_core_options_update_display_callback* update_display_cb = (struct retro_core_options_update_display_callback*)data;
			core.update_visibility_callback = update_display_cb->callback;
		} else {
			core.update_visibility_callback = NULL;
		}
		break;
	}
	// used by fceumm
	// TODO: used by gambatte for L/R palette switching (seems like it needs to return true even if data is NULL to indicate support)
	case RETRO_ENVIRONMENT_SET_VARIABLE: {
		// puts("RETRO_ENVIRONMENT_SET_VARIABLE");
		const struct retro_variable* var = (const struct retro_variable*)data;
		if (var && var->key) {
			// printf("\t%s = %s\n", var->key, var->value);
			OptionList_setOptionValue(&config.core, var->key, var->value);
			break;
		}

		int* out = (int*)data;
		if (out)
			*out = 1;

		break;
	}

	// unused
	// case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: {
	// 	puts("RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK"); fflush(stdout);
	// 	break;
	// }
	// case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: {
	// 	puts("RETRO_ENVIRONMENT_GET_THROTTLE_STATE"); fflush(stdout);
	// 	break;
	// }
	// case RETRO_ENVIRONMENT_GET_FASTFORWARDING: {
	// 	puts("RETRO_ENVIRONMENT_GET_FASTFORWARDING"); fflush(stdout);
	// 	break;
	// };
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		struct retro_hw_render_callback* cb = (struct retro_hw_render_callback*)data;

		// Fallback if version is 0.0 or other unexpected values
		if (cb->context_type == 4 && cb->version_major == 0 && cb->version_minor == 0) {
			cb->context_type = RETRO_HW_CONTEXT_OPENGLES3;
			cb->version_major = 3;
			cb->version_minor = 0;
		}

		return true;
	}
	case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE: {
		const struct retro_netpacket_callback* cb =
			(const struct retro_netpacket_callback*)data;
		if (cb) {
			core.has_netpacket = true;
			GBALink_setCoreCallbacks(cb);
		}
		return true;
	}
	default:
		// LOG_debug("Unsupported environment cmd: %u\n", cmd);
		return false;
	}
	return true;
}
