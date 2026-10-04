// UI scale — the user-selectable 2x/3x UI scale and the per-panel layout
// constants each scale uses. Header-only and SDL-free: included by the
// platform headers, config.c and the standalone-emulator overlay (which is
// built into flycast/mupen64plus without config.c). Host-tested by
// tests/test_ui_scale.c.
#ifndef UI_SCALE_H
#define UI_SCALE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_SCALE_NATIVE 0 // stored value: follow the device's default scale

typedef struct {
	int main_rows;	   // MAIN_ROW_COUNT
	int padding;	   // PADDING
	int overlay_items; // emulator overlay items per page
} UIScaleLayout;

// Stored values are 0 (native), 2 or 3; anything else means native.
static inline int UIScale_sanitize(int stored) {
	return (stored == 2 || stored == 3) ? stored : UI_SCALE_NATIVE;
}

static inline int UIScale_resolve(int stored, int native) {
	stored = UIScale_sanitize(stored);
	return stored ? stored : native;
}

// Only the Brick's small panel defaults to 3x; the physically larger Brick Pro
// and the 720p devices default to 2x.
static inline int UIScale_nativeForDevice(const char* device) {
	return (device && strcmp(device, "brick") == 0) ? 3 : 2;
}

// 768 rows are the Brick / Brick Pro panels, 720 the Smart Pro family.
// `scale` is a resolved value (2 or 3); anything other than 3 is treated as 2.
static inline UIScaleLayout UIScale_layout(int panel_h, int scale) {
	if (panel_h >= 768)
		return scale == 3 ? (UIScaleLayout){7, 5, 5} : (UIScaleLayout){11, 10, 8};
	return scale == 3 ? (UIScaleLayout){6, 10, 5} : (UIScaleLayout){10, 10, 8};
}

// PADDING (units) at `scale`, kept at the device's default scale's size in pixels so the screen-edge gaps don't move
// with the UI scale: the Brick's 15 px (5 at 3x) is 8 at 2x (16 px), the others' 20 px (10 at 2x) is 7 at 3x (21 px).
static inline int UIScale_padding(int panel_h, int scale, int native) {
	int px = UIScale_layout(panel_h, native).padding * native;
	return (px + scale / 2) / scale;
}

// Parses one minuisettings.txt line. Returns 1 and stores the sanitized value
// when the line is "uiscale=<n>", else 0 and leaves *out untouched.
static inline int UIScale_parseLine(const char* line, int* out) {
	int v;
	if (strncmp(line, "uiscale=", 8) != 0 || sscanf(line + 8, "%d", &v) != 1)
		return 0;
	*out = UIScale_sanitize(v);
	return 1;
}

// Stored value from a minuisettings.txt; UI_SCALE_NATIVE when the file or the
// key is missing. The last uiscale= line wins, like CFG_init's parse loop.
static inline int UIScale_readFile(const char* path) {
	int stored = UI_SCALE_NATIVE;
	FILE* f = path ? fopen(path, "r") : NULL;
	if (!f)
		return stored;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		UIScale_parseLine(line, &stored);
	}
	fclose(f);
	return stored;
}

// Resolved scale for processes that don't link config.c (the emulator
// overlay): reads the same file CFG_init does, native from $DEVICE.
static inline int UIScale_fromEnvironment(void) {
	const char* dir = getenv("SHARED_USERDATA_PATH");
	char path[512];
	snprintf(path, sizeof(path), "%s/minuisettings.txt",
			 dir && dir[0] ? dir : "/mnt/SDCARD/.userdata/shared");
	return UIScale_resolve(UIScale_readFile(path), UIScale_nativeForDevice(getenv("DEVICE")));
}

#endif
