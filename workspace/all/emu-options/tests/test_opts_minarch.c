// Host-side tests for opts_minarch.c (minarch flat-cfg storage backend).
// Build+run (NOTE the -D limits: must match workspace/all/emu-options/Makefile):
//   cd workspace/all/emu-options/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -DEMU_OVL_MAX_SECTIONS=32 -DEMU_OVL_MAX_ITEMS=64 \
//      -DEMU_OVL_MAX_VALUES=32 -DEMU_OVL_MAX_DESC=512 \
//      -o build/test_opts_minarch test_opts_minarch.c ../opts_minarch.c ../opts_override.c \
//      ../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c -I.. -I../../common && \
//   ./build/test_opts_minarch
// The EmuOvlConfig test locals are static: at these -D limits the struct is
// ~11 MB, far past the 8 MB default stack. Safe because every use starts
// with emu_ovl_cfg_load/opts_minarch_load, which memset their structs.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_overlay_cfg.h"
#include "opts_minarch.h"

#define ROOT "build/minarch_test_root"

static void reset_root(void) {
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT "/cfgdir");
	unsetenv("DEVICE");
}

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(content, f);
	fclose(f);
}

static char* slurp(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f)
		return NULL;
	static char buf[8192];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static int file_exists(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

// Two enum options; gpsp-style keys.
static const char* SCHEMA =
	"{ \"emulator\": \"gpsp\", \"sections\": [ { \"name\": \"Options\", \"items\": [\n"
	"  { \"key\": \"tc_frameskip\", \"label\": \"Frameskip\", \"type\": \"enum\",\n"
	"    \"values\": [\"off\", \"auto\", \"manual\"], \"default\": \"off\" },\n"
	"  { \"key\": \"tc_ratio\", \"label\": \"Ratio\", \"type\": \"enum\",\n"
	"    \"values\": [\"4:3\", \"16:9\"], \"default\": \"4:3\" }\n"
	"] } ] }\n";

static void load(EmuOvlConfig* cfg, OptsMinarchState* st, OptsOverrideState* ost,
				 const char* sys, const char* def, const char* alt) {
	write_file(ROOT "/schema.json", SCHEMA);
	assert(emu_ovl_cfg_load(cfg, ROOT "/schema.json") == 0);
	assert(opts_minarch_load(cfg, st, ost, sys, def, ROOT "/cfgdir", alt) == 0);
}

// 1. Layer precedence: system < default < console.
static void test_layering(void) {
	reset_root();
	write_file(ROOT "/system.cfg", "tc_frameskip = auto\n");
	write_file(ROOT "/default.cfg", "tc_frameskip = manual\ntc_ratio = 16:9\n");
	write_file(ROOT "/cfgdir/minarch.cfg", "tc_ratio = 4:3\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, ROOT "/system.cfg", ROOT "/default.cfg", NULL);
	assert(cfg.sections[0].items[0].current_value == 2); // manual (default layer beats system)
	assert(cfg.sections[0].items[1].current_value == 0); // 4:3 (console beats default)
	assert(ost.global_value[0][0] == 2 && ost.global_value[0][1] == 0);
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 2. Game file REPLACES console: keys absent from it fall to system/default,
//    NOT to console values.
static void test_game_replaces_console(void) {
	reset_root();
	write_file(ROOT "/default.cfg", "tc_frameskip = off\n");
	write_file(ROOT "/cfgdir/minarch.cfg", "tc_frameskip = auto\ntc_ratio = 16:9\n");
	write_file(ROOT "/cfgdir/Game.gba.cfg", "tc_frameskip = manual\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, ROOT "/default.cfg", "Game.gba");
	assert(cfg.sections[0].items[0].current_value == 2);  // manual, from game file
	assert(cfg.sections[0].items[1].current_value == 0);  // 4:3 — default layer, NOT console's 16:9
	assert(ost.global_value[0][0] == 1);				  // console baseline still auto
	assert(ost.overridden[0][0] && ost.overridden[0][1]); // both differ from console baseline
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 3. Locked keys are pruned (any layer) and empty sections vanish.
static void test_lock_pruning(void) {
	reset_root();
	write_file(ROOT "/default.cfg", "-tc_frameskip = auto\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, ROOT "/default.cfg", NULL);
	assert(cfg.sections[0].item_count == 1);
	assert(strcmp(cfg.sections[0].items[0].key, "tc_ratio") == 0);
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 4. Global save: minimal diff, byte-preserves unrelated lines, appends
//    missing keys, creates the file when absent.
static void test_global_save(void) {
	reset_root();
	write_file(ROOT "/cfgdir/minarch.cfg",
			   "minarch_screen_scaling = 2\n"
			   "-tc_locked_other = x\n"
			   "tc_frameskip = auto\n"
			   "bind Up = UP\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, NULL, NULL);
	cfg.sections[0].items[0].staged_value = 2; // manual
	cfg.sections[0].items[1].staged_value = 1; // 16:9 — key absent: must be appended
	assert(opts_minarch_save(&cfg, &st, &ost) == 0);
	char* out = slurp(ROOT "/cfgdir/minarch.cfg");
	assert(strstr(out, "minarch_screen_scaling = 2\n"));
	assert(strstr(out, "-tc_locked_other = x\n"));
	assert(strstr(out, "tc_frameskip = manual\n"));
	assert(strstr(out, "bind Up = UP\n"));
	assert(strstr(out, "tc_ratio = 16:9\n"));
	assert(!strstr(out, "tc_frameskip = auto"));
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 5. Per-game fresh save seeds from console content (frontend lines carried).
static void test_pergame_seed(void) {
	reset_root();
	write_file(ROOT "/cfgdir/minarch.cfg",
			   "minarch_screen_scaling = 2\ntc_frameskip = auto\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, NULL, "Game.gba");
	cfg.sections[0].items[0].staged_value = 2; // manual
	assert(opts_minarch_save(&cfg, &st, &ost) == 0);
	char* out = slurp(ROOT "/cfgdir/Game.gba.cfg");
	assert(out);
	assert(strstr(out, "minarch_screen_scaling = 2\n")); // console frontend carried forward
	assert(strstr(out, "tc_frameskip = manual\n"));
	assert(strstr(out, "tc_ratio = 4:3\n")); // every schema key present (full snapshot)
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 6. Per-game zero-diff: no file created; and a redundant existing file is
//    unlinked, while one carrying its own frontend lines is kept.
static void test_pergame_redundancy(void) {
	reset_root();
	write_file(ROOT "/cfgdir/minarch.cfg", "tc_frameskip = auto\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, NULL, "Game.gba");
	assert(opts_minarch_save(&cfg, &st, &ost) == 0);
	assert(!file_exists(ROOT "/cfgdir/Game.gba.cfg")); // nothing staged, nothing created

	// existing redundant file (== console + schema keys at console values) is removed
	write_file(ROOT "/cfgdir/Game.gba.cfg",
			   "tc_frameskip = auto\ntc_ratio = 4:3\n");
	static EmuOvlConfig cfg2;
	static OptsMinarchState st2;
	static OptsOverrideState ost2;
	load(&cfg2, &st2, &ost2, NULL, NULL, "Game.gba");
	assert(opts_minarch_save(&cfg2, &st2, &ost2) == 0);
	assert(!file_exists(ROOT "/cfgdir/Game.gba.cfg"));
	opts_minarch_free(&st2);
	emu_ovl_cfg_free(&cfg2);

	// a game file with its OWN frontend line (in-game "Save for game") survives
	// a clear-all: schema keys revert, the frontend line stays
	write_file(ROOT "/cfgdir/Game.gba.cfg",
			   "minarch_screen_scaling = 4\ntc_frameskip = manual\ntc_ratio = 4:3\n");
	static EmuOvlConfig cfg3;
	static OptsMinarchState st3;
	static OptsOverrideState ost3;
	load(&cfg3, &st3, &ost3, NULL, NULL, "Game.gba");
	for (int i = 0; i < cfg3.sections[0].item_count; i++)
		cfg3.sections[0].items[i].staged_value = ost3.global_value[0][i]; // "Clear All Overrides"
	assert(opts_minarch_save(&cfg3, &st3, &ost3) == 0);
	char* out = slurp(ROOT "/cfgdir/Game.gba.cfg");
	assert(out);
	assert(strstr(out, "minarch_screen_scaling = 4\n"));
	assert(strstr(out, "tc_frameskip = auto\n")); // reverted to console value
	opts_minarch_free(&st3);
	emu_ovl_cfg_free(&cfg3);

	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 7. DEVICE env suffixes the user tier strictly (no fallback to plain).
static void test_device_suffix(void) {
	reset_root();
	setenv("DEVICE", "brick", 1);
	write_file(ROOT "/cfgdir/minarch.cfg", "tc_frameskip = manual\n"); // must be IGNORED
	write_file(ROOT "/cfgdir/minarch-brick.cfg", "tc_frameskip = auto\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, NULL, NULL);
	assert(cfg.sections[0].items[0].current_value == 1); // auto from -brick file
	cfg.sections[0].items[0].staged_value = 2;
	assert(opts_minarch_save(&cfg, &st, &ost) == 0);
	assert(strstr(slurp(ROOT "/cfgdir/minarch-brick.cfg"), "tc_frameskip = manual\n"));
	assert(strstr(slurp(ROOT "/cfgdir/minarch.cfg"), "tc_frameskip = manual\n")); // untouched original
	unsetenv("DEVICE");
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

// 8. Unknown cfg value is interned and round-trips (per-game full snapshot).
static void test_unknown_value_roundtrip(void) {
	reset_root();
	write_file(ROOT "/cfgdir/minarch.cfg", "tc_frameskip = experimental\n");
	static EmuOvlConfig cfg;
	static OptsMinarchState st;
	static OptsOverrideState ost;
	load(&cfg, &st, &ost, NULL, NULL, "Game.gba");
	EmuOvlItem* it = &cfg.sections[0].items[0];
	assert(it->value_count == 4); // interned as 4th value
	assert(strcmp(it->svalues[it->current_value], "experimental") == 0);
	cfg.sections[0].items[1].staged_value = 1; // force a real diff elsewhere
	assert(opts_minarch_save(&cfg, &st, &ost) == 0);
	assert(strstr(slurp(ROOT "/cfgdir/Game.gba.cfg"), "tc_frameskip = experimental\n"));
	opts_minarch_free(&st);
	emu_ovl_cfg_free(&cfg);
}

int main(void) {
	test_layering();
	test_game_replaces_console();
	test_lock_pruning();
	test_global_save();
	test_pergame_seed();
	test_pergame_redundancy();
	test_device_suffix();
	test_unknown_value_roundtrip();
	printf("test_opts_minarch: all tests passed\n");
	return 0;
}
