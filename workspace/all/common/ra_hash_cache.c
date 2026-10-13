#include "ra_hash_cache.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cjson/cJSON.h"

#define RA_HC_MAX_PATH 1024
#define RA_HC_MAX_FILE (16 * 1024 * 1024)
#define RA_HC_TZ_STEP 900		 // FAT time zone offsets come in 15 minute steps
#define RA_HC_TZ_MAX (14 * 3600) // ...of at most 14 hours

// rcheevos' RC_CONSOLE_ARCADE (this module doesn't include rcheevos)
#define RA_HASH_CACHE_CONSOLE_ARCADE 27

static char hc_roms[RA_HC_MAX_PATH] = {0};

void RA_HashCache_init(const char* roms_root) {
	snprintf(hc_roms, sizeof(hc_roms), "%s", roms_root ? roms_root : "");
	// a trailing slash would double up when the path is split
	size_t n = strlen(hc_roms);
	while (n > 1 && hc_roms[n - 1] == '/')
		hc_roms[--n] = '\0';
}

bool RA_HashCache_mtimeMatches(long long cached, long long actual) {
	long long diff = cached > actual ? cached - actual : actual - cached;
	return diff == 0 || (diff % RA_HC_TZ_STEP == 0 && diff <= RA_HC_TZ_MAX);
}

bool RA_HashCache_canStore(const char* hashed_path, uint32_t console_id) {
	const char* ext = hashed_path ? strrchr(hashed_path, '.') : NULL;
	bool archive = ext && (!strcasecmp(ext, ".zip") || !strcasecmp(ext, ".7z"));
	return hashed_path && (!archive || console_id == RA_HASH_CACHE_CONSOLE_ARCADE);
}

/*****************************************************************************
 * Paths
 *****************************************************************************/

// Split rom_path into its console folder's data dir (<roms>/<Folder>/
// .retroachievements), its cache file (hashes.json in there; cache_file may be
// NULL) and its key (the path below <Folder>). False for paths outside a
// visible console folder, with "." / ".." components, or too long for the
// buffers.
static bool hc_split(const char* rom_path, char* cache_dir, size_t dir_size, char* cache_file,
					 size_t file_size, const char** key) {
	if (!hc_roms[0] || !rom_path)
		return false;
	size_t rlen = strlen(hc_roms);
	if (strncmp(rom_path, hc_roms, rlen) != 0 || rom_path[rlen] != '/')
		return false;
	const char* folder = rom_path + rlen + 1;
	const char* slash = strchr(folder, '/');
	if (!slash || slash == folder || folder[0] == '.' || !slash[1])
		return false;
	*key = slash + 1;
	for (const char* c = *key; *c;) {
		size_t len = strcspn(c, "/");
		if (len == 0 || (len == 1 && c[0] == '.') || (len == 2 && c[0] == '.' && c[1] == '.'))
			return false;
		c += len;
		if (*c == '/')
			c++;
	}
	int folder_len = (int)(slash - folder);
	int len = snprintf(cache_dir, dir_size, "%s/%.*s/%s", hc_roms, folder_len, folder, RA_HASH_CACHE_DIR);
	if (len <= 0 || (size_t)len >= dir_size)
		return false;
	if (cache_file) {
		len = snprintf(cache_file, file_size, "%s/%s", cache_dir, RA_HASH_CACHE_FILE);
		if (len <= 0 || (size_t)len >= file_size)
			return false;
	}
	return true;
}

bool RA_HashCache_folderDir(const char* rom_path, char* buf, size_t n) {
	if (!buf || n == 0)
		return false;
	const char* key;
	if (!hc_split(rom_path, buf, n, NULL, 0, &key)) {
		buf[0] = '\0';
		return false;
	}
	return true;
}

/*****************************************************************************
 * File I/O
 *****************************************************************************/

static cJSON* hc_load(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > RA_HC_MAX_FILE) {
		fclose(f);
		return NULL;
	}
	char* buf = (char*)malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[rd] = '\0';
	cJSON* root = cJSON_Parse(buf);
	free(buf);
	if (root && !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return NULL;
	}
	return root;
}

// Whole-file rewrite via hashes.json.tmp + rename, so a power cut never leaves a torn file
static bool hc_save(const char* dir, const char* path, cJSON* root) {
	mkdir(dir, 0755);
	char* text = cJSON_PrintUnformatted(root);
	if (!text)
		return false;
	char tmp[RA_HC_MAX_PATH + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	bool ok = false;
	FILE* f = fopen(tmp, "wb");
	if (f) {
		size_t len = strlen(text);
		ok = fwrite(text, 1, len, f) == len;
		fflush(f);
		fsync(fileno(f));
		ok = (fclose(f) == 0) && ok;
		ok = ok && rename(tmp, path) == 0;
		if (!ok)
			remove(tmp);
	}
	cJSON_free(text);
	if (!ok)
		fprintf(stderr, "[RA] hash cache: could not write %s\n", path);
	return ok;
}

/*****************************************************************************
 * Entries
 *****************************************************************************/

// 32 hex digits, lowercased into out (33 bytes)
static bool hc_valid_hash(const char* s, char* out) {
	if (!s || strlen(s) != 32)
		return false;
	for (int i = 0; i < 32; i++) {
		if (!isxdigit((unsigned char)s[i]))
			return false;
		out[i] = (char)tolower((unsigned char)s[i]);
	}
	out[32] = '\0';
	return true;
}

static bool hc_number(const cJSON* entry, const char* name, double* out) {
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(entry, name);
	if (!cJSON_IsNumber(item) || item->valuedouble < 0)
		return false;
	*out = item->valuedouble;
	return true;
}

// Set (or add) a numeric field, keeping the entry's other keys
static void hc_set_number(cJSON* entry, const char* name, double value) {
	cJSON* item = cJSON_GetObjectItemCaseSensitive(entry, name);
	if (cJSON_IsNumber(item))
		cJSON_SetNumberValue(item, value);
	else if (item)
		cJSON_ReplaceItemInObjectCaseSensitive(entry, name, cJSON_CreateNumber(value));
	else
		cJSON_AddNumberToObject(entry, name, value);
}

static cJSON* hc_files(cJSON* root, bool create) {
	cJSON* files = cJSON_GetObjectItemCaseSensitive(root, "files");
	if (cJSON_IsObject(files) || !create)
		return cJSON_IsObject(files) ? files : NULL;
	files = cJSON_CreateObject();
	if (cJSON_GetObjectItemCaseSensitive(root, "files"))
		cJSON_ReplaceItemInObjectCaseSensitive(root, "files", files);
	else
		cJSON_AddItemToObject(root, "files", files);
	return files;
}

/*****************************************************************************
 * Public
 *****************************************************************************/

bool RA_HashCache_lookup(const char* rom_path, uint32_t console_id, char hash_out[33],
						 uint32_t* game_id) {
	char dir[RA_HC_MAX_PATH], path[RA_HC_MAX_PATH + 32];
	const char* key;
	if (!hash_out || !hc_split(rom_path, dir, sizeof(dir), path, sizeof(path), &key))
		return false;
	struct stat st;
	if (stat(rom_path, &st) != 0)
		return false;
	cJSON* root = hc_load(path);
	if (!root)
		return false;

	bool hit = false;
	const cJSON* entry = cJSON_GetObjectItemCaseSensitive(hc_files(root, false), key);
	double size, mtime, console, game = 0;
	const cJSON* hash = cJSON_GetObjectItemCaseSensitive(entry, "hash");
	char clean[33];
	if (cJSON_IsObject(entry) && hc_number(entry, "size", &size) &&
		hc_number(entry, "mtime", &mtime) && hc_number(entry, "console", &console) &&
		cJSON_IsString(hash) && hc_valid_hash(hash->valuestring, clean) &&
		(uint32_t)console == console_id && (long long)size == (long long)st.st_size &&
		RA_HashCache_mtimeMatches((long long)mtime, (long long)st.st_mtime)) {
		hc_number(entry, "game", &game); // absent = unknown to RA
		memcpy(hash_out, clean, sizeof(clean));
		if (game_id)
			*game_id = (uint32_t)game;
		hit = true;
	}
	cJSON_Delete(root);
	return hit;
}

bool RA_HashCache_store(const char* rom_path, uint32_t console_id, const char* hash,
						uint32_t game_id) {
	char dir[RA_HC_MAX_PATH], path[RA_HC_MAX_PATH + 32];
	const char* key;
	char clean[33];
	if (!hc_valid_hash(hash, clean) || !hc_split(rom_path, dir, sizeof(dir), path, sizeof(path), &key))
		return false;
	struct stat st;
	if (stat(rom_path, &st) != 0)
		return false;

	cJSON* root = hc_load(path); // missing or corrupt: start over
	if (!root)
		root = cJSON_CreateObject();
	if (!root)
		return false;
	cJSON* files = hc_files(root, true);
	cJSON* entry = cJSON_GetObjectItemCaseSensitive(files, key);

	// already identical: skip the write (every launch would rewrite otherwise).
	// An mtime the validity rule accepts counts as the same, so two OSes that
	// read FAT times in different time zones don't keep rewriting each other.
	double size, mtime, console, game;
	const cJSON* old_hash = cJSON_GetObjectItemCaseSensitive(entry, "hash");
	if (cJSON_IsObject(entry) && hc_number(entry, "size", &size) &&
		hc_number(entry, "mtime", &mtime) && hc_number(entry, "console", &console) &&
		hc_number(entry, "game", &game) && cJSON_IsString(old_hash) &&
		(long long)size == (long long)st.st_size &&
		RA_HashCache_mtimeMatches((long long)mtime, (long long)st.st_mtime) &&
		(uint32_t)console == console_id && (uint32_t)game == game_id &&
		!strcmp(old_hash->valuestring, clean)) {
		cJSON_Delete(root);
		return true;
	}

	if (!cJSON_IsObject(entry)) {
		cJSON* fresh = cJSON_CreateObject();
		if (entry)
			cJSON_ReplaceItemInObjectCaseSensitive(files, key, fresh);
		else
			cJSON_AddItemToObject(files, key, fresh);
		entry = fresh;
	}
	hc_set_number(root, "version", 1);
	hc_set_number(entry, "size", (double)st.st_size);
	hc_set_number(entry, "mtime", (double)st.st_mtime);
	hc_set_number(entry, "console", console_id);
	cJSON* h = cJSON_GetObjectItemCaseSensitive(entry, "hash");
	if (h)
		cJSON_ReplaceItemInObjectCaseSensitive(entry, "hash", cJSON_CreateString(clean));
	else
		cJSON_AddStringToObject(entry, "hash", clean);
	hc_set_number(entry, "game", game_id);

	bool ok = hc_save(dir, path, root);
	cJSON_Delete(root);
	return ok;
}

void RA_HashCache_forget(const char* rom_path) {
	char dir[RA_HC_MAX_PATH], path[RA_HC_MAX_PATH + 32];
	const char* key;
	if (!hc_split(rom_path, dir, sizeof(dir), path, sizeof(path), &key))
		return;
	cJSON* root = hc_load(path);
	if (!root)
		return;
	cJSON* files = hc_files(root, false);
	if (cJSON_GetObjectItemCaseSensitive(files, key)) {
		cJSON_DeleteItemFromObjectCaseSensitive(files, key);
		hc_save(dir, path, root);
	}
	cJSON_Delete(root);
}
