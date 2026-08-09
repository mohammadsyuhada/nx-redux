#include "settings.h"
#include "defines.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Settings file path (in shared userdata directory)
#define SETTINGS_FILE SHARED_USERDATA_PATH "/music-player/settings.cfg"
#define SETTINGS_DIR SHARED_USERDATA_PATH "/music-player"

// Valid screen off timeout values (in seconds)
// 0 means off (no auto screen off)
static const int screen_off_values[] = {60, 90, 120, 0};
#define SCREEN_OFF_VALUE_COUNT 4
#define DEFAULT_SCREEN_OFF_INDEX 0 // Default to 60s

// Bass filter (high-pass cutoff Hz, 0 = off)
static const int bass_filter_values[] = {0, 80, 100, 120, 150, 200};
#define BASS_FILTER_VALUE_COUNT 6
#define DEFAULT_BASS_FILTER_INDEX 3 // 120 Hz

// Soft limiter (0=off, 1=mild, 2=medium, 3=strong)
static const float soft_limiter_thresholds[] = {0.0f, 0.7f, 0.6f, 0.5f};
#define SOFT_LIMITER_VALUE_COUNT 4
#define DEFAULT_SOFT_LIMITER_INDEX 2 // Medium (0.6)

// Audio buffer size in frames
static const int buffer_frames_values[] = {1024, 2048, 4096};
#define BUFFER_FRAMES_VALUE_COUNT 3
#define DEFAULT_BUFFER_FRAMES_INDEX 1 // 2048

#define RESAMPLER_QUALITY_COUNT 3 // 0=Fast, 1=Medium, 2=Best
#define DEFAULT_RESAMPLER_QUALITY 0

// Current settings
static struct {
	int screen_off_timeout; // seconds, 0 = off
	bool lyrics_enabled;	// true = show lyrics
	int bass_filter_hz;		// 0=off, 80, 100, 120, 150, 200
	int soft_limiter_index; // 0=off, 1=mild, 2=medium, 3=strong
	int rate_mode_follow;	// 0=device default, 1=follow source
	int resampler_quality;	// 0=Fast, 1=Medium, 2=Best
	int buffer_frames;		// 1024, 2048, 4096
} current_settings;

// Find index of current screen off value in the values array
static int get_screen_off_index(void) {
	for (int i = 0; i < SCREEN_OFF_VALUE_COUNT; i++) {
		if (screen_off_values[i] == current_settings.screen_off_timeout) {
			return i;
		}
	}
	return DEFAULT_SCREEN_OFF_INDEX;
}

// Find index of current bass filter value
static int get_bass_filter_index(void) {
	for (int i = 0; i < BASS_FILTER_VALUE_COUNT; i++) {
		if (bass_filter_values[i] == current_settings.bass_filter_hz) {
			return i;
		}
	}
	return DEFAULT_BASS_FILTER_INDEX;
}

// Find index of current buffer frames value
static int get_buffer_frames_index(void) {
	for (int i = 0; i < BUFFER_FRAMES_VALUE_COUNT; i++) {
		if (buffer_frames_values[i] == current_settings.buffer_frames) {
			return i;
		}
	}
	return DEFAULT_BUFFER_FRAMES_INDEX;
}

void Settings_init(void) {
	// Set defaults
	current_settings.screen_off_timeout = screen_off_values[DEFAULT_SCREEN_OFF_INDEX];
	current_settings.lyrics_enabled = true;
	current_settings.bass_filter_hz = bass_filter_values[DEFAULT_BASS_FILTER_INDEX];
	current_settings.soft_limiter_index = DEFAULT_SOFT_LIMITER_INDEX;
	current_settings.rate_mode_follow = 0;
	current_settings.resampler_quality = DEFAULT_RESAMPLER_QUALITY;
	current_settings.buffer_frames = buffer_frames_values[DEFAULT_BUFFER_FRAMES_INDEX];

	// Try to load from file
	FILE* f = fopen(SETTINGS_FILE, "r");
	if (!f)
		return;

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		int value;
		if (sscanf(line, "screen_off_timeout=%d", &value) == 1) {
			// Validate the value
			for (int i = 0; i < SCREEN_OFF_VALUE_COUNT; i++) {
				if (screen_off_values[i] == value) {
					current_settings.screen_off_timeout = value;
					break;
				}
			}
		}
		if (sscanf(line, "lyrics_enabled=%d", &value) == 1) {
			current_settings.lyrics_enabled = (value != 0);
		}
		if (sscanf(line, "bass_filter_hz=%d", &value) == 1) {
			for (int i = 0; i < BASS_FILTER_VALUE_COUNT; i++) {
				if (bass_filter_values[i] == value) {
					current_settings.bass_filter_hz = value;
					break;
				}
			}
		}
		if (sscanf(line, "soft_limiter=%d", &value) == 1) {
			if (value >= 0 && value < SOFT_LIMITER_VALUE_COUNT) {
				current_settings.soft_limiter_index = value;
			}
		}
		if (sscanf(line, "sample_rate_follow=%d", &value) == 1) {
			current_settings.rate_mode_follow = (value != 0) ? 1 : 0;
		}
		if (sscanf(line, "resampler_quality=%d", &value) == 1) {
			if (value >= 0 && value < RESAMPLER_QUALITY_COUNT) {
				current_settings.resampler_quality = value;
			}
		}
		if (sscanf(line, "buffer_frames=%d", &value) == 1) {
			for (int i = 0; i < BUFFER_FRAMES_VALUE_COUNT; i++) {
				if (buffer_frames_values[i] == value) {
					current_settings.buffer_frames = value;
					break;
				}
			}
		}
	}
	fclose(f);
}

void Settings_quit(void) {
	Settings_save();
}

int Settings_getScreenOffTimeout(void) {
	return current_settings.screen_off_timeout;
}

void Settings_cycleScreenOffNext(void) {
	int index = get_screen_off_index();
	index = (index + 1) % SCREEN_OFF_VALUE_COUNT;
	current_settings.screen_off_timeout = screen_off_values[index];
	Settings_save();
}

void Settings_cycleScreenOffPrev(void) {
	int index = get_screen_off_index();
	index = (index - 1 + SCREEN_OFF_VALUE_COUNT) % SCREEN_OFF_VALUE_COUNT;
	current_settings.screen_off_timeout = screen_off_values[index];
	Settings_save();
}

const char* Settings_getScreenOffDisplayStr(void) {
	switch (current_settings.screen_off_timeout) {
	case 60:
		return "60s";
	case 90:
		return "90s";
	case 120:
		return "120s";
	case 0:
		return "Off";
	default:
		return "60s";
	}
}

void Settings_save(void) {
	// Ensure directory exists
	char mkdir_cmd[512];
	snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", SETTINGS_DIR);
	system(mkdir_cmd);

	FILE* f = fopen(SETTINGS_FILE, "w");
	if (!f)
		return;

	fprintf(f, "screen_off_timeout=%d\n", current_settings.screen_off_timeout);
	fprintf(f, "lyrics_enabled=%d\n", current_settings.lyrics_enabled ? 1 : 0);
	fprintf(f, "bass_filter_hz=%d\n", current_settings.bass_filter_hz);
	fprintf(f, "soft_limiter=%d\n", current_settings.soft_limiter_index);
	fprintf(f, "sample_rate_follow=%d\n", current_settings.rate_mode_follow);
	fprintf(f, "resampler_quality=%d\n", current_settings.resampler_quality);
	fprintf(f, "buffer_frames=%d\n", current_settings.buffer_frames);
	fclose(f);
}

bool Settings_getLyricsEnabled(void) {
	return current_settings.lyrics_enabled;
}

void Settings_toggleLyrics(void) {
	current_settings.lyrics_enabled = !current_settings.lyrics_enabled;
	Settings_save();
}

// Bass filter getters/cyclers
int Settings_getBassFilterHz(void) {
	return current_settings.bass_filter_hz;
}

void Settings_cycleBassFilterNext(void) {
	int index = get_bass_filter_index();
	index = (index + 1) % BASS_FILTER_VALUE_COUNT;
	current_settings.bass_filter_hz = bass_filter_values[index];
	Settings_save();
}

void Settings_cycleBassFilterPrev(void) {
	int index = get_bass_filter_index();
	index = (index - 1 + BASS_FILTER_VALUE_COUNT) % BASS_FILTER_VALUE_COUNT;
	current_settings.bass_filter_hz = bass_filter_values[index];
	Settings_save();
}

const char* Settings_getBassFilterDisplayStr(void) {
	static char buf[16];
	if (current_settings.bass_filter_hz == 0)
		return "Off";
	snprintf(buf, sizeof(buf), "%d Hz", current_settings.bass_filter_hz);
	return buf;
}

// Soft limiter getters/cyclers
float Settings_getSoftLimiterThreshold(void) {
	if (current_settings.soft_limiter_index >= 0 && current_settings.soft_limiter_index < SOFT_LIMITER_VALUE_COUNT) {
		return soft_limiter_thresholds[current_settings.soft_limiter_index];
	}
	return soft_limiter_thresholds[DEFAULT_SOFT_LIMITER_INDEX];
}

void Settings_cycleSoftLimiterNext(void) {
	current_settings.soft_limiter_index = (current_settings.soft_limiter_index + 1) % SOFT_LIMITER_VALUE_COUNT;
	Settings_save();
}

void Settings_cycleSoftLimiterPrev(void) {
	current_settings.soft_limiter_index = (current_settings.soft_limiter_index - 1 + SOFT_LIMITER_VALUE_COUNT) % SOFT_LIMITER_VALUE_COUNT;
	Settings_save();
}

const char* Settings_getSoftLimiterDisplayStr(void) {
	switch (current_settings.soft_limiter_index) {
	case 0:
		return "Off";
	case 1:
		return "Mild";
	case 2:
		return "Medium";
	case 3:
		return "Strong";
	default:
		return "Medium";
	}
}

// Rate mode getters/cyclers
int Settings_getRateModeFollowSource(void) {
	return current_settings.rate_mode_follow;
}

void Settings_cycleRateModeNext(void) {
	current_settings.rate_mode_follow = !current_settings.rate_mode_follow;
	Settings_save();
}

void Settings_cycleRateModePrev(void) {
	current_settings.rate_mode_follow = !current_settings.rate_mode_follow;
	Settings_save();
}

const char* Settings_getRateModeDisplayStr(void) {
	switch (current_settings.rate_mode_follow) {
	case 0:
		return "Device default";
	case 1:
		return "Follow source";
	default:
		return "Device default";
	}
}

// Resampler quality getters/cyclers
int Settings_getResamplerQuality(void) {
	return current_settings.resampler_quality;
}

void Settings_cycleResamplerQualityNext(void) {
	current_settings.resampler_quality = (current_settings.resampler_quality + 1) % RESAMPLER_QUALITY_COUNT;
	Settings_save();
}

void Settings_cycleResamplerQualityPrev(void) {
	current_settings.resampler_quality = (current_settings.resampler_quality - 1 + RESAMPLER_QUALITY_COUNT) % RESAMPLER_QUALITY_COUNT;
	Settings_save();
}

const char* Settings_getResamplerQualityDisplayStr(void) {
	switch (current_settings.resampler_quality) {
	case 0:
		return "Fast";
	case 1:
		return "Medium";
	case 2:
		return "Best";
	default:
		return "Fast";
	}
}

// Buffer frames getters/cyclers
int Settings_getBufferFrames(void) {
	return current_settings.buffer_frames;
}

void Settings_cycleBufferFramesNext(void) {
	int index = get_buffer_frames_index();
	index = (index + 1) % BUFFER_FRAMES_VALUE_COUNT;
	current_settings.buffer_frames = buffer_frames_values[index];
	Settings_save();
}

void Settings_cycleBufferFramesPrev(void) {
	int index = get_buffer_frames_index();
	index = (index - 1 + BUFFER_FRAMES_VALUE_COUNT) % BUFFER_FRAMES_VALUE_COUNT;
	current_settings.buffer_frames = buffer_frames_values[index];
	Settings_save();
}

const char* Settings_getBufferFramesDisplayStr(void) {
	switch (current_settings.buffer_frames) {
	case 1024:
		return "1024";
	case 2048:
		return "2048";
	case 4096:
		return "4096";
	default:
		return "2048";
	}
}
