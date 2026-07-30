#include "opts_override.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void opts_snapshot_globals(const EmuOvlConfig* cfg, OptsOverrideState* st) {
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
	if (!cfg)
		return;
	for (int s = 0; s < cfg->section_count; s++)
		for (int i = 0; i < cfg->sections[s].item_count; i++)
			st->global_value[s][i] = cfg->sections[s].items[i].current_value;
}

static char* trim(char* s) {
	while (isspace((unsigned char)*s))
		s++;
	char* end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

int opts_read_override(EmuOvlConfig* cfg, OptsOverrideState* st, const char* override_path) {
	if (!cfg || !st || !override_path)
		return 0;
	FILE* f = fopen(override_path, "r");
	if (!f)
		return 0;
	char line[512];
	char section[EMU_OVL_MAX_STR] = "";
	int applied = 0;
	while (fgets(line, sizeof(line), f)) {
		char* p = trim(line);
		if (*p == '\0' || *p == '#' || *p == ';')
			continue;
		size_t len = strlen(p);
		if (*p == '[' && p[len - 1] == ']') {
			p[len - 1] = '\0';
			snprintf(section, sizeof(section), "%s", p + 1);
			continue;
		}
		char* eq = strchr(p, '=');
		if (!eq || section[0] == '\0')
			continue;
		*eq = '\0';
		char* key = trim(p);
		char* val = trim(eq + 1);
		int s = -1, i = -1;
		EmuOvlItem* item = emu_ovl_cfg_find_item(cfg, section, key, &s, &i);
		if (!item)
			continue;
		int value;
		if (!emu_ovl_cfg_parse_value(item, val, &value))
			continue;
		item->current_value = value;
		item->staged_value = value;
		st->overridden[s][i] = true;
		applied++;
	}
	fclose(f);
	return applied;
}

int opts_write_override(EmuOvlConfig* cfg, const OptsOverrideState* st, const char* override_path) {
	if (!cfg || !st || !override_path)
		return -1;
	// collect the diff first so an all-clean state unlinks instead of writing
	int diff_count = 0;
	for (int s = 0; s < cfg->section_count; s++)
		for (int i = 0; i < cfg->sections[s].item_count; i++)
			if (cfg->sections[s].items[i].staged_value != st->global_value[s][i])
				diff_count++;
	if (diff_count == 0) {
		unlink(override_path);
		return 0;
	}
	FILE* f = fopen(override_path, "w");
	if (!f)
		return -1;
	// group by resolved ini_section name, first-seen order, no duplicates
	const char* done[EMU_OVL_MAX_SECTIONS] = {0};
	int done_count = 0;
	int written = 0;
	for (int s = 0; s < cfg->section_count; s++) {
		const char* name = emu_ovl_cfg_section_name(cfg, s);
		bool seen = false;
		for (int d = 0; d < done_count; d++)
			if (strcmp(done[d], name) == 0)
				seen = true;
		if (seen)
			continue;
		done[done_count++] = name;
		// does any schema section resolving to `name` have a diff?
		bool any = false;
		for (int s2 = 0; s2 < cfg->section_count; s2++) {
			if (strcmp(emu_ovl_cfg_section_name(cfg, s2), name) != 0)
				continue;
			for (int i = 0; i < cfg->sections[s2].item_count; i++)
				if (cfg->sections[s2].items[i].staged_value != st->global_value[s2][i])
					any = true;
		}
		if (!any)
			continue;
		fprintf(f, "[%s]\n", name);
		for (int s2 = 0; s2 < cfg->section_count; s2++) {
			if (strcmp(emu_ovl_cfg_section_name(cfg, s2), name) != 0)
				continue;
			for (int i = 0; i < cfg->sections[s2].item_count; i++) {
				EmuOvlItem* item = &cfg->sections[s2].items[i];
				if (item->staged_value == st->global_value[s2][i])
					continue;
				char value[64];
				emu_ovl_cfg_format_value(item, item->staged_value, value, sizeof(value));
				// single spaces around '=' are load-bearing: flycast and
				// DC.pak/launch.sh's sed patterns both expect "key = value"
				fprintf(f, "%s = %s\n", item->key, value);
				written++;
			}
		}
		fprintf(f, "\n");
	}
	fclose(f);
	sync();
	return written;
}

void opts_commit(EmuOvlConfig* cfg, OptsOverrideState* st) {
	if (!cfg || !st)
		return;
	for (int s = 0; s < cfg->section_count; s++) {
		for (int i = 0; i < cfg->sections[s].item_count; i++) {
			EmuOvlItem* item = &cfg->sections[s].items[i];
			st->overridden[s][i] = item->staged_value != st->global_value[s][i];
			// emu_ovl_cfg_apply_staged only commits items flagged dirty, and
			// the options editor may have moved staged_value without setting
			// the flag (opts_read_override does exactly that), so flag
			// anything that actually moved before delegating.
			if (item->staged_value != item->current_value)
				item->dirty = true;
		}
	}
	emu_ovl_cfg_apply_staged(cfg);
}
