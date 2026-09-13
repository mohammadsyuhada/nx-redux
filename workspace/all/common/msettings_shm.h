#ifndef MSETTINGS_SHM_H
#define MSETTINGS_SHM_H

// Bare mirror of libmsettings' private Settings struct, for tools that read
// the shared-memory block or the persisted msettings.bin WITHOUT linking
// libmsettings (osdctl: cheap gets from OSD update scripts; poweroff_next:
// runs after every app is dead). Field order must match the platform's
// libmsettings/msettings.c Settings typedef, and MSETTINGS_SHM_VERSION its
// SETTINGS_VERSION. HAS_FAN selects the tg5050 layout (osdctl and
// poweroff_next Makefiles define it for that platform).
typedef struct {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int fn_mode;
	int contrast;
	int saturation;
	int exposure;
	int fn_brightness;
	int fn_colortemperature;
	int fn_contrast;
	int fn_saturation;
	int fn_exposure;
	int fn_volume;
#ifndef HAS_FAN
	int fn_dpad_disabled;
	int fn_dpad_joystick;
#endif
	int turbo_a;
	int turbo_b;
	int turbo_x;
	int turbo_y;
	int turbo_l1;
	int turbo_l2;
	int turbo_r1;
	int turbo_r2;
	int rumble_off;		 // OSD motor switch; inverted so old files read as on
	int rumble_strength; // 0 Normal (default), 1 Light, 2 Strong (vib_levels.h)
	int game_volume;
	int music_volume;
	int speaker_mute; // OSD output mute, silences output only, independent of the FN switch
	int jack;
	int audiosink;
#ifdef HAS_FAN
	int fanSpeed;
#endif
} SettingsShm;

#ifdef HAS_FAN
#define MSETTINGS_SHM_VERSION 3
#else
#define MSETTINGS_SHM_VERSION 12
#endif

#define MSETTINGS_SHM_KEY "/SharedSettings"
#define MSETTINGS_DEFAULT_FN_NO_CHANGE -69

#endif
