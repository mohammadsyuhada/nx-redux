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
	test_quantize();
	test_device_table();
	test_dev_override();
	test_pick_sheet();
	test_text_sizes();
	printf("test_ui_scale: all passed\n");
	return 0;
}
