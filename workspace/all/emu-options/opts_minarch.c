#include "opts_minarch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Longest line we can parse; anything longer passes through writes verbatim
// (a schema "key = value" line never gets near this).
#define LINE_MAX_LEN 1024

// malloc'd file contents or NULL. Same shape as read_file_to_string in
// emu_overlay_cfg.c; duplicated rather than exported — 20 lines vs a new
// public symbol in a header three binaries share.
static char* read_all(const char* path) {
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

// Linear scan of ALL sections by item->key: minarch cfg files carry no INI
// sections, so emu_ovl_cfg_find_item (which matches on resolved section
// names) is the wrong tool here.
static EmuOvlItem* find_schema_item(EmuOvlConfig* cfg, const char* key, int* out_s, int* out_i) {
	for (int s = 0; s < cfg->section_count; s++) {
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			if (strcmp(cfg->sections[s].items[i].key, key) == 0) {
				if (out_s)
					*out_s = s;
				if (out_i)
					*out_i = i;
				return &cfg->sections[s].items[i];
			}
		}
	}
	return NULL;
}

// Split a mutable line copy into key/value on the exact " = " separator
// (Config_getValue's contract, ma_config.c:843: key anchored at line start,
// optionally behind a '-' lock marker, immediately followed by " = ").
// Leading whitespace is deliberately NOT skipped — minarch would not match
// such a line either. Returns false for non-matching lines ("bind Up = UP"
// parses fine but fails the schema-key lookup downstream).
static bool parse_cfg_line(char* line, char** key, char** val, bool* locked) {
	size_t len = strlen(line);
	while (len > 0 && isspace((unsigned char)line[len - 1]))
		line[--len] = '\0';
	char* p = line;
	bool lock = false;
	if (*p == '-') {
		lock = true;
		p++;
	}
	char* sep = strstr(p, " = ");
	if (!sep || sep == p)
		return false;
	*sep = '\0';
	*key = p;
	*val = sep + 3;
	*locked = lock;
	return true;
}

// Copy the line at *cursor into buf (NUL-terminated, truncated when huge) and
// return its raw length including the '\n' when present. Advancing is the
// caller's job so it can also copy the raw bytes.
static size_t peek_line(const char* cursor, char* buf, size_t buf_size) {
	const char* eol = strchr(cursor, '\n');
	size_t raw_len = eol ? (size_t)(eol - cursor + 1) : strlen(cursor);
	size_t copy_len = raw_len;
	if (copy_len >= buf_size)
		copy_len = buf_size - 1;
	memcpy(buf, cursor, copy_len);
	buf[copy_len] = '\0';
	return raw_len;
}

// One pass over one layer's content marking locked schema keys. Indices are
// pre-prune: only prune_locked below consumes this map.
static void apply_locks(EmuOvlConfig* cfg, const char* content,
						bool locked[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS]) {
	if (!content)
		return;
	const char* cursor = content;
	char buf[LINE_MAX_LEN];
	while (*cursor) {
		size_t raw_len = peek_line(cursor, buf, sizeof(buf));
		char *key, *val;
		bool lock;
		if (parse_cfg_line(buf, &key, &val, &lock) && lock) {
			int s, i;
			if (find_schema_item(cfg, key, &s, &i))
				locked[s][i] = true;
		}
		cursor += raw_len;
	}
}

// One pass over one layer's content filling vals[s][i] for every parseable,
// non-locked schema-key line. Unknown enum values are interned here — the
// ONLY place that interns — so a hand-edited or newer-core value displays
// and round-trips instead of collapsing to the default.
static void apply_values(EmuOvlConfig* cfg, const char* content,
						 int vals[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS]) {
	if (!content)
		return;
	const char* cursor = content;
	char buf[LINE_MAX_LEN];
	while (*cursor) {
		size_t raw_len = peek_line(cursor, buf, sizeof(buf));
		cursor += raw_len;
		char *key, *val;
		bool lock;
		if (!parse_cfg_line(buf, &key, &val, &lock) || lock)
			continue;
		int s, i;
		EmuOvlItem* item = find_schema_item(cfg, key, &s, &i);
		if (!item)
			continue;
		int value;
		if (emu_ovl_cfg_parse_value(item, val, &value)) {
			vals[s][i] = value;
			continue;
		}
		value = emu_ovl_cfg_enum_intern(item, val);
		if (value >= 0)
			vals[s][i] = value;
	}
}

static void free_item_svalues(EmuOvlItem* item) {
	for (int v = 0; v < EMU_OVL_MAX_VALUES; v++) {
		if (item->svalues[v]) {
			free(item->svalues[v]);
			item->svalues[v] = NULL;
		}
	}
}

// Drop locked items from the schema, then sections left empty. Runs BEFORE
// any value snapshot so every [s][i] taken afterwards refers to the pruned
// layout. Vacated tail slots are zeroed so their stale svalues pointers can
// never be double-freed by emu_ovl_cfg_free.
static void prune_locked(EmuOvlConfig* cfg,
						 bool locked[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS]) {
	for (int s = 0; s < cfg->section_count; s++) {
		EmuOvlSection* sec = &cfg->sections[s];
		int w = 0;
		for (int r = 0; r < sec->item_count; r++) {
			if (locked[s][r]) {
				free_item_svalues(&sec->items[r]);
				continue;
			}
			if (w != r)
				sec->items[w] = sec->items[r];
			w++;
		}
		for (int k = w; k < sec->item_count; k++)
			memset(&sec->items[k], 0, sizeof(sec->items[k]));
		sec->item_count = w;
	}
	int w = 0;
	for (int r = 0; r < cfg->section_count; r++) {
		if (cfg->sections[r].item_count == 0)
			continue;
		if (w != r)
			cfg->sections[w] = cfg->sections[r];
		w++;
	}
	for (int k = w; k < cfg->section_count; k++)
		memset(&cfg->sections[k], 0, sizeof(cfg->sections[k]));
	cfg->section_count = w;
}

// "<path>.tmp" -> fputs -> rename -> sync: a launch-time power cut leaves
// either the old file or the new one, never a torn half.
static int write_atomic(const char* path, const char* content) {
	char tmp[1024 + 8];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -1;
	FILE* f = fopen(tmp, "w");
	if (!f)
		return -1;
	if (fputs(content, f) < 0) {
		fclose(f);
		unlink(tmp);
		return -1;
	}
	if (fclose(f) != 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	sync();
	return 0;
}

/*
 * The per-game content builder: walk base_content copying every line
 * verbatim, EXCEPT lines whose key is a schema item — the first occurrence
 * is replaced with "key = <formatted>", later duplicates are dropped — then
 * append, newline-guarded, every schema key that never occurred. Locked
 * lines pass through byte-for-byte (their keys were pruned, so they cannot
 * match a schema item anyway). use_staged switches between item->staged_value
 * and vals[][] so candidate (staged) and reference (console values) come out
 * of the SAME renderer and compare byte-for-byte when equivalent.
 * Returns a malloc'd buffer, NULL on OOM.
 */
static char* render_full(EmuOvlConfig* cfg, const char* base_content,
						 const int vals[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS],
						 bool use_staged) {
	char* out_buf = NULL;
	size_t out_len = 0;
	FILE* out = open_memstream(&out_buf, &out_len);
	if (!out)
		return NULL;

	bool emitted[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS] = {{false}};
	bool ends_nl = true; // empty output counts as newline-terminated
	const char* cursor = base_content;
	char buf[LINE_MAX_LEN];
	while (*cursor) {
		size_t raw_len = peek_line(cursor, buf, sizeof(buf));
		char *key, *val;
		bool lock;
		int s, i;
		if (parse_cfg_line(buf, &key, &val, &lock) && !lock &&
			find_schema_item(cfg, key, &s, &i)) {
			if (!emitted[s][i]) {
				EmuOvlItem* item = &cfg->sections[s].items[i];
				char value[64];
				int v = use_staged ? item->staged_value : vals[s][i];
				emu_ovl_cfg_format_value(item, v, value, sizeof(value));
				fprintf(out, "%s = %s\n", item->key, value);
				emitted[s][i] = true;
				ends_nl = true;
			}
			// later duplicate of a schema key: drop
		} else {
			fwrite(cursor, 1, raw_len, out);
			ends_nl = (cursor[raw_len - 1] == '\n');
		}
		cursor += raw_len;
	}
	for (int s = 0; s < cfg->section_count; s++) {
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			if (emitted[s][i])
				continue;
			if (!ends_nl) {
				fputc('\n', out);
				ends_nl = true;
			}
			EmuOvlItem* item = &cfg->sections[s].items[i];
			char value[64];
			int v = use_staged ? item->staged_value : vals[s][i];
			emu_ovl_cfg_format_value(item, v, value, sizeof(value));
			fprintf(out, "%s = %s\n", item->key, value);
		}
	}
	if (fclose(out) != 0) {
		free(out_buf);
		return NULL;
	}
	return out_buf;
}

int opts_minarch_load(EmuOvlConfig* cfg, OptsMinarchState* st, OptsOverrideState* ost,
					  const char* system_cfg_path, const char* default_cfg_path,
					  const char* config_dir, const char* alt_name) {
	if (!cfg || !st || !ost || !config_dir)
		return -1;
	memset(st, 0, sizeof(*st));
	memset(ost, 0, sizeof(*ost));

	// Empty-string DEVICE counts as unset, matching minarch (config.device_tag
	// is only ever set to a non-empty tag).
	const char* dev = getenv("DEVICE");
	if (dev && !*dev)
		dev = NULL;
	int n = snprintf(st->console_path, sizeof(st->console_path), "%s/minarch%s%s.cfg",
					 config_dir, dev ? "-" : "", dev ? dev : "");
	if (n < 0 || n >= (int)sizeof(st->console_path))
		return -1;
	st->per_game = (alt_name != NULL);
	if (st->per_game) {
		n = snprintf(st->game_path, sizeof(st->game_path), "%s/%s%s%s.cfg",
					 config_dir, alt_name, dev ? "-" : "", dev ? dev : "");
		if (n < 0 || n >= (int)sizeof(st->game_path))
			return -1;
	}

	// System/default paths arrive pre-resolved from the shell; NULL or a
	// missing file is simply an empty layer.
	char* system_content = system_cfg_path ? read_all(system_cfg_path) : NULL;
	char* default_content = default_cfg_path ? read_all(default_cfg_path) : NULL;
	st->console_content = read_all(st->console_path);
	st->game_content = st->per_game ? read_all(st->game_path) : NULL;
	st->game_existed = (st->game_content != NULL);

	// Lock pass over all four layers FIRST, then prune, so every value
	// snapshot below uses stable post-prune indices. A pruned key's lines
	// simply stop matching schema items and pass through writes untouched.
	bool locked[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS] = {{false}};
	apply_locks(cfg, system_content, locked);
	apply_locks(cfg, default_content, locked);
	apply_locks(cfg, st->console_content, locked);
	apply_locks(cfg, st->game_content, locked);
	prune_locked(cfg, locked);

	// base = schema defaults + system + pak default; console layers on top of
	// base; the game file, when present, ALSO layers on top of base — it
	// replaces the console file rather than stacking on it
	// (Config_init, ma_config.c:1383-1396).
	int base[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS];
	for (int s = 0; s < cfg->section_count; s++)
		for (int i = 0; i < cfg->sections[s].item_count; i++)
			base[s][i] = cfg->sections[s].items[i].default_value;
	apply_values(cfg, system_content, base);
	apply_values(cfg, default_content, base);

	memcpy(st->console_value, base, sizeof(st->console_value));
	apply_values(cfg, st->console_content, st->console_value);

	int eff[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS];
	if (st->per_game && st->game_existed) {
		memcpy(eff, base, sizeof(eff));
		apply_values(cfg, st->game_content, eff);
	} else {
		memcpy(eff, st->console_value, sizeof(eff));
	}

	for (int s = 0; s < cfg->section_count; s++) {
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			EmuOvlItem* item = &cfg->sections[s].items[i];
			item->current_value = eff[s][i];
			item->staged_value = eff[s][i];
			item->dirty = false;
			// the shared UI's baseline is always the console tier: the "* "
			// markers, count_overrides and reset-to-global all key off it
			ost->global_value[s][i] = st->console_value[s][i];
			ost->overridden[s][i] = st->per_game && eff[s][i] != st->console_value[s][i];
		}
	}

	free(system_content);
	free(default_content);
	return 0;
}

// Minimal-diff rewrite of the console file: only keys whose staged value
// moved off the loaded effective value are touched (first occurrence
// replaced, later duplicates of a CHANGED key dropped — minarch reads the
// first match, a stale second line would win on the next plain-text edit);
// everything else is byte-for-byte. Missing changed keys are appended,
// newline-guarded. No change at all -> the file is never opened.
static int write_global(EmuOvlConfig* cfg, OptsMinarchState* st) {
	bool changed[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS] = {{false}};
	bool any = false;
	for (int s = 0; s < cfg->section_count; s++) {
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			EmuOvlItem* item = &cfg->sections[s].items[i];
			if (item->staged_value != item->current_value) {
				changed[s][i] = true;
				any = true;
			}
		}
	}
	if (!any)
		return 0;

	char* out_buf = NULL;
	size_t out_len = 0;
	FILE* out = open_memstream(&out_buf, &out_len);
	if (!out)
		return -1;

	bool emitted[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS] = {{false}};
	bool ends_nl = true;
	const char* cursor = st->console_content ? st->console_content : "";
	char buf[LINE_MAX_LEN];
	while (*cursor) {
		size_t raw_len = peek_line(cursor, buf, sizeof(buf));
		char *key, *val;
		bool lock;
		int s, i;
		if (parse_cfg_line(buf, &key, &val, &lock) && !lock &&
			find_schema_item(cfg, key, &s, &i) && changed[s][i]) {
			if (!emitted[s][i]) {
				EmuOvlItem* item = &cfg->sections[s].items[i];
				char value[64];
				emu_ovl_cfg_format_value(item, item->staged_value, value, sizeof(value));
				fprintf(out, "%s = %s\n", item->key, value);
				emitted[s][i] = true;
				ends_nl = true;
			}
		} else {
			fwrite(cursor, 1, raw_len, out);
			ends_nl = (cursor[raw_len - 1] == '\n');
		}
		cursor += raw_len;
	}
	for (int s = 0; s < cfg->section_count; s++) {
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			if (!changed[s][i] || emitted[s][i])
				continue;
			if (!ends_nl) {
				fputc('\n', out);
				ends_nl = true;
			}
			EmuOvlItem* item = &cfg->sections[s].items[i];
			char value[64];
			emu_ovl_cfg_format_value(item, item->staged_value, value, sizeof(value));
			fprintf(out, "%s = %s\n", item->key, value);
		}
	}
	if (fclose(out) != 0) {
		free(out_buf);
		return -1;
	}
	int ret = write_atomic(st->console_path, out_buf);
	free(out_buf);
	return ret;
}

// Full-snapshot per-game file, or none: when the candidate is byte-identical
// to what the console tier would produce anyway, the file is pure redundancy
// and gets unlinked, so the game falls back to tracking the console config
// again (minarch treats file EXISTENCE as "has per-game overrides").
static int write_pergame(EmuOvlConfig* cfg, OptsMinarchState* st) {
	const char* seed = st->game_content		 ? st->game_content
					   : st->console_content ? st->console_content
											 : "";
	char* candidate = render_full(cfg, seed, NULL, true);
	if (!candidate)
		return -1;
	char* reference = render_full(cfg, st->console_content ? st->console_content : "",
								  (const int (*)[EMU_OVL_MAX_ITEMS])st->console_value, false);
	if (!reference) {
		free(candidate);
		return -1;
	}
	int ret = 0;
	if (strcmp(candidate, reference) == 0) {
		if (st->game_existed) {
			unlink(st->game_path); // ENOENT is fine: absent is the goal state
			sync();
		}
	} else {
		ret = write_atomic(st->game_path, candidate);
	}
	free(candidate);
	free(reference);
	return ret;
}

int opts_minarch_save(EmuOvlConfig* cfg, OptsMinarchState* st, const OptsOverrideState* ost) {
	(void)ost; // baseline already lives in st->console_value; kept for symmetry
	if (!cfg || !st)
		return -1;
	return st->per_game ? write_pergame(cfg, st) : write_global(cfg, st);
}

void opts_minarch_free(OptsMinarchState* st) {
	if (!st)
		return;
	free(st->console_content);
	st->console_content = NULL;
	free(st->game_content);
	st->game_content = NULL;
}
