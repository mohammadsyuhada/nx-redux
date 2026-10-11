// Host-side tests for opts_launch.c (the --launch flat page's row collector).
// Build+run:
//   cd workspace/all/emu-options/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -o build/test_opts_launch test_opts_launch.c \
//      ../opts_launch.c ../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c \
//      -I.. -I../../common && \
//   ./build/test_opts_launch
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_overlay_cfg.h"
#include "opts_launch.h"

#define ROOT "build/launch_test_root"

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

// The ports launch_options.json shape, plus a gated section to prove the
// collector follows visible_when on the staged value.
static const char* SCHEMA =
	"{ \"emulator\": \"ports\", \"config_file\": \"launch.ini\", \"config_section\": \"launch\", \"sections\": [\n"
	"  { \"name\": \"Launch Options\", \"items\": [\n"
	"    { \"key\": \"cores\", \"label\": \"CPU Cores\", \"type\": \"enum\", \"values\": [\"auto\",\"little4\",\"little4big2\",\"all8\"], \"labels\": [\"Auto\",\"4 Little\",\"4 Little + 2 Big\",\"All 8\"], \"default\": \"auto\" },\n"
	"    { \"key\": \"cpu\", \"label\": \"CPU Mode\", \"type\": \"enum\", \"values\": [\"auto\",\"powersave\",\"normal\",\"performance\"], \"labels\": [\"Auto\",\"Powersave\",\"Normal\",\"Performance\"], \"default\": \"auto\" }\n"
	"  ]},\n"
	"  { \"name\": \"Hidden\", \"visible_when\": {\"key\": \"cpu\", \"value\": \"performance\"}, \"items\": [\n"
	"    { \"key\": \"gpu\", \"label\": \"GPU Mode\", \"type\": \"enum\", \"values\": [\"auto\",\"powersave\",\"performance\"], \"default\": \"auto\" }\n"
	"  ]}\n"
	"] }\n";

static EmuOvlConfig cfg;

int main(void) {
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(cfg.section_count == 2);

	int sec[8], item[8];

	// Gate closed: only the two unconditional items, in schema order.
	int n = opts_launch_collect(&cfg, sec, item, 8);
	assert(n == 2);
	assert(sec[0] == 0 && item[0] == 0);
	assert(sec[1] == 0 && item[1] == 1);
	assert(emu_ovl_cfg_section_visible(&cfg, 0));
	assert(!emu_ovl_cfg_section_visible(&cfg, 1));

	// Staging cpu = performance opens the gate (ENUM staged_value is the
	// index into svalues).
	EmuOvlItem* cpu = &cfg.sections[0].items[1];
	int perf = -1;
	for (int v = 0; v < cpu->value_count; v++)
		if (strcmp(cpu->svalues[v], "performance") == 0)
			perf = v;
	assert(perf >= 0);
	cpu->staged_value = perf;

	n = opts_launch_collect(&cfg, sec, item, 8);
	assert(n == 3);
	assert(sec[0] == 0 && item[0] == 0);
	assert(sec[1] == 0 && item[1] == 1);
	assert(sec[2] == 1 && item[2] == 0);

	// Capped at max.
	n = opts_launch_collect(&cfg, sec, item, 1);
	assert(n == 1);
	assert(sec[0] == 0 && item[0] == 0);

	emu_ovl_cfg_free(&cfg);
	printf("test_opts_launch: all tests passed\n");
	return 0;
}
