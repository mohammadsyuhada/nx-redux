#include "emu_shortcuts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHORTCUT_NAME_(id, name) [SHORTCUT_##id] = name,
const char* shortcut_names[SHORTCUT_COUNT] = {SHORTCUT_LIST(SHORTCUT_NAME_)};
#undef SHORTCUT_NAME_

// NOTE: these must be in BTN_ID_ order also off by 1 because of NONE (which is -1 in BTN_ID_ land)
char* button_labels[SHORTCUT_LABEL_COUNT + 1] = {
	"NONE", // displayed by default
	"UP",
	"DOWN",
	"LEFT",
	"RIGHT",
	"A",
	"B",
	"X",
	"Y",
	"START",
	"SELECT",
	"L1",
	"R1",
	"L2",
	"R2",
	"L3",
	"R3",
	"L4",
	"R4",
	"MENU+UP",
	"MENU+DOWN",
	"MENU+LEFT",
	"MENU+RIGHT",
	"MENU+A",
	"MENU+B",
	"MENU+X",
	"MENU+Y",
	"MENU+START",
	"MENU+SELECT",
	"MENU+L1",
	"MENU+R1",
	"MENU+L2",
	"MENU+R2",
	"MENU+L3",
	"MENU+R3",
	"MENU+L4",
	"MENU+R4",
	NULL,
};

int EmuShortcuts_find(const char* name) {
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		if (!strcmp(shortcut_names[i], name))
			return i;
	return -1;
}

int EmuShortcuts_labelIndex(const char* label) {
	for (int i = 0; button_labels[i]; i++)
		if (!strcmp(button_labels[i], label))
			return i;
	return -1;
}

int EmuShortcuts_getPath(char* out, size_t size, const char* dir, const char* device_tag) {
	int len;
	if (device_tag && device_tag[0])
		len = snprintf(out, size, "%s/minarch-shortcuts-%s.cfg", dir, device_tag);
	else
		len = snprintf(out, size, "%s/minarch-shortcuts.cfg", dir);
	return len > 0 && (size_t)len < size;
}

// trims trailing spaces/CR in place
static void rtrim(char* s) {
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

void EmuShortcuts_parse(const char* text, int labels[SHORTCUT_COUNT]) {
	if (!text)
		return;
	const char* line = text;
	while (*line) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		char buf[256];
		if (len < sizeof(buf) && !strncmp(line, "bind ", 5)) {
			memcpy(buf, line, len);
			buf[len] = '\0';
			char* eq = strstr(buf, " = ");
			if (eq) {
				*eq = '\0';
				char* name = buf + 5;
				char* value = eq + 3;
				rtrim(name);
				rtrim(value);
				int id = EmuShortcuts_find(name);
				if (id >= 0) {
					int label = EmuShortcuts_labelIndex(value);
					labels[id] = label < 0 ? 0 : label;
				}
			}
		}
		if (!end)
			break;
		line = end + 1;
	}
}

int EmuShortcuts_read(const char* path, int labels[SHORTCUT_COUNT]) {
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		labels[i] = 0;

	FILE* file = fopen(path, "rb");
	if (!file)
		return 0;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size < 0 || size > 64 * 1024) {
		fclose(file);
		return 0;
	}
	char* text = calloc(1, size + 1);
	if (!text) {
		fclose(file);
		return 0;
	}
	size_t got = fread(text, 1, size, file);
	text[got] = '\0';
	fclose(file);

	EmuShortcuts_parse(text, labels);
	free(text);
	return 1;
}

int EmuShortcuts_write(const char* path, const int labels[SHORTCUT_COUNT]) {
	// write a sibling and rename it over: a power cut mid-write never leaves
	// a half file that would read as every shortcut cleared
	char tmp_path[1024];
	int len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	if (len <= 0 || (size_t)len >= sizeof(tmp_path))
		return 0;

	FILE* file = fopen(tmp_path, "wb");
	if (!file)
		return 0;
	for (int i = 0; i < SHORTCUT_COUNT; i++) {
		int label = labels[i];
		if (label < 0 || label >= SHORTCUT_LABEL_COUNT)
			label = 0;
		fprintf(file, "bind %s = %s\n", shortcut_names[i], button_labels[label]);
	}
	int ok = fflush(file) == 0;
	ok = (fsync(fileno(file)) == 0) && ok;
	ok = (fclose(file) == 0) && ok;
	if (!ok || rename(tmp_path, path) != 0) {
		unlink(tmp_path);
		return 0;
	}
	sync();
	return 1;
}
