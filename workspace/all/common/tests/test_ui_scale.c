// Host unit test for ui_scale.h (pure, no SDL). Run via run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../ui_scale.h"

static void write_file(const char* path, const char* body) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(body, f);
	fclose(f);
}

static void test_sanitize_resolve(void) {
	assert(UIScale_sanitize(0) == 0);
	assert(UIScale_sanitize(2) == 2);
	assert(UIScale_sanitize(3) == 3);
	assert(UIScale_sanitize(1) == 0);
	assert(UIScale_sanitize(4) == 0);
	assert(UIScale_sanitize(-3) == 0);
	assert(UIScale_resolve(0, 3) == 3);
	assert(UIScale_resolve(0, 2) == 2);
	assert(UIScale_resolve(2, 3) == 2);
	assert(UIScale_resolve(3, 2) == 3);
	assert(UIScale_resolve(7, 2) == 2); // invalid stored -> native
}

static void test_native(void) {
	assert(UIScale_nativeForDevice("brick") == 3);
	assert(UIScale_nativeForDevice("brickpro") == 2);
	assert(UIScale_nativeForDevice("smartpro") == 2);
	assert(UIScale_nativeForDevice("smartpros") == 2);
	assert(UIScale_nativeForDevice("") == 2);
	assert(UIScale_nativeForDevice(NULL) == 2);
}

static void test_layout(void) {
	UIScaleLayout l;
	l = UIScale_layout(768, 2);
	assert(l.main_rows == 11 && l.padding == 10 && l.overlay_items == 8);
	l = UIScale_layout(768, 3);
	assert(l.main_rows == 7 && l.padding == 5 && l.overlay_items == 5);
	l = UIScale_layout(720, 2);
	assert(l.main_rows == 10 && l.padding == 10 && l.overlay_items == 8);
	l = UIScale_layout(720, 3);
	assert(l.main_rows == 6 && l.padding == 10 && l.overlay_items == 5);
}

static void test_parse_line(void) {
	int v = -1;
	assert(UIScale_parseLine("uiscale=3\n", &v) == 1 && v == 3);
	assert(UIScale_parseLine("uiscale=2\r\n", &v) == 1 && v == 2);
	assert(UIScale_parseLine("uiscale=0", &v) == 1 && v == 0);
	assert(UIScale_parseLine("uiscale=9\n", &v) == 1 && v == 0); // sanitized
	v = -1;
	assert(UIScale_parseLine("xuiscale=3\n", &v) == 0 && v == -1);
	assert(UIScale_parseLine("uiscale=\n", &v) == 0 && v == -1);
	assert(UIScale_parseLine("font=1\n", &v) == 0 && v == -1);
}

static void test_read_file(void) {
	char path[] = "/tmp/nx_test_ui_scale_XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);

	// key after font= (new keys are appended at the end of the file)
	write_file(path, "font=1\ncolor1=0xFFFFFFFF\nhintlabels=1\nuiscale=3\n");
	assert(UIScale_readFile(path) == 3);
	write_file(path, "font=1\n");
	assert(UIScale_readFile(path) == UI_SCALE_NATIVE);
	write_file(path, "uiscale=5\n");
	assert(UIScale_readFile(path) == UI_SCALE_NATIVE);
	write_file(path, "uiscale=2\nfont=1\nuiscale=3\n");
	assert(UIScale_readFile(path) == 3);
	unlink(path);
	assert(UIScale_readFile(path) == UI_SCALE_NATIVE); // missing file
	assert(UIScale_readFile(NULL) == UI_SCALE_NATIVE);
}

static void test_from_environment(void) {
	char dir[] = "/tmp/nx_test_ui_scale_dir_XXXXXX";
	assert(mkdtemp(dir));
	char path[256];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);

	setenv("SHARED_USERDATA_PATH", dir, 1);
	setenv("DEVICE", "brick", 1);
	assert(UIScale_fromEnvironment() == 3); // no file -> native (brick)
	write_file(path, "uiscale=2\n");
	assert(UIScale_fromEnvironment() == 2);
	setenv("DEVICE", "smartpros", 1);
	write_file(path, "uiscale=0\n");
	assert(UIScale_fromEnvironment() == 2);
	write_file(path, "uiscale=3\n");
	assert(UIScale_fromEnvironment() == 3);
	unsetenv("DEVICE");
	unsetenv("SHARED_USERDATA_PATH");
	assert(UIScale_fromEnvironment() == 2); // /mnt/SDCARD fallback absent on host, DEVICE unset -> 2, no crash

	unlink(path);
	rmdir(dir);
}

int main(void) {
	test_sanitize_resolve();
	test_native();
	test_layout();
	test_parse_line();
	test_read_file();
	test_from_environment();
	printf("test_ui_scale: all passed\n");
	return 0;
}
