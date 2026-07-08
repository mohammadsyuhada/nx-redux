#include "ma_internal.h"
#include "ma_config.h"
#include "ma_options.h"
#include "ma_video.h"
#include "ma_shaders.h"
#include "ma_rewind.h"
#include <glob.h>
#include <dirent.h>

char* onoff_labels[] = {
	"Off",
	"On",
	NULL};
char* scaling_labels[] = {
	"Native",
	"Aspect",
	"Aspect Screen",
	"Fullscreen",
	"Cropped",
	NULL};
static char* resample_labels[] = {
	"Low",
	"Medium",
	"High",
	"Max",
	NULL};
static char* rewind_enable_labels[] = {
	"Off",
	"On",
	NULL};
static char* rewind_buffer_labels[] = {
	"8",
	"16",
	"32",
	"64",
	"128",
	"256",
	NULL};
static char* rewind_granularity_values[] = {
	"16",
	"22",
	"25",
	"33",
	"50",
	"66",
	"100",
	"150",
	"200",
	"300",
	"450",
	"600",
	NULL};
static char* rewind_granularity_labels[] = {
	"16 ms (~60 fps)",
	"22 ms (~45 fps)",
	"25 ms (~40 fps)",
	"33 ms (~30 fps)",
	"50 ms (~20 fps)",
	"66 ms (~15 fps)",
	"100 ms (~10 fps)",
	"150 ms (~7 fps)",
	"200 ms (~5 fps)",
	"300 ms",
	"450 ms",
	"600 ms",
	NULL};
static char* rewind_compression_accel_values[] = {
	"1",
	"2",
	"4",
	"8",
	"12",
	NULL};
static char* rewind_compression_accel_labels[] = {
	"1 (best ratio)",
	"2 (default)",
	"4 (fast)",
	"8 (faster)",
	"12 (fastest)",
	NULL};
static char* ambient_labels[] = {
	"Off",
	"All",
	"Top",
	"FN",
	"LR",
	"Top/LR",
	NULL};

static char* effect_labels[] = {
	"None",
	"Line",
	"Grid",
	NULL};
static char* overlay_labels[] = {
	"None",
	NULL};
// static char* sharpness_labels[] = {
// 	"Sharp",
// 	"Crisp",
// 	"Soft",
// 	NULL
// };
static char* sharpness_labels[] = {
	"NEAREST",
	"LINEAR",
	NULL};
char* sync_ref_labels[] = {
	"Auto",
	"Screen",
	"Native",
	NULL};
static char* max_ff_labels[] = {
	"None",
	"2x",
	"3x",
	"4x",
	"5x",
	"6x",
	"7x",
	"8x",
	NULL,
};
static char* offset_labels[] = {
	"-64",
	"-63",
	"-62",
	"-61",
	"-60",
	"-59",
	"-58",
	"-57",
	"-56",
	"-55",
	"-54",
	"-53",
	"-52",
	"-51",
	"-50",
	"-49",
	"-48",
	"-47",
	"-46",
	"-45",
	"-44",
	"-43",
	"-42",
	"-41",
	"-40",
	"-39",
	"-38",
	"-37",
	"-36",
	"-35",
	"-34",
	"-33",
	"-32",
	"-31",
	"-30",
	"-29",
	"-28",
	"-27",
	"-26",
	"-25",
	"-24",
	"-23",
	"-22",
	"-21",
	"-20",
	"-19",
	"-18",
	"-17",
	"-16",
	"-15",
	"-14",
	"-13",
	"-12",
	"-11",
	"-10",
	"-9",
	"-8",
	"-7",
	"-6",
	"-5",
	"-4",
	"-3",
	"-2",
	"-1",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"14",
	"15",
	"16",
	"17",
	"18",
	"19",
	"20",
	"21",
	"22",
	"23",
	"24",
	"25",
	"26",
	"27",
	"28",
	"29",
	"30",
	"31",
	"32",
	"33",
	"34",
	"35",
	"36",
	"37",
	"38",
	"39",
	"40",
	"41",
	"42",
	"43",
	"44",
	"45",
	"46",
	"47",
	"48",
	"49",
	"50",
	"51",
	"52",
	"53",
	"54",
	"55",
	"56",
	"57",
	"58",
	"59",
	"60",
	"61",
	"62",
	"63",
	"64",
	NULL,
};
static char* nrofshaders_labels[] = {
	"off",
	"1",
	"2",
	"3",
	NULL};
static char* shupscale_labels[] = {
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"screen",
	NULL};
static char* shfilter_labels[] = {
	"NEAREST",
	"LINEAR",
	NULL};
static char* shscaletype_labels[] = {
	"source",
	"relative",
	NULL};

///////////////////////////////

// FE_OPT_*/SHORTCUT_*/SYNC_SRC_*/SH_* enums live in ma_internal.h

// LOCAL_BUTTON_COUNT/RETRO_BUTTON_COUNT and ButtonMapping live in ma_internal.h

ButtonMapping default_button_mapping[] = { // used if pak.cfg doesn't exist or doesn't have bindings
	{"Up", RETRO_DEVICE_ID_JOYPAD_UP, BTN_ID_DPAD_UP},
	{"Down", RETRO_DEVICE_ID_JOYPAD_DOWN, BTN_ID_DPAD_DOWN},
	{"Left", RETRO_DEVICE_ID_JOYPAD_LEFT, BTN_ID_DPAD_LEFT},
	{"Right", RETRO_DEVICE_ID_JOYPAD_RIGHT, BTN_ID_DPAD_RIGHT},
	{"A Button", RETRO_DEVICE_ID_JOYPAD_A, BTN_ID_A},
	{"B Button", RETRO_DEVICE_ID_JOYPAD_B, BTN_ID_B},
	{"X Button", RETRO_DEVICE_ID_JOYPAD_X, BTN_ID_X},
	{"Y Button", RETRO_DEVICE_ID_JOYPAD_Y, BTN_ID_Y},
	{"Start", RETRO_DEVICE_ID_JOYPAD_START, BTN_ID_START},
	{"Select", RETRO_DEVICE_ID_JOYPAD_SELECT, BTN_ID_SELECT},
	{"L1 Button", RETRO_DEVICE_ID_JOYPAD_L, BTN_ID_L1},
	{"R1 Button", RETRO_DEVICE_ID_JOYPAD_R, BTN_ID_R1},
	{"L2 Button", RETRO_DEVICE_ID_JOYPAD_L2, BTN_ID_L2},
	{"R2 Button", RETRO_DEVICE_ID_JOYPAD_R2, BTN_ID_R2},
	{"L3 Button", RETRO_DEVICE_ID_JOYPAD_L3, BTN_ID_L3},
	{"R3 Button", RETRO_DEVICE_ID_JOYPAD_R3, BTN_ID_R3},
	{NULL, 0, 0}};
ButtonMapping button_label_mapping[] = { // used to lookup the retro_id and local btn_id from button name
	{"NONE", -1, BTN_ID_NONE},
	{"UP", RETRO_DEVICE_ID_JOYPAD_UP, BTN_ID_DPAD_UP},
	{"DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN, BTN_ID_DPAD_DOWN},
	{"LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT, BTN_ID_DPAD_LEFT},
	{"RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT, BTN_ID_DPAD_RIGHT},
	{"A", RETRO_DEVICE_ID_JOYPAD_A, BTN_ID_A},
	{"B", RETRO_DEVICE_ID_JOYPAD_B, BTN_ID_B},
	{"X", RETRO_DEVICE_ID_JOYPAD_X, BTN_ID_X},
	{"Y", RETRO_DEVICE_ID_JOYPAD_Y, BTN_ID_Y},
	{"START", RETRO_DEVICE_ID_JOYPAD_START, BTN_ID_START},
	{"SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT, BTN_ID_SELECT},
	{"L1", RETRO_DEVICE_ID_JOYPAD_L, BTN_ID_L1},
	{"R1", RETRO_DEVICE_ID_JOYPAD_R, BTN_ID_R1},
	{"L2", RETRO_DEVICE_ID_JOYPAD_L2, BTN_ID_L2},
	{"R2", RETRO_DEVICE_ID_JOYPAD_R2, BTN_ID_R2},
	{"L3", RETRO_DEVICE_ID_JOYPAD_L3, BTN_ID_L3},
	{"R3", RETRO_DEVICE_ID_JOYPAD_R3, BTN_ID_R3},
	{NULL, 0, 0}};
ButtonMapping core_button_mapping[RETRO_BUTTON_COUNT + 1] = {0};

const char* device_button_names[LOCAL_BUTTON_COUNT] = {
	[BTN_ID_DPAD_UP] = "UP",
	[BTN_ID_DPAD_DOWN] = "DOWN",
	[BTN_ID_DPAD_LEFT] = "LEFT",
	[BTN_ID_DPAD_RIGHT] = "RIGHT",
	[BTN_ID_SELECT] = "SELECT",
	[BTN_ID_START] = "START",
	[BTN_ID_Y] = "Y",
	[BTN_ID_X] = "X",
	[BTN_ID_B] = "B",
	[BTN_ID_A] = "A",
	[BTN_ID_L1] = "L1",
	[BTN_ID_R1] = "R1",
	[BTN_ID_L2] = "L2",
	[BTN_ID_R2] = "R2",
	[BTN_ID_L3] = "L3",
	[BTN_ID_R3] = "R3",
};


// NOTE: these must be in BTN_ID_ order also off by 1 because of NONE (which is -1 in BTN_ID_ land)
char* button_labels[] = {
	"NONE", // displayed by default
	"UP",
	"DOWN",
	"LEFT",
	"RIGHT",
	"A",
	"B",
	"X",
	"Y",
	"START",
	"SELECT",
	"L1",
	"R1",
	"L2",
	"R2",
	"L3",
	"R3",
	"MENU+UP",
	"MENU+DOWN",
	"MENU+LEFT",
	"MENU+RIGHT",
	"MENU+A",
	"MENU+B",
	"MENU+X",
	"MENU+Y",
	"MENU+START",
	"MENU+SELECT",
	"MENU+L1",
	"MENU+R1",
	"MENU+L2",
	"MENU+R2",
	"MENU+L3",
	"MENU+R3",
	NULL,
};
static char* overclock_labels[] = {
	"Powersave",
	"Normal",
	"Performance",
	"Auto",
	NULL,
};

// TODO: this should be provided by the core
char* gamepad_labels[] = {
	"Standard",
	"DualShock",
	NULL,
};
char* gamepad_values[] = {
	"1",
	"517",
	NULL,
};

// CONFIG_NONE/CONFIG_CONSOLE/CONFIG_GAME live in ma_internal.h

char* getScreenScalingDesc(void) {
	if (GFX_supportsOverscan()) {
		return "Native uses integer scaling. Aspect uses core nreported aspect ratio.\nAspect screen uses screen aspect ratio\n Fullscreen has non-square\npixels. Cropped is integer scaled then cropped.";
	} else {
		return "Native uses integer scaling.\nAspect uses core reported aspect ratio.\nAspect screen uses screen aspect ratio\nFullscreen has non-square pixels.";
	}
}
int getScreenScalingCount(void) {
	return GFX_supportsOverscan() ? 5 : 4;
}


struct Config config = {
	.frontend = {// (OptionList)
				 .count = FE_OPT_COUNT,
				 .options = (Option[]){
					 [FE_OPT_SCALING] = {
						 .key = "minarch_screen_scaling",
						 .name = "Screen Scaling",
						 .desc = NULL, // will call getScreenScalingDesc()
						 .default_value = 1,
						 .value = 1,
						 .count = 3, // will call getScreenScalingCount()
						 .values = scaling_labels,
						 .labels = scaling_labels,
					 },
					 [FE_OPT_RESAMPLING] = {
						 .key = "minarch__resampling_quality",
						 .name = "Audio Resampling Quality",
						 .desc = "Resampling quality higher takes more CPU",
						 .default_value = 2,
						 .value = 2,
						 .count = 4,
						 .values = resample_labels,
						 .labels = resample_labels,
					 },
					 [FE_OPT_AMBIENT] = {
						 .key = "minarch_ambient",
						 .name = "Ambient Mode",
						 .desc = "Makes your leds follow on screen colors",
						 .default_value = 0,
						 .value = 0,
						 .count = 6,
						 .values = ambient_labels,
						 .labels = ambient_labels,
					 },
					 [FE_OPT_EFFECT] = {
						 .key = "minarch_screen_effect",
						 .name = "Screen Effect",
						 .desc = "Grid simulates an LCD grid.\nLine simulates CRT scanlines.\nEffects usually look best at native scaling.",
						 .default_value = 0,
						 .value = 0,
						 .count = 3,
						 .values = effect_labels,
						 .labels = effect_labels,
					 },
					 [FE_OPT_OVERLAY] = {
						 .key = "minarch_overlay",
						 .name = "Overlay",
						 .desc = "Choose a custom overlay png from the Overlays folder",
						 .default_value = 0,
						 .value = 0,
						 .count = 1,
						 .values = overlay_labels,
						 .labels = overlay_labels,
					 },
					 [FE_OPT_SCREENX] = {
						 .key = "minarch_screen_offsetx",
						 .name = "Offset screen X",
						 .desc = "Offset X pixels",
						 .default_value = 64,
						 .value = 64,
						 .count = 129,
						 .values = offset_labels,
						 .labels = offset_labels,
					 },
					 [FE_OPT_SCREENY] = {
						 .key = "minarch_screen_offsety",
						 .name = "Offset screen Y",
						 .desc = "Offset Y pixels",
						 .default_value = 64,
						 .value = 64,
						 .count = 129,
						 .values = offset_labels,
						 .labels = offset_labels,
					 },
					 [FE_OPT_SHARPNESS] = {
						 // 	.key	= "minarch_screen_sharpness",
						 .key = "minarch_scale_filter",
						 .name = "Screen Sharpness",
						 .desc = "LINEAR smooths lines, but works better when final image is at higher resolution, so either core that outputs higher resolution or upscaling with shaders",
						 .default_value = 1,
						 .value = 1,
						 .count = 3,
						 .values = sharpness_labels,
						 .labels = sharpness_labels,
					 },
					 [FE_OPT_SYNC_REFERENCE] = {
						 .key = "minarch_sync_reference",
						 .name = "Core Sync",
						 .desc = "Choose what should be used as a\nreference for the frame rate.\n\"Native\" uses the emulator frame rate,\n\"Screen\" uses the frame rate of the screen.",
						 .default_value = SYNC_SRC_AUTO,
						 .value = SYNC_SRC_AUTO,
						 .count = 3,
						 .values = sync_ref_labels,
						 .labels = sync_ref_labels,
					 },
					 [FE_OPT_OVERCLOCK] = {
						 .key = "minarch_cpu_speed",
						 .name = "CPU Speed",
						 .desc = "Over- or underclock the CPU to prioritize\npure performance or power savings.",
						 .default_value = 3,
						 .value = 3,
						 .count = 4,
						 .values = overclock_labels,
						 .labels = overclock_labels,
					 },
					 [FE_OPT_DEBUG] = {
						 .key = "minarch_debug_hud",
						 .name = "Debug HUD",
						 .desc = "Show frames per second, cpu load,\nresolution, and scaler information.",
						 .default_value = 0,
						 .value = 0,
						 .count = 2,
						 .values = onoff_labels,
						 .labels = onoff_labels,
					 },
					 [FE_OPT_MAXFF] = {
						 .key = "minarch_max_ff_speed",
						 .name = "Max FF Speed",
						 .desc = "Fast forward will not exceed the\nselected speed (but may be less\ndepending on game and emulator).",
						 .default_value = 3, // 4x
						 .value = 3,		 // 4x
						 .count = 8,
						 .values = max_ff_labels,
						 .labels = max_ff_labels,
					 },
					 [FE_OPT_FF_AUDIO] = {
						 .key = "minarch__ff_audio",
						 .name = "Fast forward audio",
						 .desc = "Play or mute audio when fast forwarding.",
						 .default_value = 0,
						 .value = 0,
						 .count = 2,
						 .values = onoff_labels,
						 .labels = onoff_labels,
					 },
					 [FE_OPT_REWIND_ENABLE] = {
						 .key = "minarch_rewind_enable",
						 .name = "Rewind",
						 .desc = "Enable in-memory rewind buffer.\nMust set a shortcut to access rewind during gameplay.\nUses extra CPU and memory.",
						 .default_value = MINARCH_DEFAULT_REWIND_ENABLE ? 1 : 0,
						 .value = MINARCH_DEFAULT_REWIND_ENABLE ? 1 : 0,
						 .count = 2,
						 .values = rewind_enable_labels,
						 .labels = rewind_enable_labels,
					 },
					 [FE_OPT_REWIND_BUFFER] = {
						 .key = "minarch_rewind_buffer_mb",
						 .name = "Rewind Buffer (MB)",
						 .desc = "Memory reserved for rewind snapshots.\nIncrease for longer rewind times.",
						 .default_value = 3, // 64MB
						 .value = 3,
						 .count = 6,
						 .values = rewind_buffer_labels,
						 .labels = rewind_buffer_labels,
					 },
					 [FE_OPT_REWIND_GRANULARITY] = {
						 .key = "minarch_rewind_granularity",
						 .name = "Rewind Interval",
						 .desc = "Interval between rewind snapshots.\nShorter intervals improve smoothness during rewind,\nbut increase CPU and memory usage.",
						 .default_value = 0, // 16ms
						 .value = 0,
						 .count = 12,
						 .values = rewind_granularity_values,
						 .labels = rewind_granularity_labels,
					 },
					 [FE_OPT_REWIND_COMPRESSION] = {
						 .key = "minarch_rewind_compression",
						 .name = "Rewind Compression",
						 .desc = "Compress rewind snapshots to save memory at the cost of CPU.",
						 .default_value = 1,
						 .value = 1,
						 .count = 2,
						 .values = onoff_labels,
						 .labels = onoff_labels,
					 },
					 [FE_OPT_REWIND_COMPRESSION_ACCEL] = {
						 .key = "minarch_rewind_compression_speed",
						 .name = "Rewind Compression Speed",
						 .desc = "LZ4 acceleration used for rewind snapshots.\nLower values compress more but use more CPU.",
						 .default_value = 1, // value 2
						 .value = 1,
						 .count = 5,
						 .values = rewind_compression_accel_values,
						 .labels = rewind_compression_accel_labels,
					 },
					 [FE_OPT_REWIND_AUDIO] = {
						 .key = "minarch_rewind_audio",
						 .name = "Rewind audio",
						 .desc = "Play or mute audio when rewinding.",
						 .default_value = MINARCH_DEFAULT_REWIND_AUDIO ? 1 : 0,
						 .value = MINARCH_DEFAULT_REWIND_AUDIO ? 1 : 0,
						 .count = 2,
						 .values = onoff_labels,
						 .labels = onoff_labels,
					 },
					 [FE_OPT_COUNT] = {NULL}}},
	.core = {
		// (OptionList)
		.count = 0,
		.options = (Option[]){
			{NULL},
		},
	},
	.shaders = {
		// (OptionList)
		.count = 18,
		.options = (Option[]){[SH_EXTRASETTINGS] = {
								  .key = "minarch_shaders_settings",
								  .name = "Optional Shaders Settings",
								  .desc = "If shaders have extra settings they will show up in this settings menu",
								  .default_value = 1,
								  .value = 1,
								  .count = 0,
								  .values = NULL,
								  .labels = NULL,
							  },
							  [SH_SHADERS_PRESET] = {
								  .key = "minarch_shaders_preset",
								  .name = "Shader / Emulator Settings Preset",
								  .desc = "Load a premade shaders/emulators config.\nTo try out a preset, exit the game without saving settings!",
								  .default_value = 1,
								  .value = 1,
								  .count = 0,
								  .values = NULL,
								  .labels = NULL,
							  },
							  [SH_NROFSHADERS] = {
								  .key = "minarch_nrofshaders",
								  .name = "Number of Shaders",
								  .desc = "Number of shaders 1 to 3",
								  .default_value = 0,
								  .value = 0,
								  .count = 4,
								  .values = nrofshaders_labels,
								  .labels = nrofshaders_labels,
							  },

							  [SH_SHADER1] = {
								  .key = "minarch_shader1",
								  .name = "Shader 1",
								  .desc = "Shader 1 program to run",
								  .default_value = 1,
								  .value = 1,
								  .count = 0,
								  .values = NULL,
								  .labels = NULL,
							  },
							  [SH_SHADER1_FILTER] = {
								  .key = "minarch_shader1_filter",
								  .name = "Shader 1 Filter",
								  .desc = "Method of upscaling, NEAREST or LINEAR",
								  .default_value = 1,
								  .value = 1,
								  .count = 2,
								  .values = shfilter_labels,
								  .labels = shfilter_labels,
							  },
							  [SH_SRCTYPE1] = {
								  .key = "minarch_shader1_srctype",
								  .name = "Shader 1 Source type",
								  .desc = "This will choose resolution source to scale from",
								  .default_value = 0,
								  .value = 0,
								  .count = 2,
								  .values = shscaletype_labels,
								  .labels = shscaletype_labels,
							  },
							  [SH_SCALETYPE1] = {
								  .key = "minarch_shader1_scaletype",
								  .name = "Shader 1 Texture Type",
								  .desc = "This will choose resolution source to scale from",
								  .default_value = 1,
								  .value = 1,
								  .count = 2,
								  .values = shscaletype_labels,
								  .labels = shscaletype_labels,
							  },
							  [SH_UPSCALE1] = {
								  .key = "minarch_shader1_upscale",
								  .name = "Shader 1 Scale",
								  .desc = "This will scale images x times,\nscreen scales to screens resolution (can hit performance)",
								  .default_value = 1,
								  .value = 1,
								  .count = 9,
								  .values = shupscale_labels,
								  .labels = shupscale_labels,
							  },
							  [SH_SHADER2] = {
								  .key = "minarch_shader2",
								  .name = "Shader 2",
								  .desc = "Shader 2 program to run",
								  .default_value = 0,
								  .value = 0,
								  .count = 0,
								  .values = NULL,
								  .labels = NULL,

							  },
							  [SH_SHADER2_FILTER] = {
								  .key = "minarch_shader2_filter",
								  .name = "Shader 2 Filter",
								  .desc = "Method of upscaling, NEAREST or LINEAR",
								  .default_value = 0,
								  .value = 0,
								  .count = 2,
								  .values = shfilter_labels,
								  .labels = shfilter_labels,
							  },
							  [SH_SRCTYPE2] = {
								  .key = "minarch_shader2_srctype",
								  .name = "Shader 2 Source type",
								  .desc = "This will choose resolution source to scale from",
								  .default_value = 0,
								  .value = 0,
								  .count = 2,
								  .values = shscaletype_labels,
								  .labels = shscaletype_labels,
							  },
							  [SH_SCALETYPE2] = {
								  .key = "minarch_shader2_scaletype",
								  .name = "Shader 2 Texture Type",
								  .desc = "This will choose resolution source to scale from",
								  .default_value = 1,
								  .value = 1,
								  .count = 2,
								  .values = shscaletype_labels,
								  .labels = shscaletype_labels,
							  },
							  [SH_UPSCALE2] = {
								  .key = "minarch_shader2_upscale",
								  .name = "Shader 2 Scale",
								  .desc = "This will scale images x times,\nscreen scales to screens resolution (can hit performance)",
								  .default_value = 0,
								  .value = 0,
								  .count = 9,
								  .values = shupscale_labels,
								  .labels = shupscale_labels,
							  },
							  [SH_SHADER3] = {
								  .key = "minarch_shader3",
								  .name = "Shader 3",
								  .desc = "Shader 3 program to run",
								  .default_value = 2,
								  .value = 2,
								  .count = 0,
								  .values = NULL,
								  .labels = NULL,

							  },
							  [SH_SHADER3_FILTER] = {
								  .key = "minarch_shader3_filter",
								  .name = "Shader 3 Filter",
								  .desc = "Method of upscaling, NEAREST or LINEAR",
								  .default_value = 0,
								  .value = 0,
								  .count = 2,
								  .values = shfilter_labels,
								  .labels = shfilter_labels,
							  },
							  [SH_SRCTYPE3] = {
								  .key = "minarch_shader3_srctype",
								  .name = "Shader 3 Source type",
								  .desc = "This will choose resolution source to scale from",
								  .default_value = 0,
								  .value = 0,
								  .count = 2,
								  .values = shscaletype_labels,
								  .labels = shscaletype_labels,
							  },
							  [SH_SCALETYPE3] = {
								  .key = "minarch_shader3_scaletype",
								  .name = "Shader 3 Texture Type",
								  .desc = "This will choose resolution source to scale from",
								  .default_value = 1,
								  .value = 1,
								  .count = 2,
								  .values = shscaletype_labels,
								  .labels = shscaletype_labels,
							  },
							  [SH_UPSCALE3] = {
								  .key = "minarch_shader3_upscale",
								  .name = "Shader 3 Scale",
								  .desc = "This will scale images x times,\nscreen scales to screens resolution (can hit performance)",
								  .default_value = 0,
								  .value = 0,
								  .count = 9,
								  .values = shupscale_labels,
								  .labels = shupscale_labels,
							  },
							  {NULL}},
	},
	.shaderpragmas = {{
		.count = 0,
		.options = NULL,
	}},
	.controls = default_button_mapping,
	.shortcuts = (ButtonMapping[]){[SHORTCUT_SAVE_STATE] = {"Save State", -1, BTN_ID_NONE, 0}, [SHORTCUT_LOAD_STATE] = {"Load State", -1, BTN_ID_NONE, 0}, [SHORTCUT_RESET_GAME] = {"Reset Game", -1, BTN_ID_NONE, 0}, [SHORTCUT_SAVE_QUIT] = {"Save & Quit", -1, BTN_ID_NONE, 0}, [SHORTCUT_CYCLE_SCALE] = {"Cycle Scaling", -1, BTN_ID_NONE, 0}, [SHORTCUT_CYCLE_EFFECT] = {"Cycle Effect", -1, BTN_ID_NONE, 0}, [SHORTCUT_TOGGLE_FF] = {"Toggle FF", -1, BTN_ID_NONE, 0}, [SHORTCUT_HOLD_FF] = {"Hold FF", -1, BTN_ID_NONE, 0}, [SHORTCUT_TOGGLE_REWIND] = {"Toggle Rewind", -1, BTN_ID_NONE, 0}, [SHORTCUT_HOLD_REWIND] = {"Hold Rewind", -1, BTN_ID_NONE, 0}, [SHORTCUT_GAMESWITCHER] = {"Game Switcher", -1, BTN_ID_NONE, 0}, [SHORTCUT_SCREENSHOT] = {"Screenshot", -1, BTN_ID_NONE, 0},
								   // Trimui only
								   [SHORTCUT_TOGGLE_TURBO_A] = {"Toggle Turbo A", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_B] = {"Toggle Turbo B", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_X] = {"Toggle Turbo X", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_Y] = {"Toggle Turbo Y", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_L] = {"Toggle Turbo L", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_L2] = {"Toggle Turbo L2", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_R] = {"Toggle Turbo R", -1, BTN_ID_NONE, 0},
								   [SHORTCUT_TOGGLE_TURBO_R2] = {"Toggle Turbo R2", -1, BTN_ID_NONE, 0},
								   // -----
								   {NULL}},
};
static int Config_getValue(char* cfg, const char* key, char* out_value, int* lock) { // gets value from string
	size_t key_len = strlen(key);
	char* tmp = cfg;
	while ((tmp = strstr(tmp, key))) {
		// only accept matches anchored to the start of a line (optionally
		// behind a `-` lock marker) and followed by " = " — a raw substring
		// hit can land inside a longer key or an unrelated value
		char* at = tmp;
		tmp += key_len;
		int locked = at > cfg && *(at - 1) == '-';
		char* line_start = locked ? at - 1 : at;
		if ((line_start == cfg || *(line_start - 1) == '\n') && !strncmp(tmp, " = ", 3)) {
			if (lock != NULL && locked)
				*lock = 1; // prefixed with a `-` means lock
			break; // matched
		}
	};
	if (!tmp)
		return 0;
	tmp += 3;

	strncpy(out_value, tmp, 256);
	out_value[256 - 1] = '\0';
	tmp = strchr(out_value, '\n');
	if (!tmp)
		tmp = strchr(out_value, '\r');
	if (tmp)
		*tmp = '\0';
	return 1;
}


void setOverclock(int i) {
	overclock = i;
	switch (i) {
	case 0:
		PWR_setCPUSpeed(CPU_SPEED_POWERSAVE);
		break;
	case 1:
		PWR_setCPUSpeed(CPU_SPEED_NORMAL);
		break;
	case 2:
		PWR_setCPUSpeed(CPU_SPEED_PERFORMANCE);
		break;
	case 3:
		PWR_setCPUSpeedAuto();
		break;
	}
}
void Config_syncFrontend(char* key, int value) {
	int i = -1;
	if (exactMatch(key, config.frontend.options[FE_OPT_SCALING].key)) {
		screen_scaling = value;

		renderer.dst_p = 0;
		i = FE_OPT_SCALING;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_RESAMPLING].key)) {
		resampling_quality = value;
		SND_setQuality(resampling_quality);
		i = FE_OPT_RESAMPLING;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_AMBIENT].key)) {
		ambient_mode = value;
		if (ambient_mode > 0)
			LEDS_pushProfileOverride(LIGHT_PROFILE_AMBIENT);
		else
			LEDS_popProfileOverride(LIGHT_PROFILE_AMBIENT);
		i = FE_OPT_AMBIENT;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_EFFECT].key)) {
		screen_effect = value;
		GFX_setEffect(value);
		renderer.dst_p = 0;
		i = FE_OPT_EFFECT;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_OVERLAY].key)) {
		char** overlayList = config.frontend.options[FE_OPT_OVERLAY].values;
		if (overlayList) {
			int count = 0;
			while (overlayList && overlayList[count])
				count++;
			if (value >= 0 && value < count) {
				GFX_setOverlay(overlayList[value], core.tag);
				overlay = value;
				renderer.dst_p = 0;
				i = FE_OPT_OVERLAY;
			}
		}
	} else if (exactMatch(key, config.frontend.options[FE_OPT_SCREENX].key)) {
		cfg_screenx = value;
		GFX_setOffsetX(value);
		i = FE_OPT_SCREENX;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_SCREENY].key)) {
		cfg_screeny = value;
		GFX_setOffsetY(value);
		i = FE_OPT_SCREENY;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_SHARPNESS].key)) {
		GFX_setSharpness(value);
		i = FE_OPT_SHARPNESS;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_SYNC_REFERENCE].key)) {
		sync_ref = value;
		i = FE_OPT_SYNC_REFERENCE;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_OVERCLOCK].key)) {
		overclock = value;
		i = FE_OPT_OVERCLOCK;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_DEBUG].key)) {
		show_debug = value;
		i = FE_OPT_DEBUG;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_MAXFF].key)) {
		max_ff_speed = value;
		i = FE_OPT_MAXFF;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_FF_AUDIO].key)) {
		ff_audio = value;
		i = FE_OPT_FF_AUDIO;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_REWIND_ENABLE].key)) {
		i = FE_OPT_REWIND_ENABLE;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_REWIND_BUFFER].key)) {
		i = FE_OPT_REWIND_BUFFER;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_REWIND_GRANULARITY].key)) {
		i = FE_OPT_REWIND_GRANULARITY;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_REWIND_AUDIO].key)) {
		i = FE_OPT_REWIND_AUDIO;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_REWIND_COMPRESSION].key)) {
		i = FE_OPT_REWIND_COMPRESSION;
	} else if (exactMatch(key, config.frontend.options[FE_OPT_REWIND_COMPRESSION_ACCEL].key)) {
		i = FE_OPT_REWIND_COMPRESSION_ACCEL;
	}
	if (i == -1)
		return;
	Option* option = &config.frontend.options[i];
	option->value = value;
	if (i == FE_OPT_REWIND_ENABLE || i == FE_OPT_REWIND_BUFFER || i == FE_OPT_REWIND_GRANULARITY || i == FE_OPT_REWIND_AUDIO || i == FE_OPT_REWIND_COMPRESSION || i == FE_OPT_REWIND_COMPRESSION_ACCEL) {
		const char* sval = option->values && option->values[value] ? option->values[value] : "0";
		int parsed = 0;
		if (i == FE_OPT_REWIND_ENABLE || i == FE_OPT_REWIND_AUDIO || i == FE_OPT_REWIND_COMPRESSION) {
			// use option index (Off/On)
			parsed = value;
		} else {
			parsed = strtol(sval, NULL, 10);
		}
		switch (i) {
		case FE_OPT_REWIND_ENABLE:
			rewind_cfg_enable = parsed;
			break;
		case FE_OPT_REWIND_BUFFER:
			rewind_cfg_buffer_mb = parsed;
			break;
		case FE_OPT_REWIND_GRANULARITY:
			rewind_cfg_granularity = parsed;
			break;
		case FE_OPT_REWIND_AUDIO:
			rewind_cfg_audio = parsed;
			break;
		case FE_OPT_REWIND_COMPRESSION:
			rewind_cfg_compress = parsed;
			break;
		case FE_OPT_REWIND_COMPRESSION_ACCEL:
			rewind_cfg_lz4_acceleration = parsed;
			break;
		}
		// Only call Rewind_init if core is initialized; early config reads happen before
		// the core is ready and will be followed by an explicit Rewind_init later
		if (core.initialized) {
			Rewind_init(core.serialize_size ? core.serialize_size() : 0);
		}
		if (i == FE_OPT_REWIND_ENABLE) {
			// ensure runtime toggles don't linger when enabling/disabling feature
			rewind_toggle = 0;
			rewind_pressed = 0;
			Rewind_sync_encode_state();
			rewinding = 0;
			ff_paused_by_rewind_hold = 0;
		}
	}
}

// ensure live gameplay immediately picks up scaler/effect changes triggered via shortcuts
static void apply_live_video_reset(void) {
	// defer work to the video thread: mark scaler dirty (shader reset not needed here)
	renderer.dst_p = 0;
	// If shaders are disabled (0 passes), force a reset so the default pipeline rebuilds
	if (config.shaders.options[SH_NROFSHADERS].value == 0) {
		GFX_resetShaders();
		shader_reset_suppressed = 0;
	} else {
		shader_reset_suppressed = 1; // skip reset for >0 shader pipelines
	}
}

char** list_files_in_folder(const char* folderPath, int* fileCount, const char* defaultElement, const char* extensionFilter) {
	*fileCount = 0; // callers read this even when we return NULL

	DIR* dir = opendir(folderPath);
	if (!dir) {
		perror("opendir");
		return NULL;
	}

	struct dirent* entry;
	struct stat fileStat;
	char** fileList = NULL;

	if (defaultElement) {
		fileList = malloc(sizeof(char*) * 2);
		fileList[0] = strdup(defaultElement);
		fileList[1] = NULL;
		(*fileCount)++;
	}

	while ((entry = readdir(dir)) != NULL) {
		// skip all entries starting with ._ (hidden files on macOS)
		if (entry->d_name[0] == '.' && entry->d_name[1] == '_')
			continue;
		// skip macOS .DS_Store files
		if (strcmp(entry->d_name, ".DS_Store") == 0)
			continue;

		char fullPath[1024];
		snprintf(fullPath, sizeof(fullPath), "%s/%s", folderPath, entry->d_name);

		if (stat(fullPath, &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
			if (extensionFilter) {
				const char* ext = strrchr(entry->d_name, '.');
				if (!ext || strcmp(ext, extensionFilter) != 0) {
					continue;
				}
			}

			char** temp = realloc(fileList, sizeof(char*) * (*fileCount + 1));
			if (!temp) {
				perror("realloc");
				for (int i = 0; i < *fileCount; ++i) {
					free(fileList[i]);
				}
				free(fileList);
				closedir(dir);
				return NULL;
			}
			fileList = temp;
			fileList[*fileCount] = strdup(entry->d_name);
			(*fileCount)++;
		}
	}

	closedir(dir);

	// Alphabetical sort
	for (int i = 0; i < *fileCount - 1; ++i) {
		for (int j = i + 1; j < *fileCount; ++j) {
			if (strcmp(fileList[i], fileList[j]) > 0) {
				char* temp = fileList[i];
				fileList[i] = fileList[j];
				fileList[j] = temp;
			}
		}
	}

	// NUll terminate the list
	char** temp = realloc(fileList, sizeof(char*) * (*fileCount + 1));
	if (!temp) {
		perror("realloc");
		for (int i = 0; i < *fileCount; ++i) {
			free(fileList[i]);
		}
		free(fileList);
		return NULL;
	}
	fileList = temp;
	fileList[*fileCount] = NULL;

	return fileList;
}

void free_file_list(char** list) {
	if (!list)
		return;
	for (int i = 0; list[i]; i++)
		free(list[i]);
	free(list);
}

// CONFIG_WRITE_ALL/CONFIG_WRITE_GAME live in ma_internal.h
void Config_getPath(char* filename, int override) {
	char device_tag[64] = {0};
	if (config.device_tag)
		sprintf(device_tag, "-%s", config.device_tag);
	if (override)
		sprintf(filename, "%s/%s%s.cfg", core.config_dir, game.alt_name, device_tag);
	else
		sprintf(filename, "%s/minarch%s.cfg", core.config_dir, device_tag);
}
void Config_init(void) {
	if (!config.default_cfg || config.initialized)
		return;

	char* tmp = config.default_cfg;
	char* tmp2;
	char* key;

	char button_name[128];
	char button_id[128];
	int i = 0;
	while (i < RETRO_BUTTON_COUNT && (tmp = strstr(tmp, "bind "))) {
		tmp += 5; // tmp now points to the button name (plus the rest of the line)
		key = tmp;
		tmp = strstr(tmp, " = ");
		if (!tmp)
			break;

		int len = tmp - key;
		if (len <= 0 || len >= (int)sizeof(button_name)) {
			tmp += 3;
			continue; // malformed or oversized bind name
		}
		strncpy(button_name, key, len);
		button_name[len] = '\0';

		tmp += 3;
		strncpy(button_id, tmp, sizeof(button_id) - 1);
		button_id[sizeof(button_id) - 1] = '\0';
		tmp2 = strchr(button_id, '\n');
		if (!tmp2)
			tmp2 = strchr(button_id, '\r');
		if (tmp2)
			*tmp2 = '\0';

		int retro_id = -1;
		int local_id = -1;

		tmp2 = strrchr(button_id, ':');
		int remap = 0;
		if (tmp2) {
			for (int j = 0; button_label_mapping[j].name; j++) {
				ButtonMapping* button = &button_label_mapping[j];
				if (!strcmp(tmp2 + 1, button->name)) {
					retro_id = button->retro;
					break;
				}
			}
			*tmp2 = '\0';
		}
		for (int j = 0; button_label_mapping[j].name; j++) {
			ButtonMapping* button = &button_label_mapping[j];
			if (!strcmp(button_id, button->name)) {
				local_id = button->local;
				if (retro_id == -1)
					retro_id = button->retro;
				break;
			}
		}

		tmp += strlen(button_id); // prepare to continue search

		// TODO: test this without a final line return
		tmp2 = calloc(strlen(button_name) + 1, sizeof(char));
		strcpy(tmp2, button_name);
		ButtonMapping* button = &core_button_mapping[i++];
		button->name = tmp2;
		button->retro = retro_id;
		button->local = local_id;
	};

	// populate shader presets
	// TODO: None option?
	int preset_filecount;
	char** preset_filelist = list_files_in_folder(SHADERS_FOLDER, &preset_filecount, NULL, ".cfg");
	config.shaders.options[SH_SHADERS_PRESET].values = preset_filelist;

	// populate shader options
	// TODO: None option?
	// TODO: Why do we do this twice? (see OptionShaders_openMenu)
	int filecount;
	char** filelist = list_files_in_folder(SHADERS_FOLDER "/glsl", &filecount, NULL, NULL);

	config.shaders.options[SH_SHADER1].values = filelist;
	config.shaders.options[SH_SHADER1].labels = filelist;
	config.shaders.options[SH_SHADER1].count = filecount;

	config.shaders.options[SH_SHADER2].values = filelist;
	config.shaders.options[SH_SHADER2].labels = filelist;
	config.shaders.options[SH_SHADER2].count = filecount;

	config.shaders.options[SH_SHADER3].values = filelist;
	config.shaders.options[SH_SHADER3].labels = filelist;
	config.shaders.options[SH_SHADER3].count = filecount;

	char overlaypath[MAX_PATH];
	snprintf(overlaypath, sizeof(overlaypath), "%s/%s", OVERLAYS_FOLDER, core.tag);
	char** overlaylist = list_files_in_folder(overlaypath, &filecount, "None", NULL);

	if (overlaylist) {
		config.frontend.options[FE_OPT_OVERLAY].labels = overlaylist;
		config.frontend.options[FE_OPT_OVERLAY].values = overlaylist;
		config.frontend.options[FE_OPT_OVERLAY].count = filecount;
	}
	config.initialized = 1;
}
void Config_quit(void) {
	if (!config.initialized)
		return;
	for (int i = 0; core_button_mapping[i].name; i++) {
		free(core_button_mapping[i].name);
	}
}
void Config_readOptionsString(char* cfg) {
	if (!cfg)
		return;

	char key[256];
	char value[256];
	for (int i = 0; config.frontend.options[i].key; i++) {
		Option* option = &config.frontend.options[i];
		if (!Config_getValue(cfg, option->key, value, &option->lock))
			continue;
		OptionList_setOptionValue(&config.frontend, option->key, value);
		Config_syncFrontend(option->key, option->value);
	}

	if (has_custom_controllers && Config_getValue(cfg, "minarch_gamepad_type", value, NULL)) {
		gamepad_type = strtol(value, NULL, 0);
		int device = strtol(gamepad_values[gamepad_type], NULL, 0);
		core.set_controller_port_device(0, device);
	}
	for (int i = 0; config.core.options[i].key; i++) {
		Option* option = &config.core.options[i];
		if (!Config_getValue(cfg, option->key, value, &option->lock))
			continue;
		OptionList_setOptionValue(&config.core, option->key, value);
	}
	for (int i = 0; config.shaders.options[i].key; i++) {
		Option* option = &config.shaders.options[i];
		if (!Config_getValue(cfg, option->key, value, &option->lock))
			continue;
		OptionList_setOptionValue(&config.shaders, option->key, value);
	}
	for (int y = 0; y < config.shaders.options[SH_NROFSHADERS].value; y++) {
		if (config.shaderpragmas[y].count > 0) {
			for (int i = 0; config.shaderpragmas[y].options[i].key; i++) {
				Option* option = &config.shaderpragmas[y].options[i];
				if (!Config_getValue(cfg, option->key, value, &option->lock))
					continue;
				OptionList_setOptionValue(&config.shaderpragmas[y], option->key, value);
			}
		}
	}
}
static void Config_readControlsString(char* cfg) {
	if (!cfg)
		return;

	char key[256];
	char value[256];
	char* tmp;
	for (int i = 0; config.controls[i].name; i++) {
		ButtonMapping* mapping = &config.controls[i];
		sprintf(key, "bind %s", mapping->name);
		sprintf(value, "NONE");

		if (!Config_getValue(cfg, key, value, NULL))
			continue;
		if ((tmp = strrchr(value, ':')))
			*tmp = '\0'; // this is a binding artifact in default.cfg, ignore

		int id = -1;
		for (int j = 0; button_labels[j]; j++) {
			if (!strcmp(button_labels[j], value)) {
				id = j - 1;
				break;
			}
		}

		int mod = 0;
		if (id >= LOCAL_BUTTON_COUNT) {
			id -= LOCAL_BUTTON_COUNT;
			mod = 1;
		}

		mapping->local = id;
		mapping->mod = mod;
	}

	for (int i = 0; config.shortcuts[i].name; i++) {
		ButtonMapping* mapping = &config.shortcuts[i];
		sprintf(key, "bind %s", mapping->name);
		sprintf(value, "NONE");

		if (!Config_getValue(cfg, key, value, NULL))
			continue;

		int id = -1;
		for (int j = 0; button_labels[j]; j++) {
			if (!strcmp(button_labels[j], value)) {
				id = j - 1;
				break;
			}
		}

		int mod = 0;
		if (id >= LOCAL_BUTTON_COUNT) {
			id -= LOCAL_BUTTON_COUNT;
			mod = 1;
		}

		mapping->local = id;
		mapping->mod = mod;
	}
}
void Config_load(void) {
	config.device_tag = getenv("DEVICE");

	// update for crop overscan support
	Option* scaling_option = &config.frontend.options[FE_OPT_SCALING];
	scaling_option->desc = getScreenScalingDesc();
	scaling_option->count = getScreenScalingCount();
	if (!GFX_supportsOverscan()) {
		scaling_labels[4] = NULL;
	}

	char* system_path = SYSTEM_PATH "/system.cfg";

	char device_system_path[MAX_PATH] = {0};
	if (config.device_tag)
		sprintf(device_system_path, SYSTEM_PATH "/system-%s.cfg", config.device_tag);

	if (config.device_tag && exists(device_system_path)) {
		config.system_cfg = allocFile(device_system_path);
	} else if (exists(system_path))
		config.system_cfg = allocFile(system_path);
	else
		config.system_cfg = NULL;

	char default_path[MAX_PATH];
	getEmuPath((char*)core.tag, default_path);
	char* tmp = strrchr(default_path, '/');
	strcpy(tmp, "/default.cfg");

	char device_default_path[MAX_PATH] = {0};
	if (config.device_tag) {
		getEmuPath((char*)core.tag, device_default_path);
		tmp = strrchr(device_default_path, '/');
		char filename[64];
		sprintf(filename, "/default-%s.cfg", config.device_tag);
		strcpy(tmp, filename);
	}

	if (config.device_tag && exists(device_default_path)) {
		config.default_cfg = allocFile(device_default_path);
	} else if (exists(default_path))
		config.default_cfg = allocFile(default_path);
	else
		config.default_cfg = NULL;

	char path[MAX_PATH];
	config.loaded = CONFIG_NONE;
	int override = 0;
	Config_getPath(path, CONFIG_WRITE_GAME);
	if (exists(path))
		override = 1;
	if (!override)
		Config_getPath(path, CONFIG_WRITE_ALL);

	config.user_cfg = allocFile(path);
	if (!config.user_cfg)
		return;

	config.loaded = override ? CONFIG_GAME : CONFIG_CONSOLE;
}
void Config_free(void) {
	// NULL the pointers: cores can re-register options after startup, which
	// re-runs Config_readOptions over these buffers
	if (config.system_cfg)
		free(config.system_cfg);
	config.system_cfg = NULL;
	if (config.default_cfg)
		free(config.default_cfg);
	config.default_cfg = NULL;
	if (config.user_cfg)
		free(config.user_cfg);
	config.user_cfg = NULL;
}
void Config_readOptions(void) {
	Config_readOptionsString(config.system_cfg);
	Config_readOptionsString(config.default_cfg);
	Config_readOptionsString(config.user_cfg);
}
void Config_readControls(void) {
	Config_readControlsString(config.default_cfg);
	Config_readControlsString(config.user_cfg);
}
void Config_write(int override) {
	char path[MAX_PATH];
	// sprintf(path, "%s/%s.cfg", core.config_dir, game.alt_name);
	Config_getPath(path, CONFIG_WRITE_GAME);

	if (!override) {
		if (config.loaded == CONFIG_GAME)
			unlink(path);
		Config_getPath(path, CONFIG_WRITE_ALL);
	}
	config.loaded = override ? CONFIG_GAME : CONFIG_CONSOLE;

	FILE* file = fopen(path, "wb");
	if (!file)
		return;

	for (int i = 0; config.frontend.options[i].key; i++) {
		Option* option = &config.frontend.options[i];
		int count = 0;
		while (option->values && option->values[count])
			count++;
		if (option->value >= 0 && option->value < count) {
			fprintf(file, "%s = %s\n", option->key, option->values[option->value]);
		}
	}
	for (int i = 0; config.core.options[i].key; i++) {
		Option* option = &config.core.options[i];
		int count = 0;
		while (option->values && option->values[count])
			count++;
		if (option->value >= 0 && option->value < count) {
			fprintf(file, "%s = %s\n", option->key, option->values[option->value]);
		}
	}
	for (int i = 0; config.shaders.options[i].key; i++) {
		Option* option = &config.shaders.options[i];
		int count = 0;
		while (option->values && option->values[count])
			count++;
		if (option->value >= 0 && option->value < count) {
			fprintf(file, "%s = %s\n", option->key, option->values[option->value]);
		}
	}
	for (int y = 0; y < config.shaders.options[SH_NROFSHADERS].value; y++) {
		for (int i = 0; config.shaderpragmas[y].options[i].key; i++) {
			Option* option = &config.shaderpragmas[y].options[i];
			int count = 0;
			while (option->values && option->values[count])
				count++;
			if (option->value >= 0 && option->value < count) {
				fprintf(file, "%s = %s\n", option->key, option->values[option->value]);
			}
		}
	}

	if (has_custom_controllers)
		fprintf(file, "%s = %i\n", "minarch_gamepad_type", gamepad_type);

	for (int i = 0; config.controls[i].name; i++) {
		ButtonMapping* mapping = &config.controls[i];
		int j = mapping->local + 1;
		if (mapping->mod)
			j += LOCAL_BUTTON_COUNT;
		fprintf(file, "bind %s = %s\n", mapping->name, button_labels[j]);
	}
	for (int i = 0; config.shortcuts[i].name; i++) {
		ButtonMapping* mapping = &config.shortcuts[i];
		int j = mapping->local + 1;
		if (mapping->mod)
			j += LOCAL_BUTTON_COUNT;
		fprintf(file, "bind %s = %s\n", mapping->name, button_labels[j]);
	}

	fclose(file);
	sync();
}
void Config_restore(void) {
	char path[MAX_PATH];
	if (config.loaded == CONFIG_GAME) {
		if (config.device_tag)
			sprintf(path, "%s/%s-%s.cfg", core.config_dir, game.alt_name, config.device_tag);
		else
			sprintf(path, "%s/%s.cfg", core.config_dir, game.alt_name);
		unlink(path);
	} else if (config.loaded == CONFIG_CONSOLE) {
		if (config.device_tag)
			sprintf(path, "%s/minarch-%s.cfg", core.config_dir, config.device_tag);
		else
			sprintf(path, "%s/minarch.cfg", core.config_dir);
		unlink(path);
	}
	config.loaded = CONFIG_NONE;

	for (int i = 0; config.frontend.options[i].key; i++) {
		Option* option = &config.frontend.options[i];
		option->value = option->default_value;
		Config_syncFrontend(option->key, option->value);
	}
	for (int i = 0; config.core.options[i].key; i++) {
		Option* option = &config.core.options[i];
		option->value = option->default_value;
	}
	for (int i = 0; config.shaders.options[i].key; i++) {
		Option* option = &config.shaders.options[i];
		option->value = option->default_value;
	}
	config.core.changed = 1; // let the core know

	if (has_custom_controllers) {
		gamepad_type = 0;
		core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	}

	for (int i = 0; config.controls[i].name; i++) {
		ButtonMapping* mapping = &config.controls[i];
		mapping->local = mapping->default_;
		mapping->mod = 0;
	}
	for (int i = 0; config.shortcuts[i].name; i++) {
		ButtonMapping* mapping = &config.shortcuts[i];
		mapping->local = BTN_ID_NONE;
		mapping->mod = 0;
	}

	Config_load();
	Config_readOptions();
	Config_readControls();
	Config_free();

	renderer.dst_p = 0;
}
