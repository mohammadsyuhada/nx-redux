#include <dirent.h>
#include <sys/stat.h>
#include "recents.h"
#include "defines.h"
#include "utils.h"
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include "content.h"
#include "shortcuts.h"
#include "config.h"

static bool _simple_mode = false;

// EMULIST_CACHE_PATH and ROMINDEX_CACHE_PATH defined in defines.h

void Content_setSimpleMode(bool mode) {
	_simple_mode = mode;
}

///////////////////////////////////////
// Helpers

int getIndexChar(char* str) {
	char i = 0;
	char c = tolower(str[0]);
	if (c >= 'a' && c <= 'z')
		i = (c - 'a') + 1;
	return i;
}

void getUniqueName(Entry* entry, char* out_name) {
	char* slash = strrchr(entry->path, '/');
	if (!slash)
		return;
	char emu_tag[MAX_PATH];
	getEmuName(entry->path, emu_tag);
	snprintf(out_name, MAX_PATH, "%s (%s)", entry->name, emu_tag);
}

///////////////////////////////////////
// Directory indexing

void Directory_index(Directory* self) {
	int is_collection = prefixMatch(COLLECTIONS_PATH, self->path);
	int skip_index = exactMatch(FAUX_RECENT_PATH, self->path) || is_collection; // not alphabetized

	Hash* map = NULL;
	char map_path[MAX_PATH];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", is_collection ? COLLECTIONS_PATH : self->path);

	if (exists(map_path)) {
		FILE* file = fopen(map_path, "r");
		if (file) {
			map = Hash_new();
			char line[MAX_PATH];
			while (fgets(line, sizeof(line), file) != NULL) {
				normalizeNewline(line);
				trimTrailingNewlines(line);
				if (strlen(line) == 0)
					continue; // skip empty lines

				char* tmp = strchr(line, '\t');
				if (tmp) {
					tmp[0] = '\0';
					char* key = line;
					char* value = tmp + 1;
					Hash_set(map, key, value);
				}
			}
			fclose(file);

			bool resort = false;
			bool filter = false;
			for (int i = 0; i < self->entries->count; i++) {
				Entry* entry = self->entries->items[i];
				char* slash = strrchr(entry->path, '/');
				if (!slash)
					continue;
				char* filename = slash + 1;
				char* alias = Hash_get(map, filename);
				if (alias) {
					free(entry->name);
					entry->name = strdup(alias);
					resort = true;
					if (!filter && hide(entry->name))
						filter = true;
				}
			}

			if (filter) {
				Array* entries = Array_new();
				for (int i = 0; i < self->entries->count; i++) {
					Entry* entry = self->entries->items[i];
					if (hide(entry->name)) {
						Entry_free(entry);
					} else {
						Array_push(entries, entry);
					}
				}
				Array_free(self->entries);
				self->entries = entries;
			}
			if (resort)
				EntryArray_sort(self->entries);
		}
	}

	Entry* prior = NULL;
	int alpha = -1;
	int index = 0;
	for (int i = 0; i < self->entries->count; i++) {
		Entry* entry = self->entries->items[i];

		if (prior != NULL && exactMatch(prior->name, entry->name)) {
			free(prior->unique);
			free(entry->unique);
			prior->unique = NULL;
			entry->unique = NULL;

			char* prior_slash = strrchr(prior->path, '/');
			char* entry_slash = strrchr(entry->path, '/');
			if (prior_slash && entry_slash) {
				char* prior_filename = prior_slash + 1;
				char* entry_filename = entry_slash + 1;
				if (exactMatch(prior_filename, entry_filename)) {
					char prior_unique[MAX_PATH] = {0};
					char entry_unique[MAX_PATH] = {0};
					getUniqueName(prior, prior_unique);
					getUniqueName(entry, entry_unique);

					prior->unique = strdup(prior_unique);
					entry->unique = strdup(entry_unique);
				} else {
					prior->unique = strdup(prior_filename);
					entry->unique = strdup(entry_filename);
				}
			}
		}

		if (!skip_index) {
			int a = getIndexChar(entry->name);
			if (a != alpha) {
				index = self->alphas.count;
				IntArray_push(&self->alphas, i);
				alpha = a;
			}
			entry->alpha = index;
		}

		prior = entry;
	}

	if (map)
		Hash_free(map);
}

///////////////////////////////////////
// Directory construction

Directory* Directory_new(char* path, int selected) {
	char display_name[MAX_PATH];
	getDisplayName(path, display_name);

	Directory* self = malloc(sizeof(Directory));
	self->path = strdup(path);
	self->name = strdup(display_name);
	if (exactMatch(path, SDCARD_PATH)) {
		self->entries = getRoot(_simple_mode);
	} else if (exactMatch(path, FAUX_RECENT_PATH)) {
		self->entries = Recents_getEntries();
	} else if (exactMatch(path, ROMS_PATH)) {
		self->entries = getRoms();
	} else if (!exactMatch(path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, path) && suffixMatch(".txt", path)) {
		self->entries = getCollection(path);
	} else if (suffixMatch(".m3u", path)) {
		self->entries = getDiscs(path);
	} else if (exactMatch(path, TOOLS_PATH)) {
		self->entries = getTools();
	} else {
		self->entries = getEntries(path);
	}
	IntArray_init(&self->alphas);
	self->selected = selected;
	Directory_index(self);
	return self;
}

///////////////////////////////////////
// Content query helpers

Entry* entryFromPakName(char* pak_name) {
	char pak_path[MAX_PATH];
	// Check in Tools
	snprintf(pak_path, sizeof(pak_path), "%s/%s.pak", TOOLS_PATH, pak_name);
	if (exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in platform Tools (MinUI community pak convention)
	snprintf(pak_path, sizeof(pak_path), "%s/" PLATFORM "/%s.pak", TOOLS_PATH, pak_name);
	if (exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in system Tools
	snprintf(pak_path, sizeof(pak_path), "%s/Tools/%s.pak", PAKS_PATH, pak_name);
	if (exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in Emus
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/%s.pak", PAKS_PATH, pak_name);
	if (exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in SD Emus
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/%s.pak", SDCARD_PATH, pak_name);
	if (exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in platform Emus (MinUI community pak convention)
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/" PLATFORM "/%s.pak", SDCARD_PATH, pak_name);
	if (exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	return NULL;
}

int hasEmu(char* emu_name) {
	char pak_path[MAX_PATH];
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
	if (exists(pak_path))
		return 1;

	snprintf(pak_path, sizeof(pak_path), "%s/Emus/%s.pak/launch.sh", SDCARD_PATH, emu_name);
	if (exists(pak_path))
		return 1;

	// community paks use the MinUI platform-subfolder convention
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/" PLATFORM "/%s.pak/launch.sh", SDCARD_PATH, emu_name);
	return exists(pak_path);
}

int hasCue(char* dir_path, char* cue_path) { // NOTE: dir_path not rom_path
	char* slash = strrchr(dir_path, '/');
	if (!slash)
		return 0;
	char* tmp = slash + 1;
	snprintf(cue_path, MAX_PATH, "%s/%s.cue", dir_path, tmp);
	return exists(cue_path);
}

int hasM3u(char* rom_path, char* m3u_path) { // NOTE: rom_path not dir_path
	char work[MAX_PATH];
	strncpy(work, rom_path, MAX_PATH - 1);
	work[MAX_PATH - 1] = '\0';

	// strip filename to get parent dir (e.g. /Roms/PSX/Game/disc1.bin → /Roms/PSX/Game)
	char* tmp = strrchr(work, '/');
	if (!tmp)
		return 0;
	tmp[0] = '\0';

	// get directory name (e.g. "Game" from /Roms/PSX/Game)
	char* dir_name = strrchr(work, '/');
	if (!dir_name)
		return 0;
	dir_name++; // skip the slash

	// build m3u path: parent_dir + "/" + dir_name + ".m3u"
	// e.g. /Roms/PSX/Game/Game.m3u
	snprintf(m3u_path, MAX_PATH, "%s/%s.m3u", work, dir_name);

	return exists(m3u_path);
}

int dirGameFile(const char* dir_path, char* out_path) {
	// A directory is a "folder game" when it contains a folder-named .cue or
	// .m3u, e.g. /Roms/PSX/Game → /Roms/PSX/Game/Game.cue. Cue wins over m3u,
	// matching openDirectory's auto-launch order. out_path (>= MAX_PATH)
	// receives the resolved file — the entry's effective ROM for actions.
	char* dir_name = strrchr(dir_path, '/');
	if (!dir_name)
		return 0;
	snprintf(out_path, MAX_PATH, "%s%s.cue", dir_path, dir_name);
	if (exists(out_path))
		return 1;
	snprintf(out_path, MAX_PATH, "%s%s.m3u", dir_path, dir_name);
	return exists(out_path);
}

int canPinEntry(Entry* entry) {
	// PAK and ROM can always be pinned
	if (entry->type == ENTRY_PAK || entry->type == ENTRY_ROM) {
		return 1;
	}
	// ENTRY_DIR can be pinned only if it has a .cue or .m3u file (multi-disc game)
	if (entry->type == ENTRY_DIR) {
		char game_path[MAX_PATH];
		return dirGameFile(entry->path, game_path);
	}
	return 0;
}

int hasCollections(void) {
	int has = 0;
	if (!exists(COLLECTIONS_PATH))
		return has;

	DIR* dh = opendir(COLLECTIONS_PATH);
	if (!dh)
		return has;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (hide(dp->d_name))
			continue;
		has = 1;
		break;
	}
	closedir(dh);
	return has;
}

int hasRoms(char* dir_name) {
	int has = 0;
	char emu_name[MAX_PATH];
	char rom_path[MAX_PATH];

	getEmuName(dir_name, emu_name);

	// check for emu pak
	if (!hasEmu(emu_name))
		return has;

	// check for at least one non-hidden file (we're going to assume it's a rom)
	snprintf(rom_path, sizeof(rom_path), "%s/%s/", ROMS_PATH, dir_name);
	DIR* dh = opendir(rom_path);
	if (dh != NULL) {
		struct dirent* dp;
		while ((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name))
				continue;
			has = 1;
			break;
		}
		closedir(dh);
	}
	return has;
}

int hasTools(void) {
	char sys_path[MAX_PATH];
	snprintf(sys_path, sizeof(sys_path), "%s/Tools", PAKS_PATH);
	return exists(TOOLS_PATH) || exists(sys_path);
}

int isConsoleDir(char* path) {
	char parent_dir[MAX_PATH];
	strncpy(parent_dir, path, MAX_PATH - 1);
	parent_dir[MAX_PATH - 1] = '\0';
	char* tmp = strrchr(parent_dir, '/');
	if (!tmp)
		return 0;
	tmp[0] = '\0';

	return exactMatch(parent_dir, ROMS_PATH);
}

///////////////////////////////////////
// Content retrieval

// The caches persist across boots, so they are only trusted while nothing
// they were built from has changed: the console dirs under Roms (rom
// add/remove bumps the dir mtime), Roms/map.txt (console aliases), and the
// Emus pak roots consulted by hasEmu (pak add/remove). In-app mutations go
// through Content_invalidateEmulist and don't rely on this check.
static int cacheIsStale(const char* cache_path) {
	struct stat st;
	if (stat(cache_path, &st) != 0)
		return 1;
	time_t cache_mtime = st.st_mtime;

	static const char* const source_paths[] = {
		ROMS_PATH,
		ROMS_PATH "/map.txt",
		PAKS_PATH "/Emus",
		SDCARD_PATH "/Emus",
		SDCARD_PATH "/Emus/" PLATFORM,
	};
	for (size_t i = 0; i < sizeof(source_paths) / sizeof(source_paths[0]); i++) {
		if (stat(source_paths[i], &st) == 0 && st.st_mtime > cache_mtime)
			return 1;
	}

	DIR* dh = opendir(ROMS_PATH);
	if (!dh)
		return 1;
	struct dirent* dp;
	char path[MAX_PATH];
	while ((dp = readdir(dh)) != NULL) {
		if (hide(dp->d_name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", ROMS_PATH, dp->d_name);
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_mtime > cache_mtime) {
			closedir(dh);
			return 1;
		}
	}
	closedir(dh);
	return 0;
}

static void writeRomsCache(Array* entries) {
	FILE* file = fopen(EMULIST_CACHE_PATH, "w");
	if (!file)
		return;
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		fputs(entry->path, file);
		fputc('\t', file);
		fputs(entry->name, file);
		fputc('\n', file);
	}
	fclose(file);
}

static Array* readRomsCache(void) {
	if (cacheIsStale(EMULIST_CACHE_PATH))
		return NULL;
	// Both caches are written together by getRoms; a missing rom index with a
	// valid emulist would satisfy the menu but leave Search permanently empty,
	// so force the full rebuild that restores both.
	if (!exists(ROMINDEX_CACHE_PATH))
		return NULL;

	FILE* file = fopen(EMULIST_CACHE_PATH, "r");
	if (!file)
		return NULL;

	Array* entries = Array_new();
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0)
			continue;

		char* tab = strchr(line, '\t');
		if (!tab) {
			EntryArray_free(entries);
			fclose(file);
			return NULL; // malformed cache, force rescan
		}
		*tab = '\0';
		char* path = line;
		char* name = tab + 1;
		Array_push(entries, Entry_newNamed(path, ENTRY_DIR, name));
	}
	fclose(file);
	return entries;
}

static void writeRomIndexCache(Array* entries) {
	FILE* file = fopen(ROMINDEX_CACHE_PATH, "w");
	if (!file)
		return;
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		fputs(entry->path, file);
		fputc('\t', file);
		fputs(entry->name, file);
		fputc('\n', file);
	}
	fclose(file);
}

static Array* readRomIndexCache(void) {
	if (cacheIsStale(ROMINDEX_CACHE_PATH))
		return NULL;

	FILE* file = fopen(ROMINDEX_CACHE_PATH, "r");
	if (!file)
		return NULL;

	Array* entries = Array_new();
	char line[MAX_PATH * 2];
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0)
			continue;

		char* tab = strchr(line, '\t');
		if (!tab) {
			EntryArray_free(entries);
			fclose(file);
			return NULL;
		}
		*tab = '\0';
		char* path = line;
		char* name = tab + 1;
		Array_push(entries, Entry_newNamed(path, ENTRY_ROM, name));
	}
	fclose(file);
	return entries;
}

void Content_invalidateEmulist(void) {
	unlink(EMULIST_CACHE_PATH);
	unlink(ROMINDEX_CACHE_PATH);
}

Array* Content_searchRoms(const char* query) {
	Array* all_roms = readRomIndexCache();
	if (!all_roms) {
		// Force a build by calling getRoms() (which populates both caches)
		Array* consoles = getRoms();
		EntryArray_free(consoles);
		all_roms = readRomIndexCache();
		if (!all_roms)
			return Array_new();
	}

	if (!query || strlen(query) == 0)
		return all_roms;

	Array* results = Array_new();
	for (int i = 0; i < all_roms->count; i++) {
		Entry* entry = all_roms->items[i];
		if (containsString(entry->name, (char*)query)) {
			Array_push(results, entry);
		} else {
			Entry_free(entry);
		}
	}
	Array_free(all_roms);
	return results;
}

Array* getRoms(void) {
	// Try loading from cache first
	Array* entries = readRomsCache();
	if (entries)
		return entries;

	// Cache miss: full filesystem scan
	entries = Array_new();
	DIR* dh = opendir(ROMS_PATH);
	if (dh) {
		struct dirent* dp;
		char full_path[MAX_PATH];
		snprintf(full_path, sizeof(full_path), "%s/", ROMS_PATH);
		char* tmp = full_path + strlen(full_path);

		Array* emus = Array_new();
		size_t remaining = sizeof(full_path) - (tmp - full_path);
		while ((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name))
				continue;
			if (hasRoms(dp->d_name)) {
				strncpy(tmp, dp->d_name, remaining - 1);
				full_path[MAX_PATH - 1] = '\0';
				Array_push(emus, Entry_new(full_path, ENTRY_DIR));
			}
		}
		closedir(dh);

		EntryArray_sort(emus);
		Entry* prev_entry = NULL;
		for (int i = 0; i < emus->count; i++) {
			Entry* entry = emus->items[i];
			if (prev_entry && exactMatch(prev_entry->name, entry->name)) {
				Entry_free(entry);
				continue;
			}
			Array_push(entries, entry);
			prev_entry = entry;
		}
		Array_free(emus);
	}

	// Handle mapping logic
	char map_path[MAX_PATH];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", ROMS_PATH);
	if (entries->count > 0 && exists(map_path)) {
		FILE* file = fopen(map_path, "r");
		if (file) {
			Hash* map = Hash_new();
			char line[MAX_PATH];

			while (fgets(line, sizeof(line), file)) {
				normalizeNewline(line);
				trimTrailingNewlines(line);
				if (strlen(line) == 0)
					continue;

				char* tmp = strchr(line, '\t');
				if (tmp) {
					*tmp = '\0';
					char* key = line;
					char* value = tmp + 1;
					Hash_set(map, key, value);
				}
			}
			fclose(file);

			bool resort = false;
			for (int i = 0; i < entries->count; i++) {
				Entry* entry = entries->items[i];
				char* slash = strrchr(entry->path, '/');
				if (!slash)
					continue;
				char* filename = slash + 1;
				char* alias = Hash_get(map, filename);
				if (alias) {
					free(entry->name);
					entry->name = strdup(alias);
					resort = true;
				}
			}
			if (resort)
				EntryArray_sort(entries);
			Hash_free(map);
		}
	}

	// Build ROM index: scan all console dirs for individual ROMs
	{
		Array* rom_index = Array_new();
		for (int i = 0; i < entries->count; i++) {
			Entry* console_entry = entries->items[i];
			char* console_name = console_entry->name;

			DIR* rom_dh = opendir(console_entry->path);
			if (!rom_dh)
				continue;

			struct dirent* rom_dp;
			char rom_path[MAX_PATH];
			while ((rom_dp = readdir(rom_dh)) != NULL) {
				if (hide(rom_dp->d_name))
					continue;
				if (rom_dp->d_type == DT_DIR)
					continue;

				snprintf(rom_path, sizeof(rom_path), "%s/%s",
						 console_entry->path, rom_dp->d_name);

				char display_name[MAX_PATH];
				getDisplayName(rom_path, display_name);
				char full_display[MAX_PATH];
				snprintf(full_display, sizeof(full_display), "%s (%s)",
						 display_name, console_name);

				Array_push(rom_index, Entry_newNamed(rom_path, ENTRY_ROM, full_display));
			}
			closedir(rom_dh);
		}
		EntryArray_sort(rom_index);
		writeRomIndexCache(rom_index);
		EntryArray_free(rom_index);
	}

	// Write cache for next launch
	writeRomsCache(entries);

	return entries;
}

Array* getCollections(void) {
	Array* collections = Array_new();
	DIR* dh = opendir(COLLECTIONS_PATH);
	if (dh) {
		struct dirent* dp;
		char full_path[MAX_PATH];
		snprintf(full_path, sizeof(full_path), "%s/", COLLECTIONS_PATH);
		char* tmp = full_path + strlen(full_path);

		size_t remaining = sizeof(full_path) - (tmp - full_path);
		while ((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name))
				continue;
			strncpy(tmp, dp->d_name, remaining - 1);
			full_path[MAX_PATH - 1] = '\0';
			Array_push(collections, Entry_new(full_path, ENTRY_DIR));
		}
		closedir(dh);
		EntryArray_sort(collections);
	}
	return collections;
}

Array* getRoot(int simple_mode) {
	Array* root = Array_new();

	if (Recents_load() && CFG_getShowRecents())
		Array_push(root, Entry_new(FAUX_RECENT_PATH, ENTRY_DIR));

	Array* entries = getRoms();

	// Handle collections
	if (hasCollections() && CFG_getShowCollections()) {
		if (entries->count) {
			Array_push(root, Entry_new(COLLECTIONS_PATH, ENTRY_DIR));
		} else { // No visible systems, promote collections to root
			// yoink into root directly — yoinking into `entries` meant the
			// promoted collections never appeared when emulators were hidden
			Array* collections = getCollections();
			Array_yoink(root, collections);
		}
	}

	// Add shortcuts (after Recents and Collections, before user root folders)
	if (Shortcuts_getCount() > 0) {
		Shortcuts_validate();
		for (int i = 0; i < Shortcuts_getCount(); i++) {
			char* path = Shortcuts_getPath(i);
			char* name = Shortcuts_getName(i);
			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path);

			// Determine entry type based on path
			int type;
			if (suffixMatch(".pak", sd_path)) {
				type = ENTRY_PAK;
			} else {
				DIR* dh = opendir(sd_path);
				if (dh) {
					closedir(dh);
					type = ENTRY_DIR;
				} else {
					type = ENTRY_ROM;
				}
			}

			Entry* entry = Entry_new(sd_path, type);
			if (name) {
				free(entry->name);
				entry->name = strdup(name);
			}
			Array_push(root, entry);
		}
	}

	if (CFG_getShowEmulators()) {
		// Move entries to root
		Array_yoink(root, entries);
	} else {
		// emulators hidden: `entries` never reaches root, so free it (and its
		// Entry items) here instead of leaking the whole console list on every
		// root rebuild
		EntryArray_free(entries);
	}

	// Add tools if applicable
	if (hasTools() && CFG_getShowTools() && !simple_mode) {
		Array_push(root, Entry_new(TOOLS_PATH, ENTRY_DIR));
	} else if (simple_mode) {
		// Simple mode hides Tools, but Settings must stay reachable (the
		// game list PIN-gates it) or parents get locked out of the device.
		char settings_path[MAX_PATH];
		snprintf(settings_path, sizeof(settings_path), "%s/Settings.pak", TOOLS_PATH);
		if (!exists(settings_path))
			snprintf(settings_path, sizeof(settings_path), "%s/Tools/Settings.pak", PAKS_PATH);
		if (exists(settings_path))
			Array_push(root, Entry_newNamed(settings_path, ENTRY_PAK, "Settings"));
	}

	return root;
}

Array* getCollection(char* path) {
	Array* entries = Array_new();
	FILE* file = fopen(path, "r");
	if (file) {
		char line[MAX_PATH];
		while (fgets(line, sizeof(line), file) != NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line) == 0)
				continue;

			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, line);
			if (exists(sd_path)) {
				int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM;
				Array_push(entries, Entry_new(sd_path, type));
			}
		}
		fclose(file);
	}
	return entries;
}

Array* getDiscs(char* path) {
	Array* entries = Array_new();

	char base_path[MAX_PATH];
	strncpy(base_path, path, MAX_PATH - 1);
	base_path[MAX_PATH - 1] = '\0';
	char* slash = strrchr(base_path, '/');
	if (!slash)
		return entries;
	slash[1] = '\0';

	FILE* file = fopen(path, "r");
	if (file) {
		char line[MAX_PATH];
		int disc = 0;
		while (fgets(line, sizeof(line), file) != NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line) == 0)
				continue;

			char disc_path[MAX_PATH];
			snprintf(disc_path, sizeof(disc_path), "%s%s", base_path, line);

			if (exists(disc_path)) {
				disc += 1;
				Entry* entry = Entry_new(disc_path, ENTRY_ROM);
				free(entry->name);
				char name[16];
				sprintf(name, "Disc %i", disc);
				entry->name = strdup(name);
				Array_push(entries, entry);
			}
		}
		fclose(file);
	}
	return entries;
}

int getFirstDisc(char* m3u_path, char* disc_path) {
	int found = 0;

	char base_path[MAX_PATH];
	strncpy(base_path, m3u_path, MAX_PATH - 1);
	base_path[MAX_PATH - 1] = '\0';
	char* slash = strrchr(base_path, '/');
	if (!slash)
		return 0;
	slash[1] = '\0';

	FILE* file = fopen(m3u_path, "r");
	if (file) {
		char line[MAX_PATH];
		while (fgets(line, sizeof(line), file) != NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line) == 0)
				continue;

			snprintf(disc_path, MAX_PATH, "%s%s", base_path, line);

			if (exists(disc_path)) {
				found = 1;
				break;
			}
		}
		fclose(file);
	}
	return found;
}

void addEntries(Array* entries, char* path) {
	DIR* dh = opendir(path);
	if (dh != NULL) {
		struct dirent* dp;
		char* tmp;
		char full_path[MAX_PATH];
		snprintf(full_path, sizeof(full_path), "%s/", path);
		tmp = full_path + strlen(full_path);
		size_t remaining = sizeof(full_path) - (tmp - full_path);
		while ((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name))
				continue;
			strncpy(tmp, dp->d_name, remaining - 1);
			full_path[MAX_PATH - 1] = '\0';
			int is_dir = dp->d_type == DT_DIR;
			int type;
			if (is_dir) {
				if (suffixMatch(".pak", dp->d_name)) {
					type = ENTRY_PAK;
				} else {
					type = ENTRY_DIR;
				}
			} else {
				if (prefixMatch(COLLECTIONS_PATH, full_path)) {
					type = ENTRY_DIR;
				} else {
					type = ENTRY_ROM;
				}
			}
			Array_push(entries, Entry_new(full_path, type));
		}
		closedir(dh);
	}
}

Array* getEntries(char* path) {
	Array* entries = Array_new();

	if (isConsoleDir(path)) { // top-level console folder, might collate
		char collated_path[MAX_PATH];
		strncpy(collated_path, path, MAX_PATH - 1);
		collated_path[MAX_PATH - 1] = '\0';
		char* tmp = strrchr(collated_path, '(');
		if (tmp)
			tmp[1] = '\0';

		DIR* dh = opendir(ROMS_PATH);
		if (dh != NULL) {
			struct dirent* dp;
			char full_path[MAX_PATH];
			snprintf(full_path, sizeof(full_path), "%s/", ROMS_PATH);
			tmp = full_path + strlen(full_path);
			size_t remaining = sizeof(full_path) - (tmp - full_path);
			while ((dp = readdir(dh)) != NULL) {
				if (hide(dp->d_name))
					continue;
				if (dp->d_type != DT_DIR)
					continue;
				strncpy(tmp, dp->d_name, remaining - 1);
				full_path[MAX_PATH - 1] = '\0';

				if (!prefixMatch(collated_path, full_path))
					continue;
				addEntries(entries, full_path);
			}
			closedir(dh);
		}
	} else
		addEntries(entries, path);

	EntryArray_sort(entries);
	return entries;
}

// platform subfolders (MinUI community pak convention, e.g. Tools/tg5040)
// get their paks merged into the Tools list instead of appearing as folders
static int isPlatformDirName(const char* name) {
	return strcmp(name, "tg5040") == 0 || strcmp(name, "tg5050") == 0 || strcmp(name, "desktop") == 0 || strcmp(name, "shared") == 0;
}

Array* getTools(void) {
	Array* entries = Array_new();

	// SD override layer first (may not exist; opendir just fails). Only
	// directories belong in the Tools menu (paks and plain folders) -- plain
	// files like the README migrate-paks.sh drops into /Tools are excluded.
	DIR* sd = opendir(TOOLS_PATH);
	if (sd != NULL) {
		struct dirent* dp;
		while ((dp = readdir(sd)) != NULL) {
			if (hide(dp->d_name))
				continue;
			if (dp->d_type != DT_DIR)
				continue;
			if (isPlatformDirName(dp->d_name))
				continue;
			char full_path[MAX_PATH];
			snprintf(full_path, sizeof(full_path), "%s/%s", TOOLS_PATH, dp->d_name);
			int type = suffixMatch(".pak", dp->d_name) ? ENTRY_PAK : ENTRY_DIR;
			Array_push(entries, Entry_new(full_path, type));
		}
		closedir(sd);
	}

	// append platform-subfolder paks not shadowed by an SD pak of the same name
	char plat_path[MAX_PATH];
	snprintf(plat_path, sizeof(plat_path), "%s/" PLATFORM, TOOLS_PATH);
	DIR* pd = opendir(plat_path);
	if (pd != NULL) {
		struct dirent* dp;
		while ((dp = readdir(pd)) != NULL) {
			if (hide(dp->d_name))
				continue;
			if (dp->d_type != DT_DIR || !suffixMatch(".pak", dp->d_name))
				continue;
			char shadow[MAX_PATH];
			snprintf(shadow, sizeof(shadow), "%s/%s", TOOLS_PATH, dp->d_name);
			if (exists(shadow))
				continue;
			char full_path[MAX_PATH];
			snprintf(full_path, sizeof(full_path), "%s/%s", plat_path, dp->d_name);
			Array_push(entries, Entry_new(full_path, ENTRY_PAK));
		}
		closedir(pd);
	}

	// append system paks not shadowed by an SD pak of the same name
	char sys_path[MAX_PATH];
	snprintf(sys_path, sizeof(sys_path), "%s/Tools", PAKS_PATH);
	DIR* dh = opendir(sys_path);
	if (dh != NULL) {
		struct dirent* dp;
		while ((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name))
				continue;
			if (dp->d_type != DT_DIR || !suffixMatch(".pak", dp->d_name))
				continue;
			char shadow[MAX_PATH];
			snprintf(shadow, sizeof(shadow), "%s/%s", TOOLS_PATH, dp->d_name);
			if (exists(shadow))
				continue;
			snprintf(shadow, sizeof(shadow), "%s/%s", plat_path, dp->d_name);
			if (exists(shadow))
				continue;
			char full_path[MAX_PATH];
			snprintf(full_path, sizeof(full_path), "%s/%s", sys_path, dp->d_name);
			Array_push(entries, Entry_new(full_path, ENTRY_PAK));
		}
		closedir(dh);
	}
	EntryArray_sort(entries);
	return entries;
}
