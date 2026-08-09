#include "emu_overlay_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "cjson/cJSON.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void safe_strcpy(char* dst, size_t dst_size, const char* src) {
	if (!dst || dst_size == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	snprintf(dst, dst_size, "%s", src);
}

static const char* json_get_string(const cJSON* obj, const char* key) {
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(item) && item->valuestring)
		return item->valuestring;
	return NULL;
}

static int json_get_int(const cJSON* obj, const char* key, int fallback) {
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsNumber(item))
		return item->valueint;
	return fallback;
}

static bool json_get_bool(const cJSON* obj, const char* key, bool fallback) {
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsBool(item))
		return cJSON_IsTrue(item) ? true : false;
	return fallback;
}

static char* read_file_to_string(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	if (len < 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	char* buf = (char*)malloc((size_t)len + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	size_t read_len = fread(buf, 1, (size_t)len, f);
	buf[read_len] = '\0';
	fclose(f);
	return buf;
}

// Strip leading/trailing whitespace in-place, return pointer into same buffer.
static char* strip(char* s) {
	if (!s)
		return s;
	while (*s && isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	char* end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		*end-- = '\0';
	return s;
}

// Case-insensitive string comparison for bool parsing.
static bool str_eq_nocase(const char* a, const char* b) {
	if (!a || !b)
		return false;
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return false;
		a++;
		b++;
	}
	return *a == *b;
}

// ---------------------------------------------------------------------------
// JSON loading
// ---------------------------------------------------------------------------

static void parse_item(const cJSON* json_item, EmuOvlItem* item) {
	memset(item, 0, sizeof(*item));

	const char* s;

	s = json_get_string(json_item, "key");
	safe_strcpy(item->key, sizeof(item->key), s);

	s = json_get_string(json_item, "label");
	safe_strcpy(item->label, sizeof(item->label), s);

	s = json_get_string(json_item, "description");
	safe_strcpy(item->description, sizeof(item->description), s);

	// type
	const char* type_str = json_get_string(json_item, "type");
	if (type_str) {
		if (strcmp(type_str, "bool") == 0)
			item->type = EMU_OVL_TYPE_BOOL;
		else if (strcmp(type_str, "cycle") == 0)
			item->type = EMU_OVL_TYPE_CYCLE;
		else if (strcmp(type_str, "int") == 0)
			item->type = EMU_OVL_TYPE_INT;
		else if (strcmp(type_str, "enum") == 0)
			item->type = EMU_OVL_TYPE_ENUM;
		else
			item->type = EMU_OVL_TYPE_BOOL;
	}

	// values array (for cycle type)
	item->value_count = 0;
	const cJSON* values_arr = cJSON_GetObjectItemCaseSensitive(json_item, "values");
	if (cJSON_IsArray(values_arr)) {
		int count = cJSON_GetArraySize(values_arr);
		if (count > EMU_OVL_MAX_VALUES)
			count = EMU_OVL_MAX_VALUES;
		for (int i = 0; i < count; i++) {
			const cJSON* v = cJSON_GetArrayItem(values_arr, i);
			if (cJSON_IsNumber(v))
				item->values[i] = v->valueint;
		}
		item->value_count = count;
	}

	// labels array (for cycle type)
	const cJSON* labels_arr = cJSON_GetObjectItemCaseSensitive(json_item, "labels");
	if (cJSON_IsArray(labels_arr)) {
		int count = cJSON_GetArraySize(labels_arr);
		if (count > EMU_OVL_MAX_VALUES)
			count = EMU_OVL_MAX_VALUES;
		for (int i = 0; i < count; i++) {
			const cJSON* l = cJSON_GetArrayItem(labels_arr, i);
			if (cJSON_IsString(l) && l->valuestring)
				safe_strcpy(item->labels[i], sizeof(item->labels[i]), l->valuestring);
		}
	}

	// enum type: values[] holds strings; svalues owns them and the internal
	// int value is simply the index into that array
	if (item->type == EMU_OVL_TYPE_ENUM) {
		item->value_count = 0;
		if (cJSON_IsArray(values_arr)) {
			int count = cJSON_GetArraySize(values_arr);
			if (count > EMU_OVL_MAX_VALUES)
				count = EMU_OVL_MAX_VALUES;
			for (int i = 0; i < count; i++) {
				const cJSON* v = cJSON_GetArrayItem(values_arr, i);
				if (cJSON_IsString(v) && v->valuestring)
					item->svalues[item->value_count] = strdup(v->valuestring);
				else
					item->svalues[item->value_count] = strdup("");
				// labels[] was already filled positionally above; backfill the
				// value string where the schema gave no label
				if (item->labels[item->value_count][0] == '\0')
					safe_strcpy(item->labels[item->value_count],
								sizeof(item->labels[item->value_count]),
								item->svalues[item->value_count]);
				item->values[item->value_count] = item->value_count;
				item->value_count++;
			}
		}
	}

	// int range
	item->int_min = json_get_int(json_item, "min", 0);
	item->int_max = json_get_int(json_item, "max", 100);
	item->int_step = json_get_int(json_item, "step", 1);
	if (item->int_step < 1)
		item->int_step = 1;

	// float_scale: if >0, INI stores float value; multiply by scale for internal int
	item->float_scale = json_get_int(json_item, "float_scale", 0);

	// default — JSON bools need special handling
	if (item->type == EMU_OVL_TYPE_BOOL) {
		item->default_value = json_get_bool(json_item, "default", false) ? 1 : 0;
	} else if (item->type == EMU_OVL_TYPE_ENUM) {
		// string default -> index in svalues; 0 when absent or unknown
		const char* d = json_get_string(json_item, "default");
		item->default_value = 0;
		if (d) {
			for (int i = 0; i < item->value_count; i++) {
				if (strcmp(item->svalues[i], d) == 0) {
					item->default_value = i;
					break;
				}
			}
		}
	} else {
		item->default_value = json_get_int(json_item, "default", 0);
	}
	item->current_value = item->default_value;
	item->staged_value = item->default_value;
	item->dirty = false;
}

static void parse_section(const cJSON* json_sec, EmuOvlSection* sec) {
	memset(sec, 0, sizeof(*sec));

	const char* name = json_get_string(json_sec, "name");
	safe_strcpy(sec->name, sizeof(sec->name), name);

	const char* ini_sec = json_get_string(json_sec, "ini_section");
	safe_strcpy(sec->ini_section, sizeof(sec->ini_section), ini_sec);

	sec->item_count = 0;
	const cJSON* items_arr = cJSON_GetObjectItemCaseSensitive(json_sec, "items");
	if (!cJSON_IsArray(items_arr))
		return;

	int count = cJSON_GetArraySize(items_arr);
	if (count > EMU_OVL_MAX_ITEMS)
		count = EMU_OVL_MAX_ITEMS;

	for (int i = 0; i < count; i++) {
		const cJSON* json_item = cJSON_GetArrayItem(items_arr, i);
		if (!json_item)
			continue;
		if (json_get_bool(json_item, "hidden", false))
			continue;
		parse_item(json_item, &sec->items[sec->item_count]);
		sec->item_count++;
	}
}

int emu_ovl_cfg_load(EmuOvlConfig* cfg, const char* json_path) {
	if (!cfg || !json_path)
		return -1;

	memset(cfg, 0, sizeof(*cfg));

	char* json_str = read_file_to_string(json_path);
	if (!json_str) {
		printf("[emu_ovl_cfg] failed to read %s\n", json_path);
		return -1;
	}

	cJSON* root = cJSON_Parse(json_str);
	free(json_str);
	if (!root) {
		printf("[emu_ovl_cfg] failed to parse JSON from %s\n", json_path);
		return -1;
	}

	const char* s;

	s = json_get_string(root, "emulator");
	safe_strcpy(cfg->emulator, sizeof(cfg->emulator), s);

	s = json_get_string(root, "config_file");
	safe_strcpy(cfg->config_file, sizeof(cfg->config_file), s);

	s = json_get_string(root, "config_section");
	safe_strcpy(cfg->config_section, sizeof(cfg->config_section), s);

	s = json_get_string(root, "options_hint");
	safe_strcpy(cfg->options_hint, sizeof(cfg->options_hint), s);

	cfg->save_state = json_get_bool(root, "save_state", false);
	cfg->load_state = json_get_bool(root, "load_state", false);

	cfg->section_count = 0;
	const cJSON* sections_arr = cJSON_GetObjectItemCaseSensitive(root, "sections");
	if (cJSON_IsArray(sections_arr)) {
		int count = cJSON_GetArraySize(sections_arr);
		if (count > EMU_OVL_MAX_SECTIONS)
			count = EMU_OVL_MAX_SECTIONS;

		for (int i = 0; i < count; i++) {
			const cJSON* json_sec = cJSON_GetArrayItem(sections_arr, i);
			if (!json_sec)
				continue;
			parse_section(json_sec, &cfg->sections[cfg->section_count]);
			cfg->section_count++;
		}
	}

	cJSON_Delete(root);
	return 0;
}

void emu_ovl_cfg_free(EmuOvlConfig* cfg) {
	if (!cfg)
		return;
	// Release enum value strings; the memset below nulls the pointers so a
	// second free is a no-op.
	for (int s = 0; s < cfg->section_count; s++) {
		EmuOvlSection* sec = &cfg->sections[s];
		for (int i = 0; i < sec->item_count; i++) {
			for (int v = 0; v < EMU_OVL_MAX_VALUES; v++) {
				if (sec->items[i].svalues[v]) {
					free(sec->items[i].svalues[v]);
					sec->items[i].svalues[v] = NULL;
				}
			}
		}
	}
	memset(cfg, 0, sizeof(*cfg));
}

// ---------------------------------------------------------------------------
// INI reading — mupen64plus.cfg format: "key = value" inside [section]
// ---------------------------------------------------------------------------

// Parse a bool value from an INI string. Accepts "true"/"1"/"yes"/"on"
// (case-insensitive) as true; everything else is false. "yes"/"no" cover
// flycast's emu.cfg format (core/cfg/ini.cpp ConfigFile::set_bool), which
// rewrites the whole file with those tokens on every settings save.
static int parse_ini_bool(const char* val) {
	if (str_eq_nocase(val, "true") || str_eq_nocase(val, "1") ||
		str_eq_nocase(val, "yes") || str_eq_nocase(val, "on"))
		return 1;
	return 0;
}

// Parse an integer value from an INI string.  Falls back to 0 on failure.
static int parse_ini_int(const char* val) {
	if (!val || !*val)
		return 0;
	return atoi(val);
}

// Convert an INI string to this item's internal int representation. Sole
// owner of the per-type dispatch: emu_ovl_cfg_read_ini and the public
// emu_ovl_cfg_parse_value both route through here so the overlay, the
// pre-launch options editor and launch.sh can never disagree about what a
// stored value means.
static int parse_item_value(const EmuOvlItem* item, const char* val) {
	switch (item->type) {
	case EMU_OVL_TYPE_BOOL:
		return parse_ini_bool(val);
	case EMU_OVL_TYPE_CYCLE:
	case EMU_OVL_TYPE_INT:
		if (item->float_scale > 0)
			return (int)(atof(val) * item->float_scale + 0.5);
		return parse_ini_int(val);
	case EMU_OVL_TYPE_ENUM:
		// exact string match against the value set; -1 when unknown so
		// callers can decide whether to intern or keep the default
		for (int i = 0; i < item->value_count; i++) {
			if (strcmp(item->svalues[i], val) == 0)
				return i;
		}
		return -1;
	default:
		return parse_ini_int(val);
	}
}

// Whole string is a well-formed decimal/float number (no leading sign-only,
// no trailing junk). Leading whitespace is tolerated by strtod; callers are
// expected to have trimmed already.
static bool str_is_number(const char* s) {
	if (!s || !*s)
		return false;
	char* end = NULL;
	strtod(s, &end);
	return end != NULL && end != s && *end == '\0';
}

// Get the effective INI section name for a config section.
// Uses per-section ini_section if set, otherwise falls back to global config_section.
static const char* get_ini_section(const EmuOvlConfig* cfg, const EmuOvlSection* sec) {
	if (sec->ini_section[0] != '\0')
		return sec->ini_section;
	return cfg->config_section;
}

const char* emu_ovl_cfg_section_name(const EmuOvlConfig* cfg, int sec_idx) {
	if (sec_idx < 0 || sec_idx >= cfg->section_count)
		return cfg->config_section;
	return get_ini_section(cfg, &cfg->sections[sec_idx]);
}

EmuOvlItem* emu_ovl_cfg_find_item(EmuOvlConfig* cfg, const char* ini_section, const char* key, int* out_sec, int* out_item) {
	if (!cfg || !ini_section || !key)
		return NULL;
	for (int s = 0; s < cfg->section_count; s++) {
		if (strcmp(emu_ovl_cfg_section_name(cfg, s), ini_section) != 0)
			continue;
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			if (strcmp(cfg->sections[s].items[i].key, key) == 0) {
				if (out_sec)
					*out_sec = s;
				if (out_item)
					*out_item = i;
				return &cfg->sections[s].items[i];
			}
		}
	}
	return NULL;
}

bool emu_ovl_cfg_parse_value(const EmuOvlItem* item, const char* str, int* out_value) {
	if (!item || !str || !*str)
		return false;
	// Numeric types must actually look numeric. read_ini's atoi/atof
	// tolerance is fine when re-reading a file we wrote ourselves, but a
	// per-game override file is arbitrary input and garbage silently
	// collapsing to 0 would look like a deliberate setting. Bools stay
	// total, exactly as read_ini treats them (anything not true-ish is 0).
	// Enums are string-valued: no numeric gate, unknown strings just fail.
	if (item->type != EMU_OVL_TYPE_BOOL && item->type != EMU_OVL_TYPE_ENUM &&
		!str_is_number(str))
		return false;
	int val = parse_item_value(item, str);
	if (item->type == EMU_OVL_TYPE_ENUM && val < 0)
		return false;
	if (out_value)
		*out_value = val;
	return true;
}

int emu_ovl_cfg_read_ini(EmuOvlConfig* cfg, const char* ini_path) {
	if (!cfg || !ini_path)
		return -1;

	FILE* f = fopen(ini_path, "r");
	if (!f) {
		printf("[emu_ovl_cfg] failed to open INI %s for reading\n", ini_path);
		return -1;
	}

	char current_ini_section[EMU_OVL_MAX_STR] = "";
	char line[1024];

	while (fgets(line, sizeof(line), f)) {
		// Strip trailing newline / whitespace
		char* trimmed = strip(line);

		// Section header?
		if (trimmed[0] == '[') {
			char* end = strchr(trimmed, ']');
			if (end) {
				*end = '\0';
				safe_strcpy(current_ini_section, sizeof(current_ini_section), trimmed + 1);
			} else {
				current_ini_section[0] = '\0';
			}
			continue;
		}

		if (current_ini_section[0] == '\0')
			continue;

		// Skip comments and blank lines
		if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
			continue;

		// Parse "key = value"
		char* eq = strchr(trimmed, '=');
		if (!eq)
			continue;

		*eq = '\0';
		char* ini_key = strip(trimmed);
		char* ini_val = strip(eq + 1);

		// Match against items whose INI section matches the current section
		for (int s = 0; s < cfg->section_count; s++) {
			EmuOvlSection* sec = &cfg->sections[s];
			const char* target_sec = get_ini_section(cfg, sec);
			if (strcmp(target_sec, current_ini_section) != 0)
				continue;

			for (int i = 0; i < sec->item_count; i++) {
				EmuOvlItem* item = &sec->items[i];
				if (strcmp(item->key, ini_key) != 0)
					continue;

				int val = parse_item_value(item, ini_val);

				// Unknown enum value in the file: keep the default rather
				// than storing an out-of-range index.
				if (item->type == EMU_OVL_TYPE_ENUM && val < 0)
					continue;

				item->current_value = val;
				item->staged_value = val;
				item->dirty = false;
			}
		}
	}

	fclose(f);
	return 0;
}

// ---------------------------------------------------------------------------
// INI writing — preserve entire file, only replace matching keys in [section]
// ---------------------------------------------------------------------------

// The one and only value formatter. Widest output is a float_scale value
// ("-2147483648.000000", 18 chars), so 64 bytes is always enough for the
// callers below.
void emu_ovl_cfg_format_value(const EmuOvlItem* item, int value, char* out, int out_size) {
	if (!out || out_size <= 0)
		return;
	out[0] = '\0';
	if (!item)
		return;
	switch (item->type) {
	case EMU_OVL_TYPE_BOOL:
		snprintf(out, (size_t)out_size, "%s", value ? "True" : "False");
		break;
	case EMU_OVL_TYPE_CYCLE:
	case EMU_OVL_TYPE_INT:
		if (item->float_scale > 0)
			snprintf(out, (size_t)out_size, "%f", (double)value / item->float_scale);
		else
			snprintf(out, (size_t)out_size, "%d", value);
		break;
	case EMU_OVL_TYPE_ENUM:
		if (item->value_count <= 0)
			break; // out[0] already '\0'
		if (value < 0)
			value = 0;
		else if (value >= item->value_count)
			value = item->value_count - 1;
		snprintf(out, (size_t)out_size, "%s", item->svalues[value]);
		break;
	}
}

int emu_ovl_cfg_enum_intern(EmuOvlItem* item, const char* value) {
	if (!item || item->type != EMU_OVL_TYPE_ENUM || !value)
		return -1;
	for (int i = 0; i < item->value_count; i++)
		if (strcmp(item->svalues[i], value) == 0)
			return i;
	if (item->value_count >= EMU_OVL_MAX_VALUES)
		return -1;
	int i = item->value_count;
	item->svalues[i] = strdup(value);
	safe_strcpy(item->labels[i], sizeof(item->labels[i]), value);
	item->values[i] = i;
	item->value_count++;
	return i;
}

// Helper: write a single item's value to file. The "key = value" spacing is
// load-bearing — flycast's ConfigFile::save() writes exactly this and
// DC.pak/launch.sh's sed patterns ("^$key =") match on it.
static void write_item_value(FILE* out, const EmuOvlItem* item) {
	char value[64];
	emu_ovl_cfg_format_value(item, item->staged_value, value, sizeof(value));
	fprintf(out, "%s = %s\n", item->key, value);
}

// Dirty item tracking with its target INI section name
typedef struct {
	EmuOvlItem* item;
	const char* ini_section;
	bool written;
} DirtyEntry;

int emu_ovl_cfg_write_ini(EmuOvlConfig* cfg, const char* ini_path) {
	if (!cfg || !ini_path)
		return -1;

	// Read the entire original file into memory
	char* original = read_file_to_string(ini_path);
	if (!original) {
		printf("[emu_ovl_cfg] failed to read INI %s for writing\n", ini_path);
		return -1;
	}

	// Build a flat list of dirty items with their target INI sections
	DirtyEntry dirty[EMU_OVL_MAX_SECTIONS * EMU_OVL_MAX_ITEMS];
	int dirty_count = 0;
	for (int s = 0; s < cfg->section_count; s++) {
		EmuOvlSection* sec = &cfg->sections[s];
		const char* target = get_ini_section(cfg, sec);
		for (int i = 0; i < sec->item_count; i++) {
			if (sec->items[i].dirty) {
				dirty[dirty_count].item = &sec->items[i];
				dirty[dirty_count].ini_section = target;
				dirty[dirty_count].written = false;
				dirty_count++;
			}
		}
	}

	if (dirty_count == 0) {
		free(original);
		return 0; // nothing to write
	}

	// Open output file
	FILE* out = fopen(ini_path, "w");
	if (!out) {
		printf("[emu_ovl_cfg] failed to open INI %s for writing\n", ini_path);
		free(original);
		return -1;
	}

	char current_ini_section[EMU_OVL_MAX_STR] = "";
	char* cursor = original;

	// Tracks whether the last byte written to `out` was a newline. Only the
	// file's own final line, if it lacks a trailing '\n', can leave this
	// false — everything we generate ourselves (write_item_value, headers)
	// always ends in '\n'. Appended items must be newline-framed regardless,
	// so any code appending after the main loop checks this first.
	bool out_ends_with_nl = true;

	while (cursor && *cursor) {
		// Find end of current line
		char* eol = strchr(cursor, '\n');
		size_t line_len;
		if (eol) {
			line_len = (size_t)(eol - cursor + 1); // include the '\n'
		} else {
			line_len = strlen(cursor);
		}
		// Default outcome for this chunk if copied verbatim (all copy paths
		// below write exactly `cursor[0..line_len)` unchanged); the one
		// branch that writes something else (a dirty-key replacement)
		// overrides this to true, since write_item_value always ends in '\n'.
		out_ends_with_nl = (eol != NULL);

		// Make a mutable copy of this line for inspection
		char line_buf[1024];
		size_t copy_len = line_len;
		if (copy_len >= sizeof(line_buf))
			copy_len = sizeof(line_buf) - 1;
		memcpy(line_buf, cursor, copy_len);
		line_buf[copy_len] = '\0';

		char* trimmed = strip(line_buf);

		// Check for section header
		if (trimmed[0] == '[') {
			// Leaving current section — append any unwritten dirty items for it
			if (current_ini_section[0] != '\0') {
				for (int d = 0; d < dirty_count; d++) {
					if (!dirty[d].written && strcmp(dirty[d].ini_section, current_ini_section) == 0) {
						write_item_value(out, dirty[d].item);
						dirty[d].written = true;
					}
				}
			}

			char* end = strchr(trimmed, ']');
			if (end) {
				*end = '\0';
				safe_strcpy(current_ini_section, sizeof(current_ini_section), trimmed + 1);
			} else {
				current_ini_section[0] = '\0';
			}
			// Write the original line unchanged
			fwrite(cursor, 1, line_len, out);
			cursor += line_len;
			continue;
		}

		// Check if any dirty items target the current INI section
		bool section_has_dirty = false;
		for (int d = 0; d < dirty_count; d++) {
			if (!dirty[d].written && strcmp(dirty[d].ini_section, current_ini_section) == 0) {
				section_has_dirty = true;
				break;
			}
		}

		// If no dirty items target this section, write unchanged
		if (!section_has_dirty) {
			fwrite(cursor, 1, line_len, out);
			cursor += line_len;
			continue;
		}

		// In a section with dirty items: check if this line matches a dirty key
		char parse_buf[1024];
		memcpy(parse_buf, cursor, copy_len);
		parse_buf[copy_len] = '\0';
		char* parse_trimmed = strip(parse_buf);

		// Skip comments and blank lines — write as-is
		if (parse_trimmed[0] == '#' || parse_trimmed[0] == ';' || parse_trimmed[0] == '\0') {
			fwrite(cursor, 1, line_len, out);
			cursor += line_len;
			continue;
		}

		char* eq = strchr(parse_trimmed, '=');
		if (!eq) {
			// Not a key=value line, write as-is
			fwrite(cursor, 1, line_len, out);
			cursor += line_len;
			continue;
		}

		*eq = '\0';
		char* ini_key = strip(parse_trimmed);

		// Search for a dirty item with this key in the current INI section
		int matched_idx = -1;
		for (int d = 0; d < dirty_count; d++) {
			if (!dirty[d].written &&
				strcmp(dirty[d].ini_section, current_ini_section) == 0 &&
				strcmp(dirty[d].item->key, ini_key) == 0) {
				matched_idx = d;
				break;
			}
		}

		if (matched_idx >= 0) {
			// Write replacement line and mark as written
			write_item_value(out, dirty[matched_idx].item);
			dirty[matched_idx].written = true;
			out_ends_with_nl = true;
		} else {
			// Not a dirty key, write unchanged
			fwrite(cursor, 1, line_len, out);
		}

		cursor += line_len;
	}

	// If we ended while still in a section, append unwritten items for it
	if (current_ini_section[0] != '\0') {
		for (int d = 0; d < dirty_count; d++) {
			if (!dirty[d].written && strcmp(dirty[d].ini_section, current_ini_section) == 0) {
				// The file's final line may not have had a trailing '\n' —
				// don't let an appended item run onto it.
				if (!out_ends_with_nl) {
					fputc('\n', out);
					out_ends_with_nl = true;
				}
				write_item_value(out, dirty[d].item);
				dirty[d].written = true;
			}
		}
	}

	// Any dirty items whose [section] header never appeared in the file at
	// all (empty file, or a section the file simply doesn't have yet — e.g.
	// flycast's [achievements] before RetroAchievements is ever touched)
	// are still unwritten at this point. Append them grouped under freshly
	// written headers so they aren't silently dropped.
	for (int d = 0; d < dirty_count; d++) {
		if (dirty[d].written)
			continue;
		// Same newline guard as above: this can be the first append of all
		// (e.g. the section-present-but-final-line-unterminated case above
		// never ran) so it must be checked independently here too.
		if (!out_ends_with_nl) {
			fputc('\n', out);
			out_ends_with_nl = true;
		}
		const char* target = dirty[d].ini_section;
		fprintf(out, "[%s]\n", target);
		for (int d2 = d; d2 < dirty_count; d2++) {
			if (!dirty[d2].written && strcmp(dirty[d2].ini_section, target) == 0) {
				write_item_value(out, dirty[d2].item);
				dirty[d2].written = true;
			}
		}
	}

	fclose(out);
	free(original);
	return 0;
}

// ---------------------------------------------------------------------------
// Staged value helpers
// ---------------------------------------------------------------------------

void emu_ovl_cfg_reset_section_to_defaults(EmuOvlSection* sec) {
	if (!sec)
		return;
	for (int i = 0; i < sec->item_count; i++) {
		sec->items[i].staged_value = sec->items[i].default_value;
		sec->items[i].dirty = (sec->items[i].staged_value != sec->items[i].current_value);
	}
}


void emu_ovl_cfg_apply_staged(EmuOvlConfig* cfg) {
	if (!cfg)
		return;
	for (int s = 0; s < cfg->section_count; s++) {
		EmuOvlSection* sec = &cfg->sections[s];
		for (int i = 0; i < sec->item_count; i++) {
			if (sec->items[i].dirty) {
				sec->items[i].current_value = sec->items[i].staged_value;
				sec->items[i].dirty = false;
			}
		}
	}
}

bool emu_ovl_cfg_has_changes(EmuOvlConfig* cfg) {
	if (!cfg)
		return false;
	for (int s = 0; s < cfg->section_count; s++) {
		EmuOvlSection* sec = &cfg->sections[s];
		for (int i = 0; i < sec->item_count; i++) {
			if (sec->items[i].dirty)
				return true;
		}
	}
	return false;
}
