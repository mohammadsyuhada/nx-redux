#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>

// Music Player app-specific settings
// These are separate from the global NextUI settings (CFG_*)

// Initialize settings (loads from file if exists)
void Settings_init(void);

// Cleanup settings (saves and frees resources)
void Settings_quit(void);

// Screen off timeout setting (in seconds)
// Values: 60, 90, 120, 0 (off)
int Settings_getScreenOffTimeout(void);

// Cycle through screen off timeout values
void Settings_cycleScreenOffNext(void); // 60 -> 90 -> 120 -> Off -> 60
void Settings_cycleScreenOffPrev(void); // 60 -> Off -> 120 -> 90 -> 60

// Get display string for current screen off timeout
// Returns: "60s", "90s", "120s", or "Off"
const char* Settings_getScreenOffDisplayStr(void);

// Lyrics enabled setting
bool Settings_getLyricsEnabled(void);
void Settings_toggleLyrics(void);

// Speaker bass filter (high-pass cutoff in Hz, 0 = off)
int Settings_getBassFilterHz(void);
void Settings_cycleBassFilterNext(void);
void Settings_cycleBassFilterPrev(void);
const char* Settings_getBassFilterDisplayStr(void);

// Speaker soft limiter (0 = off, 1=mild, 2=medium, 3=strong)
float Settings_getSoftLimiterThreshold(void);
void Settings_cycleSoftLimiterNext(void);
void Settings_cycleSoftLimiterPrev(void);
const char* Settings_getSoftLimiterDisplayStr(void);

// Output sample-rate mode (0 = fixed device-default pipeline, 1 = follow source rate)
int Settings_getRateModeFollowSource(void);
void Settings_cycleRateModeNext(void);
void Settings_cycleRateModePrev(void);
const char* Settings_getRateModeDisplayStr(void);

// Resampler quality: 0=Fast (SRC_SINC_FASTEST), 1=Medium, 2=Best
int Settings_getResamplerQuality(void);
void Settings_cycleResamplerQualityNext(void);
void Settings_cycleResamplerQualityPrev(void);
const char* Settings_getResamplerQualityDisplayStr(void);

// SDL audio buffer size in frames (1024/2048/4096)
int Settings_getBufferFrames(void);
void Settings_cycleBufferFramesNext(void);
void Settings_cycleBufferFramesPrev(void);
const char* Settings_getBufferFramesDisplayStr(void);

// Save settings to file (auto-called on change)
void Settings_save(void);

#endif
