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

	// value lists have no cap: intern grows the heap arrays arbitrarily far
	// (well past the old 32-entry fixed-array limit)
	char name[32];
	for (int i = 0; i < 400; i++) {
		snprintf(name, sizeof(name), "grown-%d", i);
		assert(emu_ovl_cfg_enum_intern(it, name) == 4 + i);
	}
	assert(it->value_count == 404);
	assert(strcmp(it->svalues[403], "grown-399") == 0);
	assert(emu_ovl_cfg_enum_intern(it, "grown-0") == 4); // still idempotent
	assert(strcmp(it->svalues[0], "auto") == 0);		 // originals untouched
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

// mupen64plus re-serializes mupen64plus.cfg after a play session (a video
// plugin's ConfigSaveSection rewrites the whole file even under
// --nosaveoptions) and wraps string values in double quotes, e.g.
// VideoPlugin = "rice". launch.sh's awk resolver already strips those quotes;
// the INI reader must agree, or the editor would show the schema default while
// the launcher runs the stored plugin. parse_item_value strips ONE surrounding
// pair of quotes before dispatch, so read_ini and the parse wrapper both cope.
static void test_read_ini_quoted(void) {
	// A quoted enum value resolves to the same index as the unquoted form.
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	write_file(ROOT "/emu.cfg",
			   "[config]\n"
			   "tc_ratio = \"16:9\"\n");
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg, ROOT "/emu.cfg") == 0);
	assert(cfg.sections[0].items[0].current_value == 2); // "16:9" -> index 2
	emu_ovl_cfg_free(&cfg);

	// The unquoted form still works (regression guard alongside the quoted one).
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	write_file(ROOT "/emu.cfg",
			   "[config]\n"
			   "tc_ratio = 16:9\n");
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg, ROOT "/emu.cfg") == 0);
	assert(cfg.sections[0].items[0].current_value == 2);
	emu_ovl_cfg_free(&cfg);

	// Empty quotes "" must not crash and must leave the schema default (index 1
	// = "4:3") in place: after stripping, "" matches no enum entry.
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	write_file(ROOT "/emu.cfg",
			   "[config]\n"
			   "tc_ratio = \"\"\n");
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg, ROOT "/emu.cfg") == 0);
	assert(cfg.sections[0].items[0].current_value == 1); // default kept
	emu_ovl_cfg_free(&cfg);

	// A lone, unpaired quote is not stripped (needs a surrounding pair), must
	// not crash, and matches no enum entry -> default kept.
	reset_root();
	write_file(ROOT "/schema.json", SCHEMA);
	write_file(ROOT "/emu.cfg",
			   "[config]\n"
			   "tc_ratio = \"\n");
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg, ROOT "/emu.cfg") == 0);
	assert(cfg.sections[0].items[0].current_value == 1); // default kept
	emu_ovl_cfg_free(&cfg);

	// The public parse wrapper agrees: a quoted enum value resolves, and a
	// garbage/empty-quote value fails (no intern, no crash).
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	{
		EmuOvlItem* it = &cfg.sections[0].items[0];
		int v = -1;
		assert(emu_ovl_cfg_parse_value(it, "\"16:9\"", &v) && v == 2);
		assert(!emu_ovl_cfg_parse_value(it, "\"\"", &v));
		assert(!emu_ovl_cfg_parse_value(it, "\"", &v));
	}
	emu_ovl_cfg_free(&cfg);
}

// Section visibility gate (schema "visible_when"). A gate item (enum) plus a
// numeric gate (cycle item), an ungated section, a section referencing a
// missing key, and a section with an unparsable gate value. Visibility must
// track the referenced item's STAGED value, and every unresolvable reference
// must fail OPEN (visible) so a schema typo can never hide settings.
static const char* VIS_SCHEMA =
	"{\n"
	"  \"config_section\": \"Main\",\n"
	"  \"sections\": [\n"
	"    { \"name\": \"Gate\", \"ini_section\": \"Ctl\", \"items\": [\n"
	"      { \"key\": \"plugin\", \"label\": \"Plugin\", \"type\": \"enum\",\n"
	"        \"values\": [\"alpha\", \"beta\"], \"labels\": [\"Alpha\", \"Beta\"],\n"
	"        \"default\": \"alpha\" },\n"
	"      { \"key\": \"level\", \"label\": \"Level\", \"type\": \"cycle\",\n"
	"        \"values\": [0, 1, 2], \"labels\": [\"Lo\", \"Mid\", \"Hi\"],\n"
	"        \"default\": 1 }\n"
	"    ]},\n"
	"    { \"name\": \"AlphaOnly\", \"ini_section\": \"SecA\",\n"
	"      \"visible_when\": {\"ini_section\": \"Ctl\", \"key\": \"plugin\", \"value\": \"alpha\"},\n"
	"      \"items\": [ { \"key\": \"a1\", \"label\": \"A1\", \"type\": \"bool\", \"default\": false } ] },\n"
	"    { \"name\": \"LevelTwo\", \"ini_section\": \"SecB\",\n"
	"      \"visible_when\": {\"ini_section\": \"Ctl\", \"key\": \"level\", \"value\": 2},\n"
	"      \"items\": [ { \"key\": \"b1\", \"label\": \"B1\", \"type\": \"bool\", \"default\": false } ] },\n"
	"    { \"name\": \"Always\", \"ini_section\": \"SecC\",\n"
	"      \"items\": [ { \"key\": \"c1\", \"label\": \"C1\", \"type\": \"bool\", \"default\": false } ] },\n"
	"    { \"name\": \"MissingRef\", \"ini_section\": \"SecD\",\n"
	"      \"visible_when\": {\"ini_section\": \"Ctl\", \"key\": \"nope\", \"value\": \"x\"},\n"
	"      \"items\": [ { \"key\": \"d1\", \"label\": \"D1\", \"type\": \"bool\", \"default\": false } ] },\n"
	"    { \"name\": \"BadValue\", \"ini_section\": \"SecE\",\n"
	"      \"visible_when\": {\"ini_section\": \"Ctl\", \"key\": \"level\", \"value\": \"not-a-number\"},\n"
	"      \"items\": [ { \"key\": \"e1\", \"label\": \"E1\", \"type\": \"bool\", \"default\": false } ] }\n"
	"  ]\n"
	"}\n";

static void test_visible_when(void) {
	reset_root();
	write_file(ROOT "/schema.json", VIS_SCHEMA);
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(cfg.section_count == 6);

	// Parsed fields: string gate, numeric gate stored as "%d", ungated empty.
	assert(strcmp(cfg.sections[1].vis_ini_section, "Ctl") == 0);
	assert(strcmp(cfg.sections[1].vis_key, "plugin") == 0);
	assert(strcmp(cfg.sections[1].vis_value, "alpha") == 0);
	assert(strcmp(cfg.sections[2].vis_value, "2") == 0); // JSON number -> "%d"
	assert(cfg.sections[3].vis_key[0] == '\0');			 // no visible_when

	EmuOvlItem* plugin = emu_ovl_cfg_find_item(&cfg, "Ctl", "plugin", NULL, NULL);
	EmuOvlItem* level = emu_ovl_cfg_find_item(&cfg, "Ctl", "level", NULL, NULL);
	assert(plugin && level);
	assert(plugin->staged_value == 0); // "alpha"
	assert(level->staged_value == 1);

	// Ungated sections and unresolvable references are always visible.
	assert(emu_ovl_cfg_section_visible(&cfg, 0) == true); // ungated Gate
	assert(emu_ovl_cfg_section_visible(&cfg, 3) == true); // ungated Always
	assert(emu_ovl_cfg_section_visible(&cfg, 4) == true); // missing key -> open
	assert(emu_ovl_cfg_section_visible(&cfg, 5) == true); // unparsable -> open

	// String gate follows the STAGED value both ways.
	assert(emu_ovl_cfg_section_visible(&cfg, 1) == true); // plugin==alpha
	plugin->staged_value = 1;							  // "beta"
	assert(emu_ovl_cfg_section_visible(&cfg, 1) == false);
	plugin->staged_value = 0; // back to "alpha"
	assert(emu_ovl_cfg_section_visible(&cfg, 1) == true);

	// Numeric gate on a cycle item.
	assert(emu_ovl_cfg_section_visible(&cfg, 2) == false); // level==1, gate==2
	level->staged_value = 2;
	assert(emu_ovl_cfg_section_visible(&cfg, 2) == true);

	// Out-of-range index is treated as visible, never a crash.
	assert(emu_ovl_cfg_section_visible(&cfg, 99) == true);
	assert(emu_ovl_cfg_section_visible(&cfg, -1) == true);

	emu_ovl_cfg_free(&cfg);
}

// Two ini sections, the second absent from the file until an item in it is
// staged: the shape the standalone N64 schema had ([NxRedux] + [Video-Rice]).
static const char* APPEND_SCHEMA =
	"{\n"
	"  \"config_section\": \"NxRedux\",\n"
	"  \"sections\": [\n"
	"    { \"name\": \"Plugin\", \"ini_section\": \"NxRedux\", \"items\": [\n"
	"      { \"key\": \"VideoPlugin\", \"label\": \"Video Plugin\", \"type\": \"enum\",\n"
	"        \"values\": [\"gliden64\", \"rice\"], \"default\": \"gliden64\" } ] },\n"
	"    { \"name\": \"Rendering\", \"ini_section\": \"Video-Rice\", \"items\": [\n"
	"      { \"key\": \"AspectRatio\", \"label\": \"Aspect Ratio\", \"type\": \"cycle\",\n"
	"        \"values\": [0, 1, 2, 3], \"default\": 1 } ] }\n"
	"  ]\n"
	"}\n";

// Slurp a whole file into a caller buffer (NUL-terminated); asserts it fit.
static void slurp(const char* path, char* buf, size_t size) {
	FILE* f = fopen(path, "r");
	assert(f);
	size_t n = fread(buf, 1, size - 1, f);
	assert(!ferror(f) && feof(f)); // whole file fit in the buffer
	buf[n] = '\0';
	fclose(f);
}

// emu_ovl_cfg_write_ini must append a [section] the file never had (here
// [Video-Rice], absent until the editor stages an item in it and writes).
static void test_write_ini_appends_missing_section(void) {
	reset_root();
	write_file(ROOT "/mupen64plus.cfg",
			   "[NxRedux]\n"
			   "VideoPlugin = rice\n");
	EmuOvlConfig cfg;
	write_file(ROOT "/schema.json", APPEND_SCHEMA);
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);
	assert(emu_ovl_cfg_read_ini(&cfg, ROOT "/mupen64plus.cfg") == 0);

	EmuOvlItem* ar = emu_ovl_cfg_find_item(&cfg, "Video-Rice", "AspectRatio", NULL, NULL);
	assert(ar);
	ar->staged_value = 0; // stretch; differs from default (1)
	ar->dirty = true;
	assert(emu_ovl_cfg_write_ini(&cfg, ROOT "/mupen64plus.cfg") == 0);

	char buf[4096];
	slurp(ROOT "/mupen64plus.cfg", buf, sizeof(buf));
	assert(strstr(buf, "[NxRedux]"));		   // original section intact
	assert(strstr(buf, "VideoPlugin = rice")); // original key intact
	assert(strstr(buf, "[Video-Rice]"));	   // section appended
	assert(strstr(buf, "AspectRatio = 0"));	   // with the staged key
	emu_ovl_cfg_free(&cfg);
}

int main(void) {
	test_load_and_defaults();
	test_parse_format_intern();
	test_read_ini_enum();
	test_read_ini_quoted();
	test_visible_when();
	test_write_ini_appends_missing_section();
	printf("test_emu_ovl_enum: all tests passed\n");
	return 0;
}
