#include "shortcuts.h"
#include "api.h"
#include "defines.h"
#include "menutabs_model.h"
#include "utils.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////////////
// Internal types

typedef struct Shortcut {
	char* path; // without SDCARD_PATH prefix
	char* name; // display name
} Shortcut;

///////////////////////////////////////
// Shortcut functions

static Shortcut* Shortcut_new(char* path, char* name) {
	Shortcut* self = malloc(sizeof(Shortcut));
	self->path = strdup(path);
	self->name = name ? strdup(name) : NULL;
	return self;
}

static void Shortcut_free(Shortcut* self) {
	free(self->path);
	if (self->name)
		free(self->name);
	free(self);
}

static int ShortcutArray_indexOf(Array* self, const char* path) {
	for (int i = 0; i < self->count; i++) {
		Shortcut* shortcut = self->items[i];
		if (exactMatch(shortcut->path, path))
			return i;
	}
	return -1;
}

static void ShortcutArray_free(Array* self) {
	for (int i = 0; i < self->count; i++) {
		Shortcut_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////
// Global state

static Array* shortcuts = NULL;

///////////////////////////////////////
// Save/Load functions

static void saveShortcuts(void) {
	// one buffer, written via writeFileAtomic (tmp + fsync + rename): a crash or a full card mid-write can't
	// truncate the pins
	size_t cap = 1, len = 0;
	for (int i = 0; i < shortcuts->count; i++) {
		Shortcut* shortcut = shortcuts->items[i];
		cap += strlen(shortcut->path) + 2 + (shortcut->name ? strlen(shortcut->name) : 0);
	}
	char* buf = malloc(cap);
	if (!buf)
		return;
	for (int i = 0; i < shortcuts->count; i++) {
		Shortcut* shortcut = shortcuts->items[i];
		len += (size_t)snprintf(buf + len, cap - len, "%s%s%s\n", shortcut->path, shortcut->name ? "\t" : "",
								shortcut->name ? shortcut->name : "");
	}
	buf[len] = '\0';
	if (!writeFileAtomic(SHORTCUTS_PATH, buf, len))
		LOG_warn("Shortcuts: couldn't save %s\n", SHORTCUTS_PATH);
	free(buf);
}

static int loadShortcuts(void) {
	if (shortcuts) {
		ShortcutArray_free(shortcuts);
	}
	shortcuts = Array_new();
	bool removed_any = false;

	FILE* file = fopen(SHORTCUTS_PATH, "r");
	if (file) {
		char line[MAX_PATH * 2]; // "path\tname": each up to a path's length
		while (fgets(line, sizeof(line), file) != NULL) {
			size_t len = strlen(line);
			if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
				// longer than any path plus name: skip the whole line rather than split it, which would read
				// its tail as a bogus entry (and a re-save would keep only the cut-down head)
				int c;
				while ((c = fgetc(file)) != EOF && c != '\n')
					;
				continue;
			}
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line) == 0)
				continue;

			char* path = line;
			char* name = NULL;
			char* tmp = strchr(line, '\t');
			if (tmp) {
				tmp[0] = '\0';
				name = tmp + 1;
			}
			if (strlen(path) >= MAX_PATH)
				continue; // no such path on the card

			// Validate that the tool still exists
			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path);

			if (exists(sd_path)) {
				Array_push(shortcuts, Shortcut_new(path, name));
			} else {
				removed_any = true;
			}
		}
		fclose(file);
	}

	// Kept in stored (pin) order: no sorting.

	// Auto-clean: re-save if any were removed
	if (removed_any)
		saveShortcuts();

	return shortcuts->count > 0;
}

///////////////////////////////////////
// Public API

void Shortcuts_init(void) {
	loadShortcuts();
}

void Shortcuts_quit(void) {
	if (shortcuts) {
		ShortcutArray_free(shortcuts);
		shortcuts = NULL;
	}
}

int Shortcuts_exists(const char* path) {
	if (!shortcuts)
		return 0;
	return ShortcutArray_indexOf(shortcuts, path) != -1;
}

bool Shortcuts_canAdd(Entry* entry) {
	if (!shortcuts || !entry || shortcuts->count >= MAX_SHORTCUTS)
		return false;
	if (entry->type != ENTRY_PAK)
		return true;
	int tools = 0, games = 0;
	for (int i = 0; i < shortcuts->count; i++) {
		Shortcut* shortcut = shortcuts->items[i];
		if (suffixMatch(".pak", shortcut->path))
			tools++;
		else
			games++;
	}
	return tools < (games > 0 ? MAX_PINNED_TOOLS : MAX_PINNED_TOOLS_NO_GAMES);
}

void Shortcuts_add(Entry* entry) {
	if (!shortcuts || !entry)
		return;
	if (!MenuTabs_underPath(entry->path, SDCARD_PATH))
		return;

	char* path = entry->path + strlen(SDCARD_PATH);
	if (Shortcuts_exists(path))
		return;

	if (!Shortcuts_canAdd(entry)) {
		// refuse rather than silently evict a pin the user placed
		LOG_warn("Shortcuts_add: pin limit reached, not adding %s\n", path);
		return;
	}
	Array_push(shortcuts, Shortcut_new(path, entry->name)); // appended: pins keep the order they were added
	saveShortcuts();
}

void Shortcuts_remove(Entry* entry) {
	if (!shortcuts || !entry)
		return;
	if (!MenuTabs_underPath(entry->path, SDCARD_PATH))
		return;

	char* path = entry->path + strlen(SDCARD_PATH);
	int idx = ShortcutArray_indexOf(shortcuts, path);
	if (idx != -1) {
		Shortcut* shortcut = shortcuts->items[idx];
		Array_remove(shortcuts, shortcut);
		Shortcut_free(shortcut);
		saveShortcuts();
	}
}

bool Shortcuts_replacePath(const char* old_path, const char* new_path, const char* new_name) {
	if (!shortcuts || !old_path || !new_path)
		return false;
	int idx = ShortcutArray_indexOf(shortcuts, old_path);
	if (idx == -1)
		return false;
	Shortcut* shortcut = shortcuts->items[idx];
	free(shortcut->path);
	shortcut->path = strdup(new_path); // same slot: the pin keeps its place
	if (new_name) {
		free(shortcut->name);
		shortcut->name = strdup(new_name);
	}
	saveShortcuts();
	return true;
}

int Shortcuts_isInToolsFolder(const char* path) {
	char paks_tools_path[MAX_PATH];
	snprintf(paks_tools_path, sizeof(paks_tools_path), "%s/Tools", PAKS_PATH);
	return MenuTabs_underPath(path, TOOLS_PATH) || MenuTabs_underPath(path, paks_tools_path);
}

int Shortcuts_getCount(void) {
	return shortcuts ? shortcuts->count : 0;
}

char* Shortcuts_getPath(int index) {
	if (!shortcuts || index < 0 || index >= shortcuts->count)
		return NULL;
	Shortcut* shortcut = shortcuts->items[index];
	return shortcut->path;
}

char* Shortcuts_getName(int index) {
	if (!shortcuts || index < 0 || index >= shortcuts->count)
		return NULL;
	Shortcut* shortcut = shortcuts->items[index];
	return shortcut->name;
}

int Shortcuts_validate(void) {
	if (!shortcuts)
		return 0;

	bool needs_save = false;
	for (int i = shortcuts->count - 1; i >= 0; i--) {
		Shortcut* shortcut = shortcuts->items[i];
		char sd_path[MAX_PATH];
		snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, shortcut->path);

		if (!exists(sd_path)) {
			Array_remove(shortcuts, shortcut);
			Shortcut_free(shortcut);
			needs_save = true;
		}
	}

	if (needs_save)
		saveShortcuts();
	return needs_save;
}

char* Shortcuts_getPakBasename(const char* path) {
	static char basename[STR_MAX];

	// Extract filename from path
	const char* pakname = strrchr(path, '/');
	pakname = pakname ? pakname + 1 : path;

	// Copy and remove .pak extension
	strncpy(basename, pakname, sizeof(basename) - 1);
	basename[sizeof(basename) - 1] = '\0';
	char* dot = strrchr(basename, '.');
	if (dot)
		*dot = '\0';

	return basename;
}
