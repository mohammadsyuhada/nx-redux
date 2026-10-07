// Host unit test for ui_scale.h (pure, no SDL). Run via run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../ui_scale.h"
#include "../ui_text_sizes.h"

static void write_file(const char* path, const char* body) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(body, f);
	fclose(f);
}

static void test_from_environment(void) {
	char dir[] = "/tmp/nx_test_ui_scale_dir_XXXXXX";
	assert(mkdtemp(dir));
	char path[256];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);

	setenv("SHARED_USERDATA_PATH", dir, 1);
	setenv("DEVICE", "brick", 1);
	assert(UIScale_fromEnvironment() == 3.0f); // no file -> the device's scale
	write_file(path, "uiscale=2\n");
	assert(UIScale_fromEnvironment() == 3.0f); // the old setting no longer overrides
	write_file(path, "uiscale_dev=2.5\n");
	assert(UIScale_fromEnvironment() == 2.5f);
	setenv("DEVICE", "brickpro", 1);
	write_file(path, "font=1\n");
	assert(UIScale_fromEnvironment() == 2.5f);
	unsetenv("DEVICE");
	unsetenv("SHARED_USERDATA_PATH");
	assert(UIScale_fromEnvironment() == 2.25f); // /mnt/SDCARD fallback absent on host, DEVICE unset, no crash

	unlink(path);
	rmdir(dir);
}

static void test_quantize(void) {
	assert(UIScale_quantize(3.0f) == 3.0f);
	assert(UIScale_quantize(2.4375f) == 2.4375f);
	assert(UIScale_quantize(2.44f) == 2.4375f);
	assert(UIScale_quantize(2.47f) == 2.5f); // nearest 1/16
	assert(UIScale_quantize(0.0f) == UI_SCALE_MIN);
	assert(UIScale_quantize(9.0f) == UI_SCALE_MAX);
	assert(UIScale_quantize(-1.0f) == UI_SCALE_MIN);
}

static void test_device_table(void) {
	assert(UIScale_forDevice("brick") == 3.0f);
	assert(UIScale_forDevice("brickpro") == 2.5f);
	assert(UIScale_forDevice("smartpros") == 2.25f);
	assert(UIScale_forDevice("smartpro") == 2.25f);
	assert(UIScale_forDevice("") == 2.25f);
	assert(UIScale_forDevice(NULL) == 2.25f);
}

static void test_dev_override(void) {
	float v = 0;
	assert(UIScale_parseDevLine("uiscale_dev=2.5\n", &v) == 1 && v == 2.5f);
	assert(UIScale_parseDevLine("uiscale_dev=2.51\n", &v) == 1 && v == 2.5f);
	assert(UIScale_parseDevLine("uiscale_dev=9\n", &v) == 1 && v == UI_SCALE_MAX);
	v = 7;
	assert(UIScale_parseDevLine("uiscale_dev=abc\n", &v) == 0 && v == 7);
	assert(UIScale_parseDevLine("uiscale_dev=\n", &v) == 0 && v == 7);
	assert(UIScale_parseDevLine("uiscale_dev=0\n", &v) == 0 && v == 7); // <=0 means "no override"
	assert(UIScale_parseDevLine("uiscale=3\n", &v) == 0 && v == 7);		// the old key is ignored
	const char* p = "/tmp/nx_test_uiscale_dev.txt";
	write_file(p, "font=1\nuiscale_dev=2.5\nuiscale=3\nuiscale_dev=junk\nuiscale_dev=2.75\n");
	assert(UIScale_readDevOverride(p) == 2.75f);
	assert(UIScale_resolveDevice("brick", p) == 2.75f);
	write_file(p, "uiscale=2\n");
	assert(UIScale_readDevOverride(p) == 0.0f);
	assert(UIScale_resolveDevice("brick", p) == 3.0f); // the old key never overrides
	unlink(p);
	assert(UIScale_resolveDevice("brickpro", p) == 2.5f); // missing file
	assert(UIScale_resolveDevice("brick", NULL) == 3.0f);
}

static void test_px(void) {
	assert(UIScale_px(16, 3.0f) == 48);
	assert(UIScale_px(5, 2.4375f) == 12); // 12.19
	assert(UIScale_px(-2, 2.25f) == -5);  // -4.5 rounds away from zero, symmetric with +2 -> 5
	assert(UIScale_px(2, 2.25f) == 5);
}

static void test_overlay_items(void) {
	assert(UIScale_overlayItems(768, 3.0f) == 5); // the Brick today
	int prev = 99;
	for (float s = 1.5f; s <= 4.0f; s += 0.0625f) { // never more rows as the scale grows
		int n = UIScale_overlayItems(720, s);
		assert(n <= prev && n >= 3);
		prev = n;
	}
}

static void test_pick_sheet(void) {
	const float baked[] = {1.5f, 2.0f, 2.25f, 3.0f, 2.4375f}; // any order
	int n = sizeof(baked) / sizeof(baked[0]);
	assert(UIScale_pickSheet(2.25f, baked, n) == 2.25f);  // baked: itself
	assert(UIScale_pickSheet(2.5625f, baked, n) == 3.0f); // the smallest larger one (downscaling keeps it sharp)
	assert(UIScale_pickSheet(2.3125f, baked, n) == 2.4375f);
	assert(UIScale_pickSheet(3.5f, baked, n) == 3.0f); // none larger: the largest
	assert(UIScale_pickSheet(2.0f, baked, 0) == 0.0f); // nothing baked
}

static void test_text_sizes(void) {
	assert(UIScale_deviceIndex("brick") == UI_DEVICE_BRICK);
	assert(UIScale_deviceIndex("brickpro") == UI_DEVICE_BRICKPRO);
	assert(UIScale_deviceIndex("smartpros") == UI_DEVICE_SMARTPRO);
	assert(UIScale_deviceIndex("smartpro") == UI_DEVICE_SMARTPRO);
	assert(UIScale_deviceIndex(NULL) == UI_DEVICE_SMARTPRO);
	TextPx t = {30, 22, 20};
	assert(TextPx_for(t, UI_DEVICE_BRICK) == 30 && TextPx_for(t, UI_DEVICE_BRICKPRO) == 22 &&
		   TextPx_for(t, UI_DEVICE_SMARTPRO) == 20);
}

int main(void) {
	test_from_environment();
	test_quantize();
	test_device_table();
	test_dev_override();
	test_px();
	test_overlay_items();
	test_pick_sheet();
	test_text_sizes();
	printf("test_ui_scale: all passed\n");
	return 0;
}
