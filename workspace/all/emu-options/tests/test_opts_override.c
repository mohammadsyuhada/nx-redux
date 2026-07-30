// Host-side tests for opts_override.c
// Build+run:
//   cd workspace/all/emu-options/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -o build/test_opts_override test_opts_override.c \
//      ../opts_override.c ../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c \
//      -I.. -I../../common && \
//   ./build/test_opts_override
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_overlay_cfg.h"
#include "opts_override.h"

#define ROOT "build/opts_test_root"

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

static int file_exists(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

// Minimal schema mirroring the real overlay_settings.json shapes:
// global config_section "config"; one bool in [config], one bool with
// ini_section "achievements", one cycle in ini_section "network".
// Key names and value shapes match parse_item()/parse_section() exactly:
// bool defaults are JSON booleans (json_get_bool ignores numbers), cycle
// defaults are JSON numbers.
static const char* SCHEMA =
	"{\n"
	"  \"emulator\": \"flycast\",\n"
	"  \"config_file\": \"emu.cfg\",\n"
	"  \"config_section\": \"config\",\n"
	"  \"options_hint\": \"Restart game to apply changes\",\n"
	"  \"save_state\": true,\n"
	"  \"load_state\": true,\n"
	"  \"sections\": [\n"
	"    {\"name\": \"Video\", \"items\": [\n"
	"      {\"key\": \"rend.WideScreen\", \"label\": \"Widescreen\", \"type\": \"bool\", \"default\": false}\n"
	"    ]},\n"
	"    {\"name\": \"RetroAchievements\", \"ini_section\": \"achievements\", \"items\": [\n"
	"      {\"key\": \"Enabled\", \"label\": \"Enabled\", \"type\": \"bool\", \"default\": false}\n"
	"    ]},\n"
	"    {\"name\": \"Netplay\", \"ini_section\": \"network\", \"items\": [\n"
	"      {\"key\": \"GGPODelay\", \"label\": \"Input Delay\", \"type\": \"cycle\",\n"
	"       \"values\": [0,1,2,3,4,5], \"labels\": [\"0\",\"1\",\"2\",\"3\",\"4\",\"5\"], \"default\": 0}\n"
	"    ]}\n"
	"  ]\n"
	"}\n";

static const char* GLOBAL_INI =
	"[config]\n"
	"rend.WideScreen = no\n"
	"\n"
	"[achievements]\n"
	"Enabled = yes\n"
	"\n"
	"[network]\n"
	"GGPODelay = 0\n";

static void setup(EmuOvlConfig* cfg, OptsOverrideState* st) {
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	write_file(ROOT "/emu.cfg", GLOBAL_INI);
	memset(cfg, 0, sizeof(*cfg));
	assert(emu_ovl_cfg_load(cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(cfg, ROOT "/emu.cfg") == 0);
	opts_snapshot_globals(cfg, st);
}

static EmuOvlItem* item(EmuOvlConfig* cfg, const char* sec, const char* key) {
	EmuOvlItem* it = emu_ovl_cfg_find_item(cfg, sec, key, NULL, NULL);
	assert(it);
	return it;
}

// The schema must actually have loaded the way the parser reads it —
// otherwise every other test below would pass against an empty config.
static void test_schema_loads(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	assert(cfg.section_count == 3);
	assert(strcmp(cfg.config_section, "config") == 0);
	// resolved section names: no ini_section falls back to config_section
	assert(strcmp(emu_ovl_cfg_section_name(&cfg, 0), "config") == 0);
	assert(strcmp(emu_ovl_cfg_section_name(&cfg, 1), "achievements") == 0);
	assert(strcmp(emu_ovl_cfg_section_name(&cfg, 2), "network") == 0);
	// out-of-range index falls back to the global section
	assert(strcmp(emu_ovl_cfg_section_name(&cfg, 99), "config") == 0);
	// unknown section / unknown key must not match
	assert(!emu_ovl_cfg_find_item(&cfg, "nope", "rend.WideScreen", NULL, NULL));
	assert(!emu_ovl_cfg_find_item(&cfg, "config", "nope", NULL, NULL));
	// the global INI seeded current_value for every item
	assert(item(&cfg, "config", "rend.WideScreen")->current_value == 0);
	assert(item(&cfg, "achievements", "Enabled")->current_value == 1);
	assert(item(&cfg, "network", "GGPODelay")->current_value == 0);
	// types survived the JSON round-trip
	assert(item(&cfg, "achievements", "Enabled")->type == EMU_OVL_TYPE_BOOL);
	assert(item(&cfg, "network", "GGPODelay")->type == EMU_OVL_TYPE_CYCLE);
	assert(item(&cfg, "network", "GGPODelay")->value_count == 6);
	emu_ovl_cfg_free(&cfg);
	printf("ok schema_loads\n");
}

// parse_value/format_value must agree with each other and with the INI
// dialect emu.cfg + launch.sh's sed patterns expect.
static void test_parse_and_format_value(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	EmuOvlItem* b = item(&cfg, "config", "rend.WideScreen");
	EmuOvlItem* c = item(&cfg, "network", "GGPODelay");
	int v = -1;
	char out[64];

	// bool: flycast's yes/no and the overlay's own True/False both parse
	assert(emu_ovl_cfg_parse_value(b, "yes", &v) && v == 1);
	assert(emu_ovl_cfg_parse_value(b, "True", &v) && v == 1);
	assert(emu_ovl_cfg_parse_value(b, "1", &v) && v == 1);
	assert(emu_ovl_cfg_parse_value(b, "no", &v) && v == 0);
	assert(emu_ovl_cfg_parse_value(b, "False", &v) && v == 0);
	assert(!emu_ovl_cfg_parse_value(b, "", &v));

	// numeric: garbage is rejected rather than silently becoming 0
	assert(emu_ovl_cfg_parse_value(c, "3", &v) && v == 3);
	assert(emu_ovl_cfg_parse_value(c, "-2", &v) && v == -2);
	assert(!emu_ovl_cfg_parse_value(c, "abc", &v));
	assert(!emu_ovl_cfg_parse_value(c, "", &v));
	assert(!emu_ovl_cfg_parse_value(c, "3junk", &v));

	// format matches what emu_ovl_cfg_write_ini has always written
	emu_ovl_cfg_format_value(b, 1, out, sizeof(out));
	assert(strcmp(out, "True") == 0);
	emu_ovl_cfg_format_value(b, 0, out, sizeof(out));
	assert(strcmp(out, "False") == 0);
	emu_ovl_cfg_format_value(c, 4, out, sizeof(out));
	assert(strcmp(out, "4") == 0);

	// float_scale round-trips through both halves
	c->float_scale = 100;
	emu_ovl_cfg_format_value(c, 150, out, sizeof(out));
	assert(strcmp(out, "1.500000") == 0);
	assert(emu_ovl_cfg_parse_value(c, "1.5", &v) && v == 150);
	c->float_scale = 0;

	emu_ovl_cfg_free(&cfg);
	printf("ok parse_and_format_value\n");
}

static void test_no_changes_no_file(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	assert(opts_write_override(&cfg, &st, ROOT "/game.cfg") == 0);
	assert(!file_exists(ROOT "/game.cfg"));
	emu_ovl_cfg_free(&cfg);
	printf("ok no_changes_no_file\n");
}

static void test_write_one_override(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	item(&cfg, "config", "rend.WideScreen")->staged_value = 1;
	assert(opts_write_override(&cfg, &st, ROOT "/game.cfg") == 1);
	assert(file_exists(ROOT "/game.cfg"));
	// re-read into a fresh config to prove the file round-trips
	EmuOvlConfig cfg2;
	OptsOverrideState st2;
	memset(&cfg2, 0, sizeof(cfg2));
	assert(emu_ovl_cfg_load(&cfg2, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg2, ROOT "/emu.cfg") == 0);
	opts_snapshot_globals(&cfg2, &st2);
	assert(opts_read_override(&cfg2, &st2, ROOT "/game.cfg") == 1);
	assert(item(&cfg2, "config", "rend.WideScreen")->current_value == 1);
	assert(item(&cfg2, "config", "rend.WideScreen")->staged_value == 1);
	int s = -1, i = -1;
	emu_ovl_cfg_find_item(&cfg2, "config", "rend.WideScreen", &s, &i);
	assert(st2.overridden[s][i]);
	// the global baseline is untouched by the override layer
	assert(st2.global_value[s][i] == 0);
	// untouched items are not marked
	emu_ovl_cfg_find_item(&cfg2, "achievements", "Enabled", &s, &i);
	assert(!st2.overridden[s][i]);
	emu_ovl_cfg_free(&cfg2);
	emu_ovl_cfg_free(&cfg);
	printf("ok write_one_override\n");
}

static void test_revert_to_global_unlinks(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	item(&cfg, "config", "rend.WideScreen")->staged_value = 1;
	assert(opts_write_override(&cfg, &st, ROOT "/game.cfg") == 1);
	opts_commit(&cfg, &st);
	// set it back to the global value -> file must disappear
	item(&cfg, "config", "rend.WideScreen")->staged_value = 0;
	assert(opts_write_override(&cfg, &st, ROOT "/game.cfg") == 0);
	assert(!file_exists(ROOT "/game.cfg"));
	// ...and the override marker clears on the next commit
	opts_commit(&cfg, &st);
	int s = -1, i = -1;
	emu_ovl_cfg_find_item(&cfg, "config", "rend.WideScreen", &s, &i);
	assert(!st.overridden[s][i]);
	emu_ovl_cfg_free(&cfg);
	printf("ok revert_to_global_unlinks\n");
}

static void test_ini_section_resolution(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	item(&cfg, "achievements", "Enabled")->staged_value = 0; // global is yes(1)
	item(&cfg, "network", "GGPODelay")->staged_value = 3;
	assert(opts_write_override(&cfg, &st, ROOT "/game.cfg") == 2);
	// file must use the RESOLVED ini_section names
	FILE* f = fopen(ROOT "/game.cfg", "r");
	assert(f);
	char buf[512] = {0};
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	assert(strstr(buf, "[achievements]"));
	assert(strstr(buf, "[network]"));
	assert(strstr(buf, "GGPODelay = 3"));
	assert(strstr(buf, "Enabled = False"));
	assert(!strstr(buf, "[config]")); // no unchanged section leaks in
	emu_ovl_cfg_free(&cfg);
	printf("ok ini_section_resolution\n");
}

static void test_malformed_override_ignored(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	write_file(ROOT "/game.cfg", "not an ini at all\n= = =\n[unknown]\nfoo = bar\n");
	assert(opts_read_override(&cfg, &st, ROOT "/game.cfg") == 0);
	assert(item(&cfg, "config", "rend.WideScreen")->current_value == 0);
	emu_ovl_cfg_free(&cfg);
	printf("ok malformed_override_ignored\n");
}

// A known key carrying an unparseable value must leave the global value in
// place instead of collapsing it to 0.
static void test_unparseable_value_skipped(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	write_file(ROOT "/game.cfg", "[network]\nGGPODelay = wat\n");
	assert(opts_read_override(&cfg, &st, ROOT "/game.cfg") == 0);
	int s = -1, i = -1;
	emu_ovl_cfg_find_item(&cfg, "network", "GGPODelay", &s, &i);
	assert(!st.overridden[s][i]);
	emu_ovl_cfg_free(&cfg);
	printf("ok unparseable_value_skipped\n");
}

// Comments, blank lines and pre-section keys are tolerated the same way
// emu.cfg's own reader tolerates them.
static void test_comments_and_blank_lines(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	write_file(ROOT "/game.cfg",
			   "# a comment\n"
			   "; another\n"
			   "GGPODelay = 5\n" // before any [section] -> ignored
			   "\n"
			   "[network]\n"
			   "  GGPODelay  =  2  \n");
	assert(opts_read_override(&cfg, &st, ROOT "/game.cfg") == 1);
	assert(item(&cfg, "network", "GGPODelay")->current_value == 2);
	emu_ovl_cfg_free(&cfg);
	printf("ok comments_and_blank_lines\n");
}

static void test_missing_override_noop(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	assert(opts_read_override(&cfg, &st, ROOT "/nope.cfg") == 0);
	emu_ovl_cfg_free(&cfg);
	printf("ok missing_override_noop\n");
}

static void test_commit_refreshes_marks(void) {
	EmuOvlConfig cfg;
	OptsOverrideState st;
	setup(&cfg, &st);
	item(&cfg, "network", "GGPODelay")->staged_value = 2;
	assert(opts_write_override(&cfg, &st, ROOT "/game.cfg") == 1);
	opts_commit(&cfg, &st);
	int s = -1, i = -1;
	emu_ovl_cfg_find_item(&cfg, "network", "GGPODelay", &s, &i);
	assert(st.overridden[s][i]);
	assert(item(&cfg, "network", "GGPODelay")->current_value == 2);
	// commit must leave the global baseline alone so a later revert still
	// compares against emu.cfg, not against the committed override
	assert(st.global_value[s][i] == 0);
	emu_ovl_cfg_free(&cfg);
	printf("ok commit_refreshes_marks\n");
}

int main(void) {
	test_schema_loads();
	test_parse_and_format_value();
	test_no_changes_no_file();
	test_write_one_override();
	test_revert_to_global_unlinks();
	test_ini_section_resolution();
	test_malformed_override_ignored();
	test_unparseable_value_skipped();
	test_comments_and_blank_lines();
	test_missing_override_noop();
	test_commit_refreshes_marks();
	printf("all opts_override tests passed\n");
	return 0;
}
