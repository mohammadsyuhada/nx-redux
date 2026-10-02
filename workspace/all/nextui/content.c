#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <inttypes.h>
#include "recents.h"
#include "defines.h"
#include "utils.h"
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include "content.h"
#include "shortcuts.h"
#include "config.h"
#include "arcade_names.h"
#include "collname.h"

static bool _simple_mode = false;

// file-local content builders (exported API lives in content.h)
static Array* getRoms(void);
static Array* getPinned(void);
static Array* getSimpleTools(void);
static Array* getCollection(char* path);
static Array* getDiscs(char* path);
static Array* getEntries(char* path);
static void addEntries(Array* entries, char* path);

// EMULIST_CACHE_PATH and ROMINDEX_CACHE_PATH defined in defines.h

void Content_setSimpleMode(bool mode) {
	_simple_mode = mode;
}

///////////////////////////////////////
// Helpers

static int getIndexChar(char* str) {
	char i = 0;
	char c = tolower(str[0]);
	if (c >= 'a' && c <= 'z')
		i = (c - 'a') + 1;
	return i;
}

static void getUniqueName(Entry* entry, char* out_name) {
	char* slash = strrchr(entry->path, '/');
	if (!slash)
		return;
	char emu_tag[MAX_PATH];
	getEmuName(entry->path, emu_tag);
	snprintf(out_name, MAX_PATH, "%s (%s)", entry->name, emu_tag);
}

// stem of a path: its basename with trailing 1-4 letter extensions removed,
// matching getDisplayName()'s rule so multi-extensions (e.g. .p8.png) collapse.
static void getFileStem(const char* path, char* out /* MAX_PATH */) {
	const char* base = baseName(path);
	snprintf(out, MAX_PATH, "%s", base);
	char* tmp;
	while ((tmp = strrchr(out, '.')) != NULL) {
		int len = strlen(tmp);
		if (len > 2 && len <= 5)
			tmp[0] = '\0'; // 1-4 letter extension plus dot
		else
			break;
	}
	if (out[0] == '\0')
		snprintf(out, MAX_PATH, "%s", base); // stripping ate everything; restore the basename
}

///////////////////////////////////////
// Directory indexing

// Parse a map.txt: one "<key>\t<display name>" line per entry, blank lines
// skipped. Returns NULL if the file cannot be opened (missing or unreadable),
// else a Hash the caller must Hash_free.
static Hash* readMapFile(const char* map_path) {
	FILE* file = fopen(map_path, "r");
	if (!file)
		return NULL;
	Hash* map = Hash_new();
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
	return map;
}

// Arcade titles for ROMs with no map.txt alias (see arcade_names.h), keyed by
// the Roms folder's emulator tag. Each table is loaded on first use and kept
// for the process lifetime; a tag without a table is cached too, so other
// consoles cost one failed fopen per boot.
#define ARCADE_TABLES_MAX 64
static struct {
	char tag[MAX_PATH];
	ArcadeNames* names;
} arcade_tables[ARCADE_TABLES_MAX];
static int arcade_table_count = 0;

static ArcadeNames* arcadeTable(const char* rom_path) {
	if (!prefixMatch(ROMS_PATH, rom_path) || rom_path[strlen(ROMS_PATH)] != '/')
		return NULL;
	char tag[MAX_PATH];
	getEmuName(rom_path, tag);

	ArcadeNames* names = NULL;
	int i;
	for (i = 0; i < arcade_table_count; i++) {
		if (exactMatch(arcade_tables[i].tag, tag)) {
			names = arcade_tables[i].names;
			break;
		}
	}
	if (i == arcade_table_count) {
		char table_path[MAX_PATH];
		snprintf(table_path, sizeof(table_path), "%s/arcade/%s.txt", RES_PATH, tag);
		if (arcade_table_count == ARCADE_TABLES_MAX)
			return NULL; // more emulator tags than any card has; skip rather than evict
		names = ArcadeNames_load(table_path);
		snprintf(arcade_tables[arcade_table_count].tag, MAX_PATH, "%s", tag);
		arcade_tables[arcade_table_count].names = names;
		arcade_table_count++;
	}
	return names;
}

static const char* arcadeName(const char* rom_path) {
	return ArcadeNames_get(arcadeTable(rom_path), baseName(rom_path));
}

// True when an entry's display name is its arcade table title, setting
// *qualifier to the table qualifier (e.g. "(Japan 940520)", NULL if none).
// False when map.txt names it or the name is not the table's (a Rename
// recorded in Recents, a non-arcade file).
static bool arcadeQualifier(Hash* map, const Entry* entry, const char** qualifier) {
	if (entry->type != ENTRY_ROM)
		return false;
	const char* filename = baseName(entry->path);
	if (map && Hash_get(map, filename))
		return false;
	ArcadeNames* names = arcadeTable(entry->path);
	const char* title = ArcadeNames_get(names, filename);
	if (!title || !exactMatch(title, entry->name))
		return false;
	*qualifier = ArcadeNames_getQualifier(names, filename);
	return true;
}

// Label each row of a same-folder run of arcade clones sharing one title by
// region or DAT qualifier: "Alien vs. Predator (Japan)" rather than "avspj";
// a lone unqualified parent set keeps the bare title, and clones the
// qualifiers cannot tell apart get "<title> (<stem>)". Returns false, touching
// nothing, when any row is not table-named.
static bool arcadeLabelRun(Directory* self, Hash* map, int start, int end) {
	int n = end - start;
	const char** qualifiers = calloc(n, sizeof(char*));
	const char** filenames = calloc(n, sizeof(char*));
	char** labels = calloc(n, sizeof(char*));
	char* bufs = calloc(n, MAX_PATH);
	bool ok = qualifiers && filenames && labels && bufs;
	for (int k = 0; ok && k < n; k++) {
		Entry* e = self->entries->items[start + k];
		labels[k] = bufs + k * MAX_PATH;
		filenames[k] = baseName(e->path);
		ok = arcadeQualifier(map, e, &qualifiers[k]);
	}
	if (ok)
		ok = ArcadeNames_disambiguate(qualifiers, filenames, n, labels, MAX_PATH);
	for (int k = 0; ok && k < n; k++) {
		Entry* e = self->entries->items[start + k];
		char buf[MAX_PATH];
		if (labels[k][0])
			snprintf(buf, sizeof(buf), "%s %s", e->name, labels[k]);
		else
			snprintf(buf, sizeof(buf), "%s", e->name); // the unqualified parent set
		free(e->unique);
		e->unique = strdup(buf);
	}
	free(bufs);
	free(labels);
	free(filenames);
	free(qualifiers);
	return ok;
}

static void Directory_index(Directory* self) {
	int is_collection = prefixMatch(COLLECTIONS_PATH, self->path);
	int skip_index = exactMatch(FAUX_RECENT_PATH, self->path) || is_collection; // not alphabetized

	char map_path[MAX_PATH];
	const char* map_dir = is_collection						  ? COLLECTIONS_PATH
						  : exactMatch(ROMS_PATH, self->path) ? SDCARD_PATH // Consoles tab: the old root's map.txt
															  : self->path;
	snprintf(map_path, sizeof(map_path), "%s/map.txt", map_dir);
	Hash* map = readMapFile(map_path);
	// Recents names come from the alias recorded at launch (a Rename or the
	// arcade title it showed then); the arcade table must not override that.
	bool use_arcade = !exactMatch(FAUX_RECENT_PATH, self->path);

	bool resort = false;
	bool filter = false;
	for (int i = 0; i < self->entries->count; i++) {
		Entry* entry = self->entries->items[i];
		char* slash = strrchr(entry->path, '/');
		if (!slash)
			continue;
		char* filename = slash + 1;
		char* alias = map ? Hash_get(map, filename) : NULL;
		bool from_map = alias != NULL;
		if (!alias && use_arcade && entry->type == ENTRY_ROM)
			alias = (char*)arcadeName(entry->path);
		if (alias) {
			free(entry->name);
			entry->name = strdup(alias);
			// collections keep their file order unless map.txt renames (as before)
			if (from_map || !is_collection)
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

	Entry* prior = NULL;
	int alpha = -1;
	int index = 0;
	for (int i = 0; i < self->entries->count; i++) {
		Entry* entry = self->entries->items[i];

		// A "run" is a maximal span of consecutive entries with an identical
		// display name. Game lists collate every sibling Roms/<Console> (<TAG>)/
		// folder into one list, so the same name can arrive from several cores.
		// Disambiguate each row in a run of >= 2 with three label forms:
		//   same file across the run  -> "<name> (<tag>)"     (per-core copies of one rom)
		//   files differ, tags differ -> "<stem> (<tag>)"     (different roms from different cores; the core tag tells them apart)
		//   files differ, same tag    -> "<stem>"             (same folder, e.g. "Tetris" vs "Tetris (1)")
		// except that arcade clones named by the same table title get
		// "<name> (<region>)" or "<name> <qualifier>", the bare "<name>" for
		// a lone unqualified parent set, else "<name> (<stem>)" (arcadeLabelRun).
		// The tag is appended only when filenames differ across cores because
		// without it the user cannot tell which core a row would launch. The stem
		// drops the extension, except when two stems in the run would collide
		// (e.g. "Tetris.gb" vs "Tetris.gbc") — then the whole run keeps filenames.
		Entry* next = (i + 1 < self->entries->count) ? self->entries->items[i + 1] : NULL;
		bool run_start = next != NULL && exactMatch(entry->name, next->name) && (prior == NULL || !exactMatch(prior->name, entry->name));
		if (run_start) {
			int j = i + 1;
			while (j < self->entries->count && exactMatch(entry->name, ((Entry*)self->entries->items[j])->name))
				j++;

			char first_tag[MAX_PATH];
			getEmuName(entry->path, first_tag);
			const char* first_file = baseName(entry->path);
			bool same_file = true;
			bool tags_differ = false;
			for (int k = i + 1; k < j; k++) {
				Entry* e = self->entries->items[k];
				if (!exactMatch(baseName(e->path), first_file))
					same_file = false;
				char tag[MAX_PATH];
				getEmuName(e->path, tag);
				if (!exactMatch(tag, first_tag))
					tags_differ = true;
			}

			// Prefer the extensionless stem for the "files differ" labels, but only
			// if every stem in the run is still distinct; if two collide (e.g.
			// "Tetris.gb" vs "Tetris.gbc") the run keeps full filenames. Runs are
			// short, so an O(n^2) pairwise compare is fine.
			bool stems_unique = false;
			if (!same_file) {
				stems_unique = true;
				for (int k = i; k < j && stems_unique; k++) {
					char stem_k[MAX_PATH];
					getFileStem(((Entry*)self->entries->items[k])->path, stem_k);
					for (int m = k + 1; m < j; m++) {
						char stem_m[MAX_PATH];
						getFileStem(((Entry*)self->entries->items[m])->path, stem_m);
						if (exactMatch(stem_k, stem_m)) {
							stems_unique = false;
							break;
						}
					}
				}
			}

			bool arcade_labelled = !same_file && !tags_differ && arcadeLabelRun(self, map, i, j);
			for (int k = i; k < j && !arcade_labelled; k++) {
				Entry* e = self->entries->items[k];
				free(e->unique);
				e->unique = NULL;
				char buf[MAX_PATH] = {0};
				if (same_file) {
					getUniqueName(e, buf);
				} else {
					char stem[MAX_PATH];
					getFileStem(e->path, stem);
					const char* label = stems_unique ? stem : baseName(e->path);
					if (tags_differ) {
						char tag[MAX_PATH];
						getEmuName(e->path, tag);
						snprintf(buf, sizeof(buf), "%s (%s)", label, tag);
					} else {
						snprintf(buf, sizeof(buf), "%s", label);
					}
				}
				e->unique = strdup(buf);
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

	static unsigned next_serial = 0;
	Directory* self = malloc(sizeof(Directory));
	if (++next_serial == 0)
		next_serial = 1;
	self->serial = next_serial;
	self->path = strdup(path);
	self->name = strdup(display_name);
	if (exactMatch(path, SDCARD_PATH)) {
		self->entries = getPinned();
	} else if (exactMatch(path, FAUX_RECENT_PATH)) {
		self->entries = Recents_getEntries();
	} else if (exactMatch(path, ROMS_PATH)) {
		self->entries = getRoms();
	} else if (exactMatch(path, COLLECTIONS_PATH)) {
		self->entries = getCollections(); // the Collections tab: finishes a cut-short rename first
	} else if (!exactMatch(path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, path) && suffixMatch(".txt", path)) {
		self->entries = getCollection(path);
	} else if (suffixMatch(".m3u", path)) {
		self->entries = getDiscs(path);
	} else if (exactMatch(path, TOOLS_PATH)) {
		self->entries = _simple_mode ? getSimpleTools() : getTools();
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

// readdir may report DT_UNKNOWN on some filesystems; fall back to stat there
static int direntIsDir(const char* parent_path, const struct dirent* dp) {
	if (dp->d_type != DT_UNKNOWN)
		return dp->d_type == DT_DIR;
	char full[MAX_PATH];
	struct stat st;
	snprintf(full, sizeof(full), "%s/%s", parent_path, dp->d_name);
	return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
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
	cue_path[0] = '\0';						 // never leave callers reading an uninitialized buffer
	char* slash = strrchr(dir_path, '/');
	if (!slash)
		return 0;
	char* tmp = slash + 1;
	snprintf(cue_path, MAX_PATH, "%s/%s.cue", dir_path, tmp);
	return exists(cue_path);
}

int hasFolderM3u(char* dir_path, char* m3u_path) { // NOTE: dir_path not rom_path
	m3u_path[0] = '\0';							   // never leave callers reading an uninitialized buffer
	char* slash = strrchr(dir_path, '/');
	if (!slash)
		return 0;
	snprintf(m3u_path, MAX_PATH, "%s/%s.m3u", dir_path, slash + 1);
	return exists(m3u_path);
}

int hasM3u(char* rom_path, char* m3u_path) { // NOTE: rom_path not dir_path
	return M3U_findForRom(rom_path, m3u_path, MAX_PATH);
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

static int hasRoms(char* dir_name) {
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
// add/remove bumps the dir mtime), Roms/map.txt (console aliases), each
// per-console Roms/<Console>/map.txt (rom aliases), the Emus pak roots
// consulted by hasEmu (pak add/remove), and the res/arcade name tables.
// In-app mutations go through Content_invalidateEmulist and don't rely on
// this check.
//
// Change detection is by EQUALITY of a fingerprint of the source mtimes that
// is recorded in the cache file, not by "source newer than cache": a source
// carrying a bogus future mtime (seen in the wild: an Emus dir dated 2098,
// FAT keeps whatever a copy tool writes) is "newer" forever, which forced a
// full rescan on every boot and left Search permanently empty. The sum is
// order-independent so readdir order does not matter. The fingerprint is also
// seeded with a schema tag (CACHE_SCHEMA_TAG) so a change to the index-building
// logic invalidates stale caches too.

// Bump this whenever the index-building logic changes, so existing caches are
// rebuilt once after an upgrade without users clearing them.
#define CACHE_SCHEMA_TAG "romindex-v5"
static uint64_t fnv1a64(const char* str) {
	uint64_t h = 0xCBF29CE484222325ULL;
	for (; *str; str++)
		h = (h ^ (unsigned char)*str) * 0x100000001B3ULL;
	return h;
}

static void fingerprintMix(uint64_t* fp, const char* path) {
	struct stat st;
	// missing sources still contribute (a pak root appearing is a change)
	uint64_t mtime = stat(path, &st) == 0 ? (uint64_t)st.st_mtime + 1 : 0;
	*fp += fnv1a64(path) ^ (mtime * 0x9E3779B97F4A7C15ULL);
}

static uint64_t cacheSourcesFingerprint(void) {
	uint64_t fp = fnv1a64(CACHE_SCHEMA_TAG);
	char roms_map_path[MAX_PATH];
	snprintf(roms_map_path, sizeof(roms_map_path), "%s/map.txt", ROMS_PATH);
	char paks_emus_path[MAX_PATH];
	snprintf(paks_emus_path, sizeof(paks_emus_path), "%s/Emus", PAKS_PATH);
	char sdcard_emus_path[MAX_PATH];
	snprintf(sdcard_emus_path, sizeof(sdcard_emus_path), "%s/Emus", SDCARD_PATH);
	char sdcard_emus_plat_path[MAX_PATH];
	snprintf(sdcard_emus_plat_path, sizeof(sdcard_emus_plat_path), "%s/Emus/%s", SDCARD_PATH, PLATFORM);
	const char* const source_paths[] = {
		ROMS_PATH,
		roms_map_path,
		paks_emus_path,
		sdcard_emus_path,
		sdcard_emus_plat_path,
	};
	for (size_t i = 0; i < sizeof(source_paths) / sizeof(source_paths[0]); i++)
		fingerprintMix(&fp, source_paths[i]);

	// arcade name tables: an update that replaces one changes its mtime but
	// not the directory's, so mix each file
	char arcade_path[MAX_PATH];
	snprintf(arcade_path, sizeof(arcade_path), "%s/arcade", RES_PATH);
	DIR* arcade_dh = opendir(arcade_path);
	if (arcade_dh) {
		struct dirent* adp;
		char table_path[MAX_PATH];
		while ((adp = readdir(arcade_dh)) != NULL) {
			if (hide(adp->d_name))
				continue;
			snprintf(table_path, sizeof(table_path), "%s/%s", arcade_path, adp->d_name);
			fingerprintMix(&fp, table_path);
		}
		closedir(arcade_dh);
	}

	DIR* dh = opendir(ROMS_PATH);
	if (!dh)
		return 0; // no Roms dir: never matches a recorded fingerprint
	struct dirent* dp;
	char path[MAX_PATH];
	char map_path[MAX_PATH];
	struct stat st;
	while ((dp = readdir(dh)) != NULL) {
		if (hide(dp->d_name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", ROMS_PATH, dp->d_name);
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			fingerprintMix(&fp, path);
			// per-console map.txt: appearing, vanishing, or being edited all
			// change the fingerprint (a missing file is a stable contribution)
			snprintf(map_path, sizeof(map_path), "%s/map.txt", path);
			fingerprintMix(&fp, map_path);
		}
	}
	closedir(dh);
	return fp;
}

#define CACHE_FP_PREFIX "#fp="

// serialize entries as "path\tname\n" lines and stage via writeFileAtomic so a
// power cut can never leave a truncated cache. Refuses empty lists — an empty
// cache newer than its sources would read back as valid-and-fresh and blank
// the UI on every boot until an mtime bump forced a rescan.
static void writeEntryCache(const char* cache_path, Array* entries, uint64_t fp) {
	if (entries->count == 0)
		return;
	size_t cap = 16384, len = 0;
	char* buf = malloc(cap);
	if (!buf)
		return;
	len += snprintf(buf, cap, CACHE_FP_PREFIX "%016" PRIx64 "\n", fp);
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		size_t need = strlen(entry->path) + strlen(entry->name) + 2;
		while (len + need + 1 > cap) {
			cap *= 2;
			char* grown = realloc(buf, cap);
			if (!grown) {
				free(buf);
				return;
			}
			buf = grown;
		}
		len += snprintf(buf + len, cap - len, "%s\t%s\n", entry->path, entry->name);
	}
	writeFileAtomic(cache_path, buf, len);
	free(buf);
}

// Walks the "path\tname\n" lines writeEntryCache emits, calling cb(path, name, ctx) for each.
// Returns the line count, or -1 when the cache is missing or malformed, or when expect_fp is set
// and the recorded source fingerprint differs (stale, or a pre-fingerprint file). fp_out (may be
// NULL) receives the recorded fingerprint, 0 when there is none.
static int forEachEntryCacheLine(const char* cache_path, const uint64_t* expect_fp, uint64_t* fp_out,
								 void (*cb)(char* path, char* name, void* ctx), void* ctx) {
	if (fp_out)
		*fp_out = 0;
	FILE* file = fopen(cache_path, "r");
	if (!file)
		return -1;

	int count = 0;
	// sized to what writeEntryCache can emit: two MAX_PATH strings + tab + newline
	char line[MAX_PATH * 2 + 8];
	bool first = true;
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (first) {
			first = false;
			uint64_t fp = 0;
			bool has_fp = sscanf(line, CACHE_FP_PREFIX "%" SCNx64, &fp) == 1;
			if (expect_fp && (!has_fp || fp != *expect_fp)) {
				fclose(file);
				return -1; // sources changed since this cache was written
			}
			if (has_fp) {
				if (fp_out)
					*fp_out = fp;
				continue;
			}
		}
		if (strlen(line) == 0)
			continue;

		char* tab = strchr(line, '\t');
		if (!tab) {
			fclose(file);
			return -1; // malformed cache, force rescan
		}
		*tab = '\0';
		cb(line, tab + 1, ctx);
		count++;
	}
	fclose(file);
	return count;
}

typedef struct {
	Array* entries;
	int type;
} EntryCacheRead;

static void readEntryCacheCb(char* path, char* name, void* ctx) {
	EntryCacheRead* read = ctx;
	Array_push(read->entries, Entry_newNamed(path, read->type, name));
}

// Shared reader for the "path\tname\n" caches writeEntryCache emits. Returns
// NULL when the cache is missing, malformed, or empty; otherwise an Array of
// Entry_newNamed(path, type, name). With expect_fp set, the recorded source
// fingerprint must match it (else NULL — stale, or a pre-fingerprint file);
// NULL expect_fp skips the check, for callers that just (re)built the file.
static Array* readEntryCacheFile(const char* cache_path, int type, const uint64_t* expect_fp) {
	EntryCacheRead read = {Array_new(), type};
	int count = forEachEntryCacheLine(cache_path, expect_fp, NULL, readEntryCacheCb, &read);
	if (count <= 0) { // missing/stale/malformed; an empty cache is never trusted either: force rescan
		EntryArray_free(read.entries);
		return NULL;
	}
	return read.entries;
}

///////////////////////////////////////
// Console row game counts

// Per console folder (the first path segment under ROMS_PATH), how many rom-index rows it holds.
// Built lazily from ROMINDEX_CACHE_PATH with one read; dropped whenever getRoms rewrites the index
// or it is invalidated. UI thread only.
typedef struct {
	char folder[256];
	int count;
} ConsoleCount;

static ConsoleCount* console_counts = NULL;
static int console_counts_len = 0;
static int console_counts_cap = 0;
static bool console_counts_built = false;
static bool console_counts_ok = false; // the index was readable
static char library_fp[32] = "";

static void dropConsoleCounts(void) {
	free(console_counts);
	console_counts = NULL;
	console_counts_len = console_counts_cap = 0;
	console_counts_built = console_counts_ok = false;
	library_fp[0] = '\0';
}

static void consoleCountCb(char* path, char* name, void* ctx) {
	(void)name;
	(void)ctx;
	size_t root_len = strlen(ROMS_PATH);
	if (strncmp(path, ROMS_PATH, root_len) != 0 || path[root_len] != '/')
		return;
	const char* folder = path + root_len + 1;
	size_t len = strcspn(folder, "/");
	if (len == 0 || len >= sizeof(console_counts[0].folder))
		return;
	// rows arrive sorted by label, not folder: check the last hit first, then scan (few folders)
	static int last = -1;
	if (last >= console_counts_len)
		last = -1;
	int found = -1;
	if (last >= 0 && strncmp(console_counts[last].folder, folder, len) == 0 && !console_counts[last].folder[len])
		found = last;
	for (int i = 0; found < 0 && i < console_counts_len; i++) {
		if (strncmp(console_counts[i].folder, folder, len) == 0 && !console_counts[i].folder[len])
			found = i;
	}
	if (found < 0) {
		if (console_counts_len == console_counts_cap) {
			int cap = console_counts_cap ? console_counts_cap * 2 : 32;
			ConsoleCount* grown = realloc(console_counts, cap * sizeof(ConsoleCount));
			if (!grown)
				return;
			console_counts = grown;
			console_counts_cap = cap;
		}
		found = console_counts_len++;
		memcpy(console_counts[found].folder, folder, len);
		console_counts[found].folder[len] = '\0';
		console_counts[found].count = 0;
	}
	console_counts[found].count++;
	last = found;
}

static void buildConsoleCounts(void) {
	if (console_counts_built)
		return;
	console_counts_built = true;
	uint64_t fp = 0;
	// no staleness gate: getRoms already validated (or rebuilt) the index this boot, and a
	// rewrite drops this table
	console_counts_ok = forEachEntryCacheLine(ROMINDEX_CACHE_PATH, NULL, &fp, consoleCountCb, NULL) > 0;
	if (console_counts_ok && fp)
		snprintf(library_fp, sizeof(library_fp), "%016" PRIx64, fp);
	else if (!console_counts_ok)
		// no rows (writeEntryCache never writes an empty index): an empty library is still a
		// library, so counts of pak-only collections stay cacheable
		snprintf(library_fp, sizeof(library_fp), "empty");
}

const char* Content_libraryFingerprint(void) {
	buildConsoleCounts();
	return library_fp;
}

int Content_consoleGameCount(const Entry* console_row) {
	if (!console_row || !console_row->path || !isConsoleDir(console_row->path))
		return -1;
	buildConsoleCounts();
	if (!console_counts_ok)
		return -1;
	// the folders opening this row collates: getEntries' rule (path prefix up to and including the last '(')
	char collated[MAX_PATH];
	snprintf(collated, sizeof(collated), "%s", console_row->path);
	char* paren = strrchr(collated, '(');
	if (paren)
		paren[1] = '\0';
	int total = 0;
	char folder_path[MAX_PATH];
	for (int i = 0; i < console_counts_len; i++) {
		snprintf(folder_path, sizeof(folder_path), "%s/%s", ROMS_PATH, console_counts[i].folder);
		if (prefixMatch(collated, folder_path))
			total += console_counts[i].count;
	}
	return total;
}

// readEntryCacheFile plus the staleness gate: NULL (forcing a rescan) when any
// cache source changed since the file was written.
static Array* readEntryCache(const char* cache_path, int type) {
	uint64_t fp = cacheSourcesFingerprint();
	return readEntryCacheFile(cache_path, type, &fp);
}

static Array* readRomsCache(void) {
	// Both caches are written together by getRoms; a missing rom index with a
	// valid emulist would satisfy the menu but leave Search permanently empty,
	// so force the full rebuild that restores both. (Ordering vs the staleness
	// check inside readEntryCache is irrelevant — both are read-only and both
	// short-circuit to NULL.)
	if (!exists(ROMINDEX_CACHE_PATH))
		return NULL;
	return readEntryCache(EMULIST_CACHE_PATH, ENTRY_DIR);
}

static Array* readRomIndexCache(void) {
	return readEntryCache(ROMINDEX_CACHE_PATH, ENTRY_ROM);
}

void Content_invalidateEmulist(void) {
	unlink(EMULIST_CACHE_PATH);
	unlink(ROMINDEX_CACHE_PATH);
	dropConsoleCounts();
}

// The "#fp=" header of a cache file; false when the file is missing or has none.
static bool readCacheFp(const char* cache_path, uint64_t* fp) {
	FILE* file = fopen(cache_path, "r");
	if (!file)
		return false;
	char line[64];
	bool ok = fgets(line, sizeof(line), file) != NULL && sscanf(line, CACHE_FP_PREFIX "%" SCNx64, fp) == 1;
	fclose(file);
	return ok;
}

bool Content_romCachesFresh(void) {
	uint64_t now = cacheSourcesFingerprint(), index_fp = 0, emulist_fp = 0;
	return now && readCacheFp(ROMINDEX_CACHE_PATH, &index_fp) && index_fp == now &&
		   readCacheFp(EMULIST_CACHE_PATH, &emulist_fp) && emulist_fp == now;
}

typedef struct {
	char* buf;
	size_t len, cap;
	const char* drop; // rows at or below this path are left out; NULL keeps all
	size_t drop_len;
	int kept;
	bool failed;
} CacheRewrite;

static void rewriteCacheCb(char* path, char* name, void* ctx) {
	CacheRewrite* rw = ctx;
	if (rw->failed)
		return;
	if (rw->drop && strncmp(path, rw->drop, rw->drop_len) == 0 &&
		(path[rw->drop_len] == '\0' || path[rw->drop_len] == '/'))
		return;
	size_t need = strlen(path) + strlen(name) + 2;
	while (rw->len + need + 1 > rw->cap) {
		char* grown = realloc(rw->buf, rw->cap * 2);
		if (!grown) {
			rw->failed = true;
			return;
		}
		rw->buf = grown;
		rw->cap *= 2;
	}
	rw->len += snprintf(rw->buf + rw->len, rw->cap - rw->len, "%s\t%s\n", path, name);
	rw->kept++;
}

// Rewrite cache_path stamped with new_fp, leaving out the rows at or below `drop`. Returns the rows kept,
// or -1 (file untouched) when it can't be read or written; an empty result is not written (see writeEntryCache).
static int rewriteEntryCache(const char* cache_path, uint64_t new_fp, const char* drop) {
	CacheRewrite rw = {malloc(16384), 0, 16384, drop, drop ? strlen(drop) : 0, 0, false};
	if (!rw.buf)
		return -1;
	rw.len = snprintf(rw.buf, rw.cap, CACHE_FP_PREFIX "%016" PRIx64 "\n", new_fp);
	int rows = forEachEntryCacheLine(cache_path, NULL, NULL, rewriteCacheCb, &rw);
	int kept = rw.kept;
	if (rows < 0 || rw.failed || (kept > 0 && !writeFileAtomic(cache_path, rw.buf, rw.len)))
		kept = -1;
	free(rw.buf);
	return kept;
}

void Content_forgetRom(const char* removed_path, bool caches_were_fresh) {
	if (removed_path && exists((char*)removed_path)) {
		// the delete failed (a read-only card) or only half-removed a folder game: the rom is still on the
		// card, so its rows must not be dropped. The map.txt/collection clean-up still ran, so the caches
		// can't be restamped as fresh either: invalidate, and the next getRoms rescans what is really there.
		Content_invalidateEmulist();
		return;
	}
	size_t root_len = strlen(ROMS_PATH);
	if (!caches_were_fresh || !removed_path || strncmp(removed_path, ROMS_PATH, root_len) != 0 ||
		removed_path[root_len] != '/') {
		// a stale index is rebuilt by the next getRoms (its fingerprint no longer matches)
		dropConsoleCounts();
		return;
	}
	// the console folder: a deletion that empties it changes what the Consoles tab lists, so rescan
	char folder[MAX_PATH];
	snprintf(folder, sizeof(folder), "%s", removed_path + root_len + 1);
	folder[strcspn(folder, "/")] = '\0';
	if (!folder[0] || !hasRoms(folder)) {
		Content_invalidateEmulist();
		return;
	}
	// Both caches were current before the delete and the delete (plus its map.txt clean-up) is the only
	// change since, so the index minus the removed rows, stamped with today's fingerprint, is what a
	// rescan would produce. The index goes first: a crash in between leaves the emulist stale, and a
	// stale emulist forces the full rescan.
	uint64_t fp = cacheSourcesFingerprint();
	if (!fp || rewriteEntryCache(ROMINDEX_CACHE_PATH, fp, removed_path) <= 0 ||
		rewriteEntryCache(EMULIST_CACHE_PATH, fp, NULL) <= 0) {
		Content_invalidateEmulist();
		return;
	}
	dropConsoleCounts();
}

// Byte length of the rom-label part of an indexed name ("Zelda (GBA)" -> 5):
// the offset of the LAST " (" (the tag suffix always starts there and tags
// never contain parentheses), or the whole string when there is no " (".
int Content_romLabelLen(const char* indexed_name) {
	const char* last = NULL;
	for (const char* p = strstr(indexed_name, " ("); p; p = strstr(p + 1, " ("))
		last = p;
	return last ? (int)(last - indexed_name) : (int)strlen(indexed_name);
}

Array* Content_searchRoms(const char* query) {
	Array* all_roms = readRomIndexCache();
	if (!all_roms) {
		// Force a build by calling getRoms() (which populates both caches)
		Array* consoles = getRoms();
		EntryArray_free(consoles);
		// Read the index we just wrote WITHOUT the staleness gate. The gate
		// used to be "source mtime newer than cache", which a bogus future
		// mtime (seen: an Emus dir dated 2098) kept true forever — the gated
		// re-read returned NULL and every search came back "No results" while
		// the index on disk was complete. The fingerprint gate cannot loop
		// like that, but a just-written file needs no gate at all.
		all_roms = readEntryCacheFile(ROMINDEX_CACHE_PATH, ENTRY_ROM, NULL);
		if (!all_roms)
			return Array_new();
	}

	if (!query || strlen(query) == 0)
		return all_roms;

	Array* results = Array_new();
	for (int i = 0; i < all_roms->count; i++) {
		Entry* entry = all_roms->items[i];
		// match the label ("Metal Slug (Arcade)") or the raw filename ("mslug"),
		// so an aliased rom still surfaces under its on-disk name
		if (containsString(entry->name, (char*)query) || containsString((char*)baseName(entry->path), (char*)query)) {
			Array_push(results, entry);
		} else {
			Entry_free(entry);
		}
	}
	Array_free(all_roms);

	return results;
}

// A search-index row named from an arcade table, kept until the folder scan
// ends so clones sharing a title can be labelled like the game list does.
typedef struct {
	const char* title;	   // table-owned
	const char* qualifier; // table-owned, may be NULL
	Entry* entry;
} ArcadeIndexRow;

static int compareArcadeIndexRow(const void* a, const void* b) {
	return strcmp(((const ArcadeIndexRow*)a)->title, ((const ArcadeIndexRow*)b)->title);
}

// Rename each run of rows sharing an arcade title to
// "<title> <label> (<tag>)" (ArcadeNames_disambiguate), matching the labels
// Directory_index gives the same rows in the folder view.
static void labelArcadeIndexRows(ArcadeIndexRow* rows, int count, const char* tag) {
	qsort(rows, count, sizeof(ArcadeIndexRow), compareArcadeIndexRow);
	for (int i = 0; i < count;) {
		int j = i + 1;
		while (j < count && exactMatch(rows[i].title, rows[j].title))
			j++;
		int n = j - i;
		if (n > 1) {
			const char** qualifiers = calloc(n, sizeof(char*));
			const char** filenames = calloc(n, sizeof(char*));
			char** labels = calloc(n, sizeof(char*));
			char* bufs = calloc(n, MAX_PATH);
			bool ok = qualifiers && filenames && labels && bufs;
			for (int k = 0; ok && k < n; k++) {
				qualifiers[k] = rows[i + k].qualifier;
				filenames[k] = baseName(rows[i + k].entry->path);
				labels[k] = bufs + k * MAX_PATH;
			}
			if (ok && ArcadeNames_disambiguate(qualifiers, filenames, n, labels, MAX_PATH)) {
				for (int k = 0; k < n; k++) {
					if (!labels[k][0])
						continue; // the unqualified parent set keeps "<title> (<tag>)"
					Entry* e = rows[i + k].entry;
					char name[MAX_PATH];
					snprintf(name, sizeof(name), "%s %s (%s)", rows[i + k].title, labels[k], tag);
					free(e->name);
					e->name = strdup(name);
				}
			}
			free(bufs);
			free(labels);
			free(filenames);
			free(qualifiers);
		}
		i = j;
	}
}

// Scan one console folder and push one search-index Entry per rom, each named
// "<rom label> (<TAG>)". The TAG is the folder's parenthesised emulator tag
// (getEmuName), or the folder name itself when it has none; it is used instead
// of the console's display name because it is short (so long console names
// never truncate) and because sibling folders differ only by their tag, so it
// disambiguates the same ROM indexed from several cores.
// Per-console Roms/<Console>/map.txt aliases and folder sort prefixes (e.g.
// "001)Game Boy (GB)") are resolved here at index time, so Search matches and
// shows the same names as the folder views. (search.c still calls
// trimSortingMeta at render; with the prefix already gone that is a harmless
// no-op.)
static void indexRomDir(Array* rom_index, const char* dir_path) {
	DIR* rom_dh = opendir(dir_path);
	if (!rom_dh)
		return;

	// the suffix for every rom in this folder: its emulator tag, sort prefix
	// stripped (getEmuName already returns the folder name when there's no tag)
	char tag_buf[MAX_PATH];
	getEmuName(dir_path, tag_buf);
	char* tag = tag_buf;
	trimSortingMeta(&tag);

	// per-console aliases: the same map.txt the folder view honours
	char map_path[MAX_PATH];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", dir_path);
	Hash* rom_map = readMapFile(map_path);

	ArcadeIndexRow* arcade_rows = NULL;
	int arcade_count = 0;
	int arcade_cap = 0;

	struct dirent* rom_dp;
	char rom_path[MAX_PATH];
	while ((rom_dp = readdir(rom_dh)) != NULL) {
		if (hide(rom_dp->d_name))
			continue;

		snprintf(rom_path, sizeof(rom_path), "%s/%s", dir_path, rom_dp->d_name);

		// Directory_index keys aliases by the entry's last path component,
		// which for a folder game is the directory name — so this matches.
		const char* alias = rom_map ? Hash_get(rom_map, rom_dp->d_name) : NULL;
		bool from_arcade = false;
		if (!alias) {
			alias = arcadeName(rom_path); // same fallback as Directory_index
			from_arcade = alias != NULL;
		}
		if (alias && hide((char*)alias))
			continue; // folder view filters hidden aliases; search must too

		if (direntIsDir(dir_path, rom_dp)) {
			// folder game (e.g. Game/Game.m3u): index the resolved
			// cue/m3u so multi-disc games are searchable too
			char resolved[MAX_PATH];
			if (!dirGameFile(rom_path, resolved))
				continue;
			snprintf(rom_path, sizeof(rom_path), "%s", resolved);
		}

		char display_name[MAX_PATH];
		char* rom_label;
		if (alias) {
			rom_label = (char*)alias;
		} else {
			getDisplayName(rom_path, display_name);
			rom_label = display_name;
		}
		trimSortingMeta(&rom_label);
		char full_display[MAX_PATH];
		snprintf(full_display, sizeof(full_display), "%s (%s)", rom_label, tag);

		Entry* entry = Entry_newNamed(rom_path, ENTRY_ROM, full_display);
		Array_push(rom_index, entry);

		if (from_arcade) {
			if (arcade_count == arcade_cap) {
				int cap = arcade_cap ? arcade_cap * 2 : 64;
				ArcadeIndexRow* grown = realloc(arcade_rows, cap * sizeof(ArcadeIndexRow));
				if (!grown)
					continue; // out of memory: this row just keeps its plain title
				arcade_rows = grown;
				arcade_cap = cap;
			}
			arcade_rows[arcade_count].title = alias;
			arcade_rows[arcade_count].qualifier = ArcadeNames_getQualifier(arcadeTable(rom_path), rom_dp->d_name);
			arcade_rows[arcade_count].entry = entry;
			arcade_count++;
		}
	}
	closedir(rom_dh);
	if (arcade_count > 1)
		labelArcadeIndexRows(arcade_rows, arcade_count, tag);
	free(arcade_rows);
	if (rom_map)
		Hash_free(rom_map);
}

static Array* getRoms(void) {
	// Try loading from cache first
	Array* entries = readRomsCache();
	if (entries)
		return entries;

	// Cache miss: full filesystem scan. Fingerprint the sources BEFORE
	// scanning so anything that changes mid-scan mismatches on the next read.
	uint64_t fp = cacheSourcesFingerprint();
	entries = Array_new();
	// declared outside the if so it survives an opendir failure; freed after
	// the ROM-index block below
	Array* sibling_dirs = Array_new();
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
				// siblings that lose the display-name dedupe (e.g. Sega Genesis
				// (MD) when Sega Genesis (GPGX) survived) are still scanned for
				// the search index under the survivor's label, mirroring
				// getEntries' collation
				Array_push(sibling_dirs, entry);
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
	Hash* map = entries->count > 0 ? readMapFile(map_path) : NULL;
	if (map) {
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

	// Build ROM index: scan every console dir for individual ROMs. indexRomDir
	// pushes one entry per rom, named "<rom label> (<TAG>)".
	{
		Array* rom_index = Array_new();
		for (int i = 0; i < entries->count; i++) {
			Entry* console_entry = entries->items[i];

			indexRomDir(rom_index, console_entry->path);

			// Sibling folders that lost the display-name dedupe belong to this
			// console in the game list too, so index them under the same label.
			// This is the same rule the game list uses to merge sibling folders
			// (getEntries): share the path prefix up to and including the last
			// '('. Doing it here keeps search and the list agreeing on which
			// ROMs belong to a console. (A loser folder matches at most one
			// survivor prefix in practice, so no marking is needed.)
			char collated[MAX_PATH];
			strncpy(collated, console_entry->path, MAX_PATH - 1);
			collated[MAX_PATH - 1] = '\0';
			char* p = strrchr(collated, '(');
			if (p)
				p[1] = '\0';
			for (int j = 0; j < sibling_dirs->count; j++) {
				Entry* sib = sibling_dirs->items[j];
				if (prefixMatch(collated, sib->path))
					indexRomDir(rom_index, sib->path);
			}
		}
		EntryArray_sort(rom_index);
		writeEntryCache(ROMINDEX_CACHE_PATH, rom_index, fp);
		dropConsoleCounts(); // counts and the library fingerprint follow the new index
		EntryArray_free(rom_index);
	}
	EntryArray_free(sibling_dirs);

	// Write cache for next launch (refused for an empty scan — see writeEntryCache)
	writeEntryCache(EMULIST_CACHE_PATH, entries, fp);

	return entries;
}

// True when the Consoles tab has anything to list (reads the cached emulist).
int Content_hasConsoles(void) {
	Array* roms = getRoms();
	int has = roms->count > 0;
	EntryArray_free(roms);
	return has;
}

// A case-only collection rename goes old -> <new>.txt.part -> <new>.txt (gamelist.c). A "<name>.txt.part" left by a
// cut-short rename is finished here, when its "<name>.txt" doesn't exist; otherwise it stays (and is never listed).
static void recoverCollectionParts(void) {
	DIR* dh = opendir(COLLECTIONS_PATH);
	if (!dh)
		return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		char target[MAX_PATH];
		if (dp->d_name[0] == '.' || !CollName_partTarget(dp->d_name, target, sizeof(target)))
			continue;
		char part_path[MAX_PATH], txt_path[MAX_PATH];
		snprintf(part_path, sizeof(part_path), "%s/%s", COLLECTIONS_PATH, dp->d_name);
		snprintf(txt_path, sizeof(txt_path), "%s/%s", COLLECTIONS_PATH, target);
		if (!exists(txt_path))
			rename(part_path, txt_path);
	}
	closedir(dh);
}

Array* getCollections(void) {
	Array* collections = Array_new();
	recoverCollectionParts();
	DIR* dh = opendir(COLLECTIONS_PATH);
	if (dh) {
		struct dirent* dp;
		char full_path[MAX_PATH];
		snprintf(full_path, sizeof(full_path), "%s/", COLLECTIONS_PATH);
		char* tmp = full_path + strlen(full_path);

		size_t remaining = sizeof(full_path) - (tmp - full_path);
		while ((dp = readdir(dh)) != NULL) {
			char part_target[MAX_PATH];
			if (hide(dp->d_name) || CollName_partTarget(dp->d_name, part_target, sizeof(part_target)))
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

// The Home tab's pins: the pinned games, folders and tools, in stored order.
static Array* getPinned(void) {
	Array* root = Array_new();

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

	return root;
}

// Simple mode's Tools tab: Settings only. Simple mode hides the other tools,
// but Settings must stay reachable (the game list PIN-gates it) or parents
// get locked out of the device.
static Array* getSimpleTools(void) {
	Array* entries = Array_new();
	char settings_path[MAX_PATH];
	snprintf(settings_path, sizeof(settings_path), "%s/Settings.pak", TOOLS_PATH);
	if (!exists(settings_path))
		snprintf(settings_path, sizeof(settings_path), "%s/Tools/Settings.pak", PAKS_PATH);
	if (exists(settings_path))
		Array_push(entries, Entry_newNamed(settings_path, ENTRY_PAK, "Settings"));
	return entries;
}

int Content_forEachCollectionGame(const char* path, bool (*cb)(const char* sd_path, void* ctx), void* ctx) {
	FILE* file = fopen(path, "r");
	if (!file)
		return -1;
	int n = 0;
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0)
			continue;

		char sd_path[MAX_PATH];
		snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, line);
		if (exists(sd_path)) {
			n++;
			if (cb && !cb(sd_path, ctx))
				break;
		}
	}
	fclose(file);
	return n;
}

static bool getCollectionCb(const char* sd_path, void* ctx) {
	int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM;
	Array_push((Array*)ctx, Entry_new(sd_path, type));
	return true;
}
static Array* getCollection(char* path) {
	Array* entries = Array_new();
	Content_forEachCollectionGame(path, getCollectionCb, entries);
	return entries;
}

static bool getDiscsCb(const char* disc_path, int index, void* ctx) {
	Array* entries = (Array*)ctx;
	Entry* entry = Entry_new(disc_path, ENTRY_ROM);
	free(entry->name);
	char name[16];
	sprintf(name, "Disc %i", index + 1);
	entry->name = strdup(name);
	Array_push(entries, entry);
	return true;
}
static Array* getDiscs(char* path) {
	Array* entries = Array_new();
	M3U_forEachDisc(path, getDiscsCb, entries);
	return entries;
}

static bool firstDiscCb(const char* disc_path, int index, void* ctx) {
	(void)index;
	char* out = (char*)ctx;
	strncpy(out, disc_path, MAX_PATH - 1);
	out[MAX_PATH - 1] = '\0';
	return false; // first existing disc wins
}
int getFirstDisc(char* m3u_path, char* disc_path) {
	disc_path[0] = '\0';
	M3U_forEachDisc(m3u_path, firstDiscCb, disc_path);
	return disc_path[0] != '\0';
}

static void addEntries(Array* entries, char* path) {
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
			int is_dir = direntIsDir(path, dp);
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

static Array* getEntries(char* path) {
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
				if (!direntIsDir(ROMS_PATH, dp))
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
	return strcmp(name, "tg5040") == 0 || strcmp(name, "tg5050") == 0 || strcmp(name, "shared") == 0;
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
			if (!direntIsDir(TOOLS_PATH, dp))
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
			if (!direntIsDir(plat_path, dp) || !suffixMatch(".pak", dp->d_name))
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
			if (!direntIsDir(sys_path, dp) || !suffixMatch(".pak", dp->d_name))
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
