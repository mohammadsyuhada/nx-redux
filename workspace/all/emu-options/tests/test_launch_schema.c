// Host-side check of the Ports Launch Options files: both shipped
// launch_options.json schemas parse, the global launch.ini that
// launch_options.sh seeds reads back as all-auto, and what options.elf's
// writers produce (global edit + per-game override) is read back by the
// shell helper nx_launch_opts.sh.
// Build+run:
//   cd workspace/all/emu-options/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -o build/test_launch_schema test_launch_schema.c \
//      ../opts_override.c ../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c \
//      -I.. -I../../common && \
//   ./build/test_launch_schema
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "emu_overlay_cfg.h"
#include "opts_override.h"

#define ROOT "build/launch_schema_root"
#define SYS "../../../../skeleton/SYSTEM/"
#define PAK "/paks/Tools/Xtras.pak/catalog/portmaster/pak/"

static EmuOvlConfig cfg;
static OptsOverrideState st;

static int file_exists(const char* path) {
	struct stat sb;
	return stat(path, &sb) == 0;
}

static EmuOvlItem* item(const char* key) {
	EmuOvlItem* it = emu_ovl_cfg_find_item(&cfg, "launch", key, NULL, NULL);
	assert(it);
	return it;
}

static const char* val(const char* key) {
	static char buf[64];
	EmuOvlItem* it = item(key);
	return emu_ovl_cfg_value_str(it, it->current_value, buf, sizeof(buf));
}

static void set_staged(const char* key, const char* token) {
	EmuOvlItem* it = item(key);
	int v = -1;
	assert(emu_ovl_cfg_parse_value(it, token, &v));
	it->staged_value = v;
	it->dirty = (v != it->current_value);
}

// Run launch_options.sh (no rom = global editor, or with a rom) against a
// stub options.elf that logs its argv and exits 0 (exit 3 would write the
// real /tmp/nextui_launch).
static void run_script(const char* plat, const char* rom) {
	char cmd[2048];
	assert(system("mkdir -p " ROOT "/bin " ROOT "/PORTS.pak " ROOT "/logs") == 0);
	FILE* stub = fopen(ROOT "/bin/options.elf", "w");
	assert(stub);
	fputs("#!/bin/sh\nfor a; do printf '%s|' \"$a\"; done > \"$STUB_LOG\"\nexit 0\n", stub);
	fclose(stub);
	snprintf(cmd, sizeof(cmd),
			 "cp " SYS "%s" PAK "launch_options.sh " SYS "%s" PAK "launch_options.json " ROOT "/PORTS.pak/ && "
			 "chmod +x " ROOT "/bin/options.elf && "
			 "PATH=\"$PWD/" ROOT "/bin:$PATH\" STUB_LOG=\"$PWD/" ROOT "/argv\" "
			 "USERDATA_PATH=\"$PWD/" ROOT "/userdata\" LOGS_PATH=\"$PWD/" ROOT "/logs\" "
			 "sh " ROOT "/PORTS.pak/launch_options.sh %s%s%s",
			 plat, plat, rom ? "\"" : "", rom ? rom : "", rom ? "\"" : "");
	assert(system(cmd) == 0);
}

static void read_argv(char* out, size_t n) {
	FILE* f = fopen(ROOT "/argv", "r");
	assert(f);
	size_t r = fread(out, 1, n - 1, f);
	out[r] = '\0';
	fclose(f);
}

// Source the shell helper and print "cores cpu gpu" for ROM.
static void helper_load(const char* plat, const char* rom, char* out, size_t n) {
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
			 "USERDATA_PATH=\"$PWD/" ROOT "/userdata\" sh -c '. " SYS "%s/bin/nx_launch_opts.sh; "
			 "nx_launch_opts_load PORTS \"$1\"; echo \"$NX_LO_CORES $NX_LO_CPU $NX_LO_GPU\"' sh \"%s\"",
			 plat, rom);
	FILE* p = popen(cmd, "r");
	assert(p);
	assert(fgets(out, (int)n, p));
	pclose(p);
	out[strcspn(out, "\n")] = '\0';
}

static void check_platform(const char* plat, int item_count) {
	char json[512], argv_buf[2048], got[128];
	const char* global = ROOT "/userdata/PORTS-launch/launch.ini";
	const char* rom = "/x/Roms/Ports (PORTS)/Don't Starve.v2.sh";
	const char* override = ROOT "/userdata/PORTS-launch/Don't Starve.v2.sh.ini";

	system("rm -rf " ROOT);

	// 1. schema parses with every key we use
	snprintf(json, sizeof(json), SYS "%s" PAK "launch_options.json", plat);
	memset(&cfg, 0, sizeof(cfg));
	assert(emu_ovl_cfg_load(&cfg, json) == 0);
	assert(strcmp(cfg.emulator, "ports") == 0);
	assert(strcmp(cfg.config_file, "launch.ini") == 0);
	assert(strcmp(cfg.config_section, "launch") == 0);
	assert(strcmp(cfg.options_hint, "Applies the next time the game starts") == 0);
	assert(cfg.section_count == 1);
	assert(strcmp(cfg.sections[0].name, "Launch Options") == 0);
	assert(strcmp(emu_ovl_cfg_section_name(&cfg, 0), "launch") == 0);
	assert(cfg.sections[0].item_count == item_count);
	EmuOvlItem* cpu = item("cpu");
	assert(cpu->type == EMU_OVL_TYPE_ENUM && cpu->value_count == 4);
	assert(strcmp(cpu->labels[2], "Normal") == 0);
	assert(cpu->description[0] != '\0'); // wording differs per platform
	if (item_count == 3) {
		EmuOvlItem* cores = item("cores");
		assert(cores->value_count == 5 && strcmp(cores->labels[1], "2 Little") == 0 && strcmp(cores->labels[3], "4 Little + 2 Big") == 0);
		assert(strcmp(cores->svalues[3], "little4big2") == 0);
		EmuOvlItem* gpu = item("gpu");
		assert(gpu->value_count == 3 && strcmp(gpu->labels[1], "Powersave") == 0);
		assert(gpu->description[0] != '\0');
	} else {
		assert(!emu_ovl_cfg_find_item(&cfg, "launch", "cores", NULL, NULL));
		assert(!emu_ovl_cfg_find_item(&cfg, "launch", "gpu", NULL, NULL));
	}

	// 2. the global editor run seeds launch.ini; it reads back as auto
	run_script(plat, NULL);
	assert(file_exists(global));
	read_argv(argv_buf, sizeof(argv_buf));
	assert(strstr(argv_buf, "--json|") && strstr(argv_buf, "/launch.ini|--game|Ports|"));
	assert(!strstr(argv_buf, "--launch") && !strstr(argv_buf, "--override"));
	set_staged("cpu", "performance"); // poison: the read must overwrite it
	cpu->current_value = cpu->staged_value;
	assert(emu_ovl_cfg_read_ini(&cfg, global) == 0);
	assert(strcmp(val("cpu"), "auto") == 0);
	if (item_count == 3) {
		assert(strcmp(val("cores"), "auto") == 0);
		assert(strcmp(val("gpu"), "auto") == 0);
	}
	assert(!emu_ovl_cfg_has_changes(&cfg));

	// 3. global edit through options.elf's writer, read back by the helper
	set_staged("cpu", "normal");
	if (item_count == 3)
		set_staged("gpu", "performance");
	assert(emu_ovl_cfg_has_changes(&cfg));
	assert(emu_ovl_cfg_write_ini(&cfg, global) == 0);
	emu_ovl_cfg_apply_staged(&cfg);
	helper_load(plat, rom, got, sizeof(got));
	assert(strcmp(got, item_count == 3 ? "auto normal performance" : "auto normal auto") == 0);

	// 4. per-game run passes --launch/--override for the name with a quote and dots
	run_script(plat, rom);
	read_argv(argv_buf, sizeof(argv_buf));
	assert(strstr(argv_buf, "--launch|--json|"));
	assert(strstr(argv_buf, "/PORTS-launch/Don't Starve.v2.sh.ini|--game|Don't Starve.v2|"));

	// 5. options.elf's override writer -> the helper finds and prefers it
	assert(emu_ovl_cfg_read_ini(&cfg, global) == 0);
	opts_snapshot_globals(&cfg, &st);
	assert(opts_read_override(&cfg, &st, override) == 0); // missing: no-op
	set_staged("cpu", "powersave");
	if (item_count == 3)
		set_staged("cores", "little4");
	assert(opts_write_override(&cfg, &st, override) == (item_count == 3 ? 2 : 1));
	opts_commit(&cfg, &st);
	helper_load(plat, rom, got, sizeof(got));
	assert(strcmp(got, item_count == 3 ? "little4 powersave performance" : "auto powersave auto") == 0);

	// 6. resetting to global unlinks the override; helper falls back
	set_staged("cpu", "normal");
	if (item_count == 3)
		set_staged("cores", "auto");
	assert(opts_write_override(&cfg, &st, override) == 0);
	assert(!file_exists(override));
	helper_load(plat, rom, got, sizeof(got));
	assert(strcmp(got, item_count == 3 ? "auto normal performance" : "auto normal auto") == 0);

	emu_ovl_cfg_free(&cfg);
	printf("  %s ok\n", plat);
}

int main(void) {
	check_platform("tg5040", 1);
	check_platform("tg5050", 3);
	printf("test_launch_schema: all passed\n");
	return 0;
}
