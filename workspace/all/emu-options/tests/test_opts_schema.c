// Host-side tests for ma_opts_schema.c (the options.json serializer).
// Build+run (NOTE -DEMU_OVL_MAX_VALUES must match workspace/all/emu-options/
// Makefile so the truncation round-trip tests see the device value cap):
//   cd workspace/all/emu-options/tests && mkdir -p build && \
//   cc -std=gnu99 -Wall -DEMU_OVL_MAX_VALUES=32 -o build/test_opts_schema test_opts_schema.c \
//      ../../minarch/ma_opts_schema.c ../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c \
//      -I.. -I../../common -I../../minarch -I../../minarch/libretro-common/include && \
//   ./build/test_opts_schema
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "libretro.h"
#include "ma_opts_schema.h"
#include "emu_overlay_cfg.h"
#include "cjson/cJSON.h"

#define ROOT "build/schema_test_root"

static void write_str(const char* path, const char* s) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(s, f);
	fclose(f);
}

static void test_v2_categories(void) {
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT);
	static struct retro_core_option_v2_category cats[] = {
		{"video", "Video", "Video settings"},
		{NULL, NULL, NULL},
	};
	static struct retro_core_option_v2_definition defs[] = {
		{"tc_ratio", "Aspect Ratio", "Ratio", "Pick a ratio \"quoted\" info", NULL, "video", {{"auto", "Auto"}, {"4:3", NULL}, {NULL, NULL}}, "4:3"},
		{"tc_general", "General Thing", NULL, NULL, NULL, NULL, {{"on", NULL}, {"off", NULL}, {NULL, NULL}}, "off"},
		{NULL, NULL, NULL, NULL, NULL, NULL, {{NULL, NULL}}, NULL},
	};
	static const struct retro_core_options_v2 v2 = {cats, defs};

	char* json = OptsSchema_fromV2(&v2, "testcore");
	assert(json);
	write_str(ROOT "/options.json", json);

	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/options.json") == 0);
	// General (uncategorized) first, then Video
	assert(cfg.section_count == 2);
	assert(strcmp(cfg.sections[0].name, "General") == 0);
	assert(strcmp(cfg.sections[1].name, "Video") == 0);
	EmuOvlItem* ratio = &cfg.sections[1].items[0];
	assert(ratio->type == EMU_OVL_TYPE_ENUM);
	assert(strcmp(ratio->key, "tc_ratio") == 0);
	assert(strcmp(ratio->label, "Ratio") == 0); // desc_categorized wins inside a category
	assert(ratio->value_count == 2);
	assert(strcmp(ratio->labels[0], "Auto") == 0);
	assert(strcmp(ratio->labels[1], "4:3") == 0); // label fallback = value
	assert(ratio->default_value == 1);
	assert(strstr(ratio->description, "\"quoted\"") != NULL); // JSON escaping round-trips
	assert(cfg.sections[0].items[0].default_value == 1);	  // "off"
	emu_ovl_cfg_free(&cfg);
	free(json);
}

static void test_v1_and_write_if_changed(void) {
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT);
	static struct retro_core_option_definition defs[] = {
		{"tc_a", "Option A", "info a", {{"1", NULL}, {"2", NULL}, {NULL, NULL}}, "2"},
		{"tc_noval", "Broken", NULL, {{NULL, NULL}}, NULL}, // zero values: skipped
		{NULL, NULL, NULL, {{NULL, NULL}}, NULL},
	};
	char* json = OptsSchema_fromV1(defs, "testcore");
	assert(json);
	assert(OptsSchema_writeFile(ROOT "/options.json", json) == 0);
	// unchanged content: second write is a no-op that still returns 0 and does
	// not touch the file (proves the compare-before-write path)
	struct stat before, after;
	system("touch -t 200001010000 " ROOT "/options.json");
	assert(stat(ROOT "/options.json", &before) == 0);
	assert(OptsSchema_writeFile(ROOT "/options.json", json) == 0);
	assert(stat(ROOT "/options.json", &after) == 0);
	assert(before.st_mtime == after.st_mtime);
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/options.json") == 0);
	assert(cfg.section_count == 1);
	assert(strcmp(cfg.sections[0].name, "Options") == 0);
	assert(cfg.sections[0].item_count == 1); // tc_noval skipped
	assert(cfg.sections[0].items[0].default_value == 1);
	emu_ovl_cfg_free(&cfg);
	free(json);
}

static void test_vars(void) {
	static struct retro_variable vars[] = {
		{"tc_var", "Speed Hack; disabled|enabled"},
		{NULL, NULL},
	};
	char* json = OptsSchema_fromVars(vars, "testcore");
	assert(json);
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT);
	write_str(ROOT "/options.json", json);
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/options.json") == 0);
	EmuOvlItem* it = &cfg.sections[0].items[0];
	assert(strcmp(it->label, "Speed Hack") == 0);
	assert(it->value_count == 2);
	assert(strcmp(it->svalues[1], "enabled") == 0);
	assert(it->default_value == 0); // vars carry no default; first value
	emu_ovl_cfg_free(&cfg);
	free(json);
}

// --- numeric-range detection (long label-less integer lists -> "int") -------

// fills vals[0..count-1] with canonical "%d" strings start, start+step, ...
// (pool provides the string storage); NULL-terminates the array
static void fill_numeric_values(struct retro_core_option_value* vals, char pool[][16], int count, int start, int step) {
	for (int i = 0; i < count; i++) {
		snprintf(pool[i], 16, "%d", start + i * step);
		vals[i].value = pool[i];
		vals[i].label = NULL;
	}
	vals[count].value = NULL;
	vals[count].label = NULL;
}

static EmuOvlItem* find_loaded_item(EmuOvlConfig* cfg, const char* key) {
	for (int s = 0; s < cfg->section_count; s++)
		for (int i = 0; i < cfg->sections[s].item_count; i++)
			if (strcmp(cfg->sections[s].items[i].key, key) == 0)
				return &cfg->sections[s].items[i];
	return NULL;
}

static cJSON* find_json_item(cJSON* root, const char* key) {
	cJSON* sections = cJSON_GetObjectItemCaseSensitive(root, "sections");
	cJSON* sec;
	cJSON_ArrayForEach(sec, sections) {
		cJSON* items = cJSON_GetObjectItemCaseSensitive(sec, "items");
		cJSON* it;
		cJSON_ArrayForEach(it, items) {
			cJSON* k = cJSON_GetObjectItemCaseSensitive(it, "key");
			if (cJSON_IsString(k) && k->valuestring && strcmp(k->valuestring, key) == 0)
				return it;
		}
	}
	return NULL;
}

static void test_int_range_detection(void) {
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT);

	static char pool_100[100][16], pool_neg[101][16], pool_step[40][16];
	static char pool_bad[40][16], pool_lab[40][16], pool_small[10][16];
	static struct retro_core_option_definition defs[7];
	memset(defs, 0, sizeof(defs));

	// 1) 100 values "0".."99", no labels -> int range [0..99] step 1, default 50
	defs[0].key = "tc_brightness";
	defs[0].desc = "Brightness";
	defs[0].default_value = "50";
	fill_numeric_values(defs[0].values, pool_100, 100, 0, 1);

	// 2) 101 values "-50".."50" -> int range [-50..50]; unparseable default -> min
	defs[1].key = "tc_saturation";
	defs[1].desc = "Saturation";
	defs[1].default_value = "banana";
	fill_numeric_values(defs[1].values, pool_neg, 101, -50, 1);

	// 3) 40 values "0","5",...,"195" -> int range step 5
	defs[2].key = "tc_stepped";
	defs[2].desc = "Stepped";
	defs[2].default_value = "10";
	fill_numeric_values(defs[2].values, pool_step, 40, 0, 5);

	// 4) 40 values but one non-canonical ("007") -> stays enum, truncated to 32
	defs[3].key = "tc_noncanon";
	defs[3].desc = "Non Canonical";
	defs[3].default_value = "0";
	fill_numeric_values(defs[3].values, pool_bad, 40, 0, 1);
	snprintf(pool_bad[7], 16, "007");

	// 5) 40 numeric values but one custom label -> stays enum, truncated to 32
	defs[4].key = "tc_labeled";
	defs[4].desc = "Labeled";
	defs[4].default_value = "0";
	fill_numeric_values(defs[4].values, pool_lab, 40, 0, 1);
	defs[4].values[0].label = "Off";

	// 6) 10-value numeric list -> below threshold, stays enum untouched
	defs[5].key = "tc_small";
	defs[5].desc = "Small";
	defs[5].default_value = "3";
	fill_numeric_values(defs[5].values, pool_small, 10, 0, 1);

	char* json = OptsSchema_fromV1(defs, "testcore");
	assert(json);
	write_str(ROOT "/options.json", json);

	// JSON-side checks (serializer contract, independent of parser caps)
	cJSON* root = cJSON_Parse(json);
	assert(root);
	cJSON* jit;
	jit = find_json_item(root, "tc_brightness");
	assert(jit);
	assert(strcmp(cJSON_GetObjectItemCaseSensitive(jit, "type")->valuestring, "int") == 0);
	assert(cJSON_GetObjectItemCaseSensitive(jit, "min")->valueint == 0);
	assert(cJSON_GetObjectItemCaseSensitive(jit, "max")->valueint == 99);
	assert(cJSON_GetObjectItemCaseSensitive(jit, "step")->valueint == 1);
	assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(jit, "default")));
	assert(cJSON_GetObjectItemCaseSensitive(jit, "default")->valueint == 50);
	assert(cJSON_GetObjectItemCaseSensitive(jit, "values") == NULL); // no value list on int items
	jit = find_json_item(root, "tc_noncanon");
	assert(jit);
	assert(strcmp(cJSON_GetObjectItemCaseSensitive(jit, "type")->valuestring, "enum") == 0);
	assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(jit, "values")) == 32); // existing truncation
	jit = find_json_item(root, "tc_labeled");
	assert(jit);
	assert(strcmp(cJSON_GetObjectItemCaseSensitive(jit, "type")->valuestring, "enum") == 0);
	assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(jit, "values")) == 32);
	cJSON_Delete(root);

	// editor-side checks: the emitted int items load end-to-end
	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/options.json") == 0);

	EmuOvlItem* it = find_loaded_item(&cfg, "tc_brightness");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_INT);
	assert(it->int_min == 0);
	assert(it->int_max == 99);
	assert(it->int_step == 1);
	assert(it->default_value == 50); // matches the def's default string

	it = find_loaded_item(&cfg, "tc_saturation");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_INT);
	assert(it->int_min == -50);
	assert(it->int_max == 50);
	assert(it->int_step == 1);
	assert(it->default_value == -50); // unparseable default falls back to min

	it = find_loaded_item(&cfg, "tc_stepped");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_INT);
	assert(it->int_min == 0);
	assert(it->int_max == 195);
	assert(it->int_step == 5);
	assert(it->default_value == 10);

	it = find_loaded_item(&cfg, "tc_noncanon");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_ENUM);

	it = find_loaded_item(&cfg, "tc_labeled");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_ENUM);

	it = find_loaded_item(&cfg, "tc_small");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_ENUM); // below threshold: behavior unchanged
	assert(it->value_count == 10);
	assert(it->default_value == 3); // index of "3"

	emu_ovl_cfg_free(&cfg);
	free(json);

	// vars path routes through the same add_item: 101-value var -> int
	static char varbuf[1024];
	int off = snprintf(varbuf, sizeof(varbuf), "Color Depth; ");
	for (int i = 0; i <= 100; i++)
		off += snprintf(varbuf + off, sizeof(varbuf) - off, i ? "|%d" : "%d", i);
	static struct retro_variable vars[] = {
		{"tc_var_range", NULL},
		{NULL, NULL},
	};
	vars[0].value = varbuf;
	json = OptsSchema_fromVars(vars, "testcore");
	assert(json);
	write_str(ROOT "/options.json", json);
	assert(emu_ovl_cfg_load(&cfg, ROOT "/options.json") == 0);
	it = find_loaded_item(&cfg, "tc_var_range");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_INT);
	assert(it->int_min == 0);
	assert(it->int_max == 100);
	assert(it->int_step == 1);
	assert(it->default_value == 0); // vars carry no default -> min
	emu_ovl_cfg_free(&cfg);
	free(json);
}

// Truncated enums must keep the core's default representable: when the
// default sits past the OPTS_SCHEMA_MAX_VALUES cut it takes over the last
// kept slot (value + label), otherwise parse_item's default lookup misses,
// a fresh editor shows values[0], and a full per-game snapshot writes that
// wrong value into the game cfg. Real case: vice_vicii_color_brightness,
// 100 values "20".."2000" step 20 with % labels, default "1000".
static void test_truncation_preserves_default(void) {
	system("rm -rf " ROOT);
	system("mkdir -p " ROOT);

	static char valpool[2][100][16], labpool[2][100][16];
	static struct retro_core_option_definition defs[3];
	memset(defs, 0, sizeof(defs));
	for (int d = 0; d < 2; d++) {
		for (int i = 0; i < 100; i++) {
			snprintf(valpool[d][i], 16, "%d", (i + 1) * 20);
			snprintf(labpool[d][i], 16, "%d%%", i + 1);
			defs[d].values[i].value = valpool[d][i];
			defs[d].values[i].label = labpool[d][i]; // real labels: no int conversion
		}
	}
	defs[0].key = "tc_bright_pct";
	defs[0].desc = "Brightness";
	defs[0].default_value = "1000"; // value #50 — past the 32-value cut
	defs[1].key = "tc_bright_kept";
	defs[1].desc = "Brightness Kept";
	defs[1].default_value = "20"; // already among the kept head: unchanged

	char* json = OptsSchema_fromV1(defs, "testcore");
	assert(json);
	write_str(ROOT "/options.json", json);

	EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/options.json") == 0);

	EmuOvlItem* it = find_loaded_item(&cfg, "tc_bright_pct");
	assert(it);
	assert(it->type == EMU_OVL_TYPE_ENUM);
	assert(it->value_count == 32);
	int di = it->default_value;
	assert(di >= 0 && di < it->value_count);
	assert(strcmp(it->svalues[di], "1000") == 0); // default representable
	assert(strcmp(it->labels[di], "50%") == 0);	  // and its label survives
	assert(di == 31);							  // in the last kept slot
	// the head of the list is still the original head
	assert(strcmp(it->svalues[0], "20") == 0);
	assert(strcmp(it->labels[0], "1%") == 0);
	assert(strcmp(it->svalues[30], "620") == 0);

	// default already among the kept values: plain truncation, tail unchanged
	it = find_loaded_item(&cfg, "tc_bright_kept");
	assert(it);
	assert(it->value_count == 32);
	assert(it->default_value == 0);
	assert(strcmp(it->svalues[31], "640") == 0);
	assert(strcmp(it->labels[31], "32%") == 0);

	emu_ovl_cfg_free(&cfg);
	free(json);
}

int main(void) {
	test_v2_categories();
	test_v1_and_write_if_changed();
	test_vars();
	test_int_range_detection();
	test_truncation_preserves_default();
	printf("test_opts_schema: all tests passed\n");
	return 0;
}
