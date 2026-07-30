// Host-side tests for the enum item type in emu_overlay_cfg.c
// Build+run:
//   cd workspace/all/emu-options/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -o build/test_emu_ovl_enum test_emu_ovl_enum.c \
//      ../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c \
//      -I.. -I../../common && \
//   ./build/test_emu_ovl_enum
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_overlay_cfg.h"

#define ROOT "build/enum_test_root"

static void reset_root(void) {
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT);
}

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(content, f);
	fclose(f);
}

static const char* SCHEMA =
	"{\n"
	"  \"emulator\": \"testcore\",\n"
	"  \"config_section\": \"config\",\n"
	"  \"sections\": [\n"
	"    { \"name\": \"Options\", \"items\": [\n"
	"      { \"key\": \"tc_ratio\", \"label\": \"Aspect Ratio\", \"type\": \"enum\",\n"
	"        \"values\": [\"auto\", \"4:3\", \"16:9\"],\n"
	"        \"labels\": [\"Auto\", \"4:3\", \"16:9\"],\n"
	"        \"default\": \"4:3\" },\n"
	"      { \"key\": \"tc_hidden\", \"label\": \"Secret\", \"type\": \"enum\",\n"
	"        \"values\": [\"a\", \"b\"], \"default\": \"a\", \"hidden\": true },\n"
	"      { \"key\": \"tc_nolabels\", \"label\": \"No Labels\", \"type\": \"enum\",\n"
	"        \"values\": [\"x\", \"y\"] }\n"
	"    ]}\n"
	"  ]\n"
	"}\n";

static void test_load_and_defaults(void) {
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(cfg.section_count == 1);
	// hidden item skipped entirely
	assert(cfg.sections[0].item_count == 2);
	EmuOvlItem* it = &cfg.sections[0].items[0];
	assert(it->type == EMU_OVL_TYPE_ENUM);
	assert(it->value_count == 3);
	assert(strcmp(it->svalues[0], "auto") == 0);
	assert(strcmp(it->svalues[2], "16:9") == 0);
	assert(strcmp(it->labels[1], "4:3") == 0);
	assert(it->default_value == 1); // "4:3" -> index 1
	assert(it->current_value == 1);
	// missing default -> index 0
	assert(cfg.sections[0].items[1].default_value == 0);
	emu_ovl_cfg_free(&cfg);
	// double-free safety: free again is a no-op
	emu_ovl_cfg_free(&cfg);
}

static void test_parse_format_intern(void) {
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	EmuOvlItem* it = &cfg.sections[0].items[0];

	int v = -1;
	assert(emu_ovl_cfg_parse_value(it, "16:9", &v) && v == 2);
	assert(!emu_ovl_cfg_parse_value(it, "not-a-value", &v)); // unknown: false, no intern
	char out[64];
	emu_ovl_cfg_format_value(it, 2, out, sizeof(out));
	assert(strcmp(out, "16:9") == 0);
	emu_ovl_cfg_format_value(it, 99, out, sizeof(out)); // clamped
	assert(strcmp(out, "16:9") == 0);

	// intern appends unknown values with label = value
	int idx = emu_ovl_cfg_enum_intern(it, "21:9");
	assert(idx == 3 && it->value_count == 4);
	assert(strcmp(it->svalues[3], "21:9") == 0);
	assert(strcmp(it->labels[3], "21:9") == 0);
	assert(emu_ovl_cfg_enum_intern(it, "21:9") == 3); // idempotent
	emu_ovl_cfg_free(&cfg);
}

static void test_read_ini_enum(void) {
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	write_file(ROOT "/emu.cfg",
			   "[config]\n"
			   "tc_ratio = 16:9\n"
			   "tc_nolabels = garbagevalue\n");
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg, ROOT "/emu.cfg") == 0);
	assert(cfg.sections[0].items[0].current_value == 2);
	// unknown enum value in the INI leaves the default in place (no intern here;
	// the minarch backend interns explicitly where round-tripping matters)
	assert(cfg.sections[0].items[1].current_value == 0);
	emu_ovl_cfg_free(&cfg);
}

int main(void) {
	test_load_and_defaults();
	test_parse_format_intern();
	test_read_ini_enum();
	printf("test_emu_ovl_enum: all tests passed\n");
	return 0;
}
