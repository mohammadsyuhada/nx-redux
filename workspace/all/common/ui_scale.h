// UI scale — each device's one UI scale, tuned so the UI is the same physical size on every panel, and the hidden
// uiscale_dev= override used to tune it on hardware. Header-only and SDL-free: included by the platform headers and the
// standalone-emulator overlay (which is built into flycast/mupen64plus without platform.c). Host-tested by
// tests/test_ui_scale.c.
#ifndef UI_SCALE_H
#define UI_SCALE_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_SCALE_MIN 1.5f
#define UI_SCALE_MAX 4.0f
#define UI_PADDING_UNITS 5 // the Brick's 15 px at 3.0: one physical size everywhere

// Clamped to [UI_SCALE_MIN, UI_SCALE_MAX] and snapped to 1/16, so every asset sheet (128 units) is whole pixels and
// its file name (%g) is stable.
static inline float UIScale_quantize(float s) {
	if (!(s >= UI_SCALE_MIN)) // also catches NaN
		s = UI_SCALE_MIN;
	if (s > UI_SCALE_MAX)
		s = UI_SCALE_MAX;
	return roundf(s * 16.0f) / 16.0f;
}

typedef enum { UI_DEVICE_BRICK,
			   UI_DEVICE_BRICKPRO,
			   UI_DEVICE_SMARTPRO,
			   UI_DEVICE_COUNT } UIDevice;

// $DEVICE (or a platform's UI_DEVICE_NAME) as a UIDevice; the Smart Pro family and anything unknown are
// UI_DEVICE_SMARTPRO.
static inline UIDevice UIScale_deviceIndex(const char* device) {
	if (device && strcmp(device, "brick") == 0)
		return UI_DEVICE_BRICK;
	if (device && strcmp(device, "brickpro") == 0)
		return UI_DEVICE_BRICKPRO;
	return UI_DEVICE_SMARTPRO;
}

// Each device's UI scale, tuned by eye so the UI is one physical size on every panel (seeded from the panels'
// density against the Brick's 400 ppi at 3.0).
static inline float UIScale_forDevice(const char* device) {
	if (device && strcmp(device, "brick") == 0)
		return 3.0f; // 3.2" 1024x768, 400 ppi
	if (device && strcmp(device, "brickpro") == 0)
		return 2.5f; // 3.95" 1024x768, 324 ppi (2.4375 by density; 2.5 for rounder sizes, 2026-10-07)
	return 2.25f;	 // 4.96" 1280x720, 296 ppi (the Smart Pro family)
}

// Parses one minuisettings.txt line. Returns 1 and stores the quantized value when the line is "uiscale_dev=<float>"
// with a positive number, else 0 and leaves *out untouched.
static inline int UIScale_parseDevLine(const char* line, float* out) {
	char* end;
	if (strncmp(line, "uiscale_dev=", 12) != 0)
		return 0;
	float v = strtof(line + 12, &end);
	if (end == line + 12 || !(v > 0.0f))
		return 0;
	*out = UIScale_quantize(v);
	return 1;
}

// The hidden tuning override from a minuisettings.txt (hand-added, never shown in Settings); 0 when the file or the
// key is missing. The last valid uiscale_dev= line wins.
static inline float UIScale_readDevOverride(const char* path) {
	float v = 0.0f;
	FILE* f = path ? fopen(path, "r") : NULL;
	if (!f)
		return 0.0f;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		UIScale_parseDevLine(line, &v);
	}
	fclose(f);
	return v;
}

// The UI scale for `device`: the override in `settings_path` when there is one, else the device's.
static inline float UIScale_resolveDevice(const char* device, const char* settings_path) {
	float o = UIScale_readDevOverride(settings_path);
	return o > 0.0f ? o : UIScale_forDevice(device);
}

// `units` at `scale` in whole pixels, rounded half away from zero (so offsets are symmetric).
static inline int UIScale_px(float units, float scale) {
	return (int)lroundf(units * scale);
}

// The standalone overlay's rows per page: those between its two 28-unit bars, less one row of air (the Brick keeps
// its 5 at 3.0); never fewer than 3.
static inline int UIScale_overlayItems(int panel_h, float scale) {
	int n = (int)((panel_h / scale - 56.0f) / 30.0f) - 1;
	return n < 3 ? 3 : n;
}

// Which baked sheet to draw a `scale` that has none of its own from (a uiscale_dev= value being tuned): `scale`
// itself when baked, else the smallest larger one (scaled down at load, so it stays sharp), else the largest; 0 when
// `n` is 0.
static inline float UIScale_pickSheet(float scale, const float* baked, int n) {
	float best = 0.0f, largest = 0.0f;
	for (int i = 0; i < n; i++) {
		if (baked[i] == scale)
			return scale;
		if (baked[i] > scale && (best == 0.0f || baked[i] < best))
			best = baked[i];
		if (baked[i] > largest)
			largest = baked[i];
	}
	return best > 0.0f ? best : largest;
}

// The scale for processes that don't link config.c or platform.c (the emulator overlay): $DEVICE, and the same
// minuisettings.txt GFX_init reads.
static inline float UIScale_fromEnvironment(void) {
	const char* dir = getenv("SHARED_USERDATA_PATH");
	char path[512];
	snprintf(path, sizeof(path), "%s/minuisettings.txt",
			 dir && dir[0] ? dir : "/mnt/SDCARD/.userdata/shared");
	return UIScale_resolveDevice(getenv("DEVICE"), path);
}

#endif
