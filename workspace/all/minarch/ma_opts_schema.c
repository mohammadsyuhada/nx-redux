// Serializers for the pre-launch options schema cache (options.json).
// IMPORTANT: keep this file free of minarch globals (no ma_internal.h) — it is
// also compiled host-side by workspace/all/emu-options/tests/test_opts_schema.c
// and must link against nothing but cJSON.
#include "ma_opts_schema.h"

#include "cjson/cJSON.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////

// section manager: appends items to a named section, chunking into "<name>",
// "<name> 2", "<name> 3"... every OPTS_SCHEMA_MAX_ITEMS_PER_SECTION items and
// refusing beyond OPTS_SCHEMA_MAX_SECTIONS total sections. Chunks are created
// lazily so empty sections are never emitted.
typedef struct {
	cJSON* sections; // root "sections" array (borrowed from the root object)
	int total;		 // sections emitted so far, across all names
} SchemaSections;

typedef struct {
	SchemaSections* all;
	const char* name;
	cJSON* items; // current chunk's items array, NULL until the first item lands
	int in_chunk;
	int chunk; // 1-based; >1 gets the " N" suffix
} SectionWriter;

static SectionWriter section_begin(SchemaSections* all, const char* name) {
	SectionWriter w = {all, name, NULL, 0, 0};
	return w;
}

static int values_count(const struct retro_core_option_value* values) {
	int n = 0;
	if (values)
		while (values[n].value)
			n++;
	return n;
}

static cJSON* section_items(SectionWriter* w) {
	if (!w->items || w->in_chunk >= OPTS_SCHEMA_MAX_ITEMS_PER_SECTION) {
		if (w->all->total >= OPTS_SCHEMA_MAX_SECTIONS) {
			fprintf(stderr, "opts-schema: section cap (%i) reached, dropping items for \"%s\"\n", OPTS_SCHEMA_MAX_SECTIONS, w->name);
			return NULL;
		}
		char name[256];
		w->chunk += 1;
		if (w->chunk > 1)
			snprintf(name, sizeof(name), "%s %i", w->name, w->chunk);
		else
			snprintf(name, sizeof(name), "%s", w->name);
		cJSON* sec = cJSON_CreateObject();
		cJSON_AddStringToObject(sec, "name", name);
		w->items = cJSON_AddArrayToObject(sec, "items");
		cJSON_AddItemToArray(w->all->sections, sec);
		w->all->total += 1;
		w->in_chunk = 0;
	}
	w->in_chunk += 1;
	return w->items;
}

// Whole string is the canonical "%d" rendering of an int (base-10 strtol
// consumes it all AND printing the result back reproduces it byte-for-byte).
// Canonical form is load-bearing: minarch matches cfg values by exact strcmp
// against the registered strings (Option_getValueIndex, ma_options.c), so the
// editor's "%d" formatter must regenerate the core's exact strings. Rejects
// "007", "+5", "1.5", "" and anything outside int range.
static int parse_canonical_int(const char* s, int* out) {
	if (!s || !*s)
		return 0;
	char* end = NULL;
	long v = strtol(s, &end, 10);
	if (end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
		return 0;
	char canon[16];
	snprintf(canon, sizeof(canon), "%d", (int)v);
	if (strcmp(canon, s) != 0)
		return 0;
	*out = (int)v;
	return 1;
}

// Detect a long, label-less, strictly-ascending arithmetic sequence of
// canonically-formatted integers. Only lists that would otherwise be
// truncated (count > OPTS_SCHEMA_MAX_VALUES) qualify — short numeric lists
// stay enums so their UI behavior is unchanged. Returns 1 and fills
// min/max/step on success.
static int detect_int_range(const struct retro_core_option_value* values, int count, int* out_min, int* out_max, int* out_step) {
	if (count <= OPTS_SCHEMA_MAX_VALUES)
		return 0;
	int min = 0, prev = 0, step = 0;
	for (int i = 0; i < count; i++) {
		int v;
		if (!parse_canonical_int(values[i].value, &v))
			return 0;
		// a real label would be silently discarded by the conversion
		if (values[i].label && strcmp(values[i].label, values[i].value) != 0)
			return 0;
		if (i == 0) {
			min = v;
		} else {
			long diff = (long)v - (long)prev; // both fit int, so diff fits long
			if (diff < 1 || diff > INT_MAX)
				return 0; // not strictly ascending (or absurd step)
			if (i == 1)
				step = (int)diff;
			else if (diff != step)
				return 0; // not arithmetic
		}
		prev = v;
	}
	*out_min = min;
	*out_max = prev;
	*out_step = step;
	return 1;
}

// one item in the dialect emu_overlay_cfg.c parses: string-valued "enum" with
// values[]/labels[] and a string default — or, for long canonical numeric
// sequences, an "int" range item (min/max/step) that sidesteps the
// OPTS_SCHEMA_MAX_VALUES truncation entirely
static void add_item(cJSON* items_arr, const char* key, const char* label, const char* info, const struct retro_core_option_value* values, const char* default_value) {
	int count = values_count(values);
	if (count <= 0)
		return; // callers pre-check; defensive

	cJSON* item = cJSON_CreateObject();
	cJSON_AddStringToObject(item, "key", key);
	cJSON_AddStringToObject(item, "label", label ? label : key);
	if (info)
		cJSON_AddStringToObject(item, "description", info);

	int min, max, step;
	if (detect_int_range(values, count, &min, &max, &step)) {
		int def;
		if (!default_value || !parse_canonical_int(default_value, &def) || def < min || def > max)
			def = min;
		fprintf(stderr, "opts-schema: option \"%s\" emitted as int range [%i..%i] step %i (%i values)\n", key, min, max, step, count);
		cJSON_AddStringToObject(item, "type", "int");
		cJSON_AddNumberToObject(item, "min", min);
		cJSON_AddNumberToObject(item, "max", max);
		cJSON_AddNumberToObject(item, "step", step);
		cJSON_AddNumberToObject(item, "default", def);
		cJSON_AddItemToArray(items_arr, item);
		return;
	}

	// When truncating, keep the core's default representable: if default_value
	// sits past the cut, it takes over the LAST kept slot (value + label).
	// Otherwise parse_item's default lookup misses, a fresh editor shows
	// values[0], and a full per-game snapshot writes that wrong value into the
	// game cfg — a silent behavior change for an untouched option.
	const struct retro_core_option_value* def_rescue = NULL;
	if (count > OPTS_SCHEMA_MAX_VALUES) {
		if (default_value) {
			int kept = 0;
			for (int i = 0; i < OPTS_SCHEMA_MAX_VALUES; i++) {
				if (strcmp(values[i].value, default_value) == 0) {
					kept = 1;
					break;
				}
			}
			if (!kept) {
				for (int i = OPTS_SCHEMA_MAX_VALUES; i < count; i++) {
					if (strcmp(values[i].value, default_value) == 0) {
						def_rescue = &values[i];
						break;
					}
				}
			}
		}
		fprintf(stderr, "opts-schema: option \"%s\" has %i values, truncating to %i%s\n", key, count, OPTS_SCHEMA_MAX_VALUES, def_rescue ? ", default preserved" : "");
		count = OPTS_SCHEMA_MAX_VALUES;
	}

	cJSON_AddStringToObject(item, "type", "enum");
	cJSON* vals = cJSON_AddArrayToObject(item, "values");
	cJSON* labs = cJSON_AddArrayToObject(item, "labels");
	for (int i = 0; i < count; i++) {
		const struct retro_core_option_value* v = (def_rescue && i == count - 1) ? def_rescue : &values[i];
		cJSON_AddItemToArray(vals, cJSON_CreateString(v->value));
		cJSON_AddItemToArray(labs, cJSON_CreateString(v->label ? v->label : v->value));
	}
	cJSON_AddStringToObject(item, "default", default_value ? default_value : values[0].value);
	cJSON_AddItemToArray(items_arr, item);
}

static void section_put(SectionWriter* w, const char* key, const char* label, const char* info, const struct retro_core_option_value* values, const char* default_value) {
	if (!key)
		return;
	if (values_count(values) <= 0) {
		fprintf(stderr, "opts-schema: skipping option \"%s\" (no values)\n", key);
		return;
	}
	cJSON* items = section_items(w);
	if (!items)
		return; // section cap reached
	add_item(items, key, label, info, values, default_value);
}

///////////////////////////////

static cJSON* schema_root(const char* emulator, SchemaSections* all) {
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "emulator", emulator ? emulator : "");
	cJSON_AddStringToObject(root, "options_hint", "Changes apply the next time the game is launched.");
	all->sections = cJSON_AddArrayToObject(root, "sections");
	all->total = 0;
	return root;
}

static char* schema_finish(cJSON* root, SchemaSections* all) {
	char* json = all->total > 0 ? cJSON_Print(root) : NULL;
	cJSON_Delete(root);
	return json;
}

char* OptsSchema_fromV1(const struct retro_core_option_definition* defs, const char* emulator) {
	if (!defs || !defs[0].key)
		return NULL;

	SchemaSections all;
	cJSON* root = schema_root(emulator, &all);
	SectionWriter sec = section_begin(&all, "Options");
	for (int i = 0; defs[i].key; i++)
		section_put(&sec, defs[i].key, defs[i].desc, defs[i].info, defs[i].values, defs[i].default_value);
	return schema_finish(root, &all);
}

static const struct retro_core_option_v2_category* v2_findCategory(const struct retro_core_option_v2_category* cats, const char* key) {
	if (!cats || !key)
		return NULL;
	for (int i = 0; cats[i].key; i++) {
		if (!strcmp(cats[i].key, key))
			return &cats[i];
	}
	return NULL;
}

static void v2_put(SectionWriter* w, const struct retro_core_option_v2_definition* def, int in_category) {
	const char* label = in_category && def->desc_categorized ? def->desc_categorized : def->desc;
	const char* info = def->info ? def->info : def->info_categorized;
	section_put(w, def->key, label, info, def->values, def->default_value);
}

char* OptsSchema_fromV2(const struct retro_core_options_v2* v2, const char* emulator) {
	if (!v2 || !v2->definitions || !v2->definitions[0].key)
		return NULL;

	const struct retro_core_option_v2_category* cats = v2->categories;
	const struct retro_core_option_v2_definition* defs = v2->definitions;

	SchemaSections all;
	cJSON* root = schema_root(emulator, &all);

	// uncategorized defs first (NULL category_key or key without a matching
	// category); the lazy chunking only emits "General" when non-empty
	SectionWriter general = section_begin(&all, "General");
	for (int i = 0; defs[i].key; i++) {
		if (v2_findCategory(cats, defs[i].category_key))
			continue;
		v2_put(&general, &defs[i], 0);
	}

	// then one section per category, in registration order
	for (int c = 0; cats && cats[c].key; c++) {
		SectionWriter sec = section_begin(&all, cats[c].desc ? cats[c].desc : cats[c].key);
		for (int i = 0; defs[i].key; i++) {
			if (!defs[i].category_key || strcmp(defs[i].category_key, cats[c].key) != 0)
				continue;
			v2_put(&sec, &defs[i], 1);
		}
	}
	return schema_finish(root, &all);
}

// raw retro_variable value lists are tiny; anything past this is dropped
// before add_item's OPTS_SCHEMA_MAX_VALUES truncation even applies
#define VARS_MAX_SPLIT 128

char* OptsSchema_fromVars(const struct retro_variable* vars, const char* emulator) {
	if (!vars || !vars[0].key)
		return NULL;

	SchemaSections all;
	cJSON* root = schema_root(emulator, &all);
	SectionWriter sec = section_begin(&all, "Options");
	for (int i = 0; vars[i].key; i++) {
		if (!vars[i].value)
			continue;

		// same shape OptionList_vars (ma_options.c) consumes:
		// "Name; a|b|c" — split at "; ", then '|'; values double as labels
		char* buf = strdup(vars[i].value);
		if (!buf)
			continue;
		const char* name = vars[i].key;
		char* opt = buf;
		char* semi = strchr(buf, ';');
		if (semi && semi[1] == ' ') {
			*semi = '\0';
			name = buf;
			opt = semi + 2;
		}

		struct retro_core_option_value values[VARS_MAX_SPLIT + 1];
		int count = 0;
		char* tok = opt;
		while (tok && count < VARS_MAX_SPLIT) {
			char* bar = strchr(tok, '|');
			if (bar)
				*bar = '\0';
			values[count].value = tok;
			values[count].label = NULL; // no separate labels for vars
			count += 1;
			tok = bar ? bar + 1 : NULL;
		}
		values[count].value = NULL;
		values[count].label = NULL;

		// vars carry no default; add_item falls back to the first value
		section_put(&sec, vars[i].key, name, NULL, values, NULL);
		free(buf);
	}
	return schema_finish(root, &all);
}

///////////////////////////////

int OptsSchema_writeFile(const char* path, const char* json) {
	if (!path || !json)
		return -1;

	size_t json_len = strlen(json);

	// unchanged content: leave the file untouched (preserves mtime, avoids
	// pointless flash writes on every launch)
	FILE* file = fopen(path, "rb");
	if (file) {
		int same = 0;
		if (fseek(file, 0, SEEK_END) == 0) {
			long len = ftell(file);
			if (len == (long)json_len && fseek(file, 0, SEEK_SET) == 0) {
				char* buf = malloc(json_len);
				if (buf) {
					if (fread(buf, 1, json_len, file) == json_len && memcmp(buf, json, json_len) == 0)
						same = 1;
					free(buf);
				}
			}
		}
		fclose(file);
		if (same)
			return 0;
	}

	// atomic replace: write "<path>.tmp", rename over, then flush to disk
	size_t tmp_len = strlen(path) + 5; // ".tmp" + NUL
	char* tmp = malloc(tmp_len);
	if (!tmp)
		return -1;
	snprintf(tmp, tmp_len, "%s.tmp", path);

	FILE* out = fopen(tmp, "wb");
	if (!out) {
		free(tmp);
		return -1;
	}
	int ok = fwrite(json, 1, json_len, out) == json_len;
	if (fclose(out) != 0)
		ok = 0;
	if (ok && rename(tmp, path) != 0)
		ok = 0;
	if (!ok) {
		remove(tmp);
		free(tmp);
		return -1;
	}
	free(tmp);
	sync();
	return 0;
}
