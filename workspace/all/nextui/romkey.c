#include "romkey.h"
#include <stdio.h>
#include <string.h>

bool RomKey_aliasSource(const char* rom_path, const char* roms_root, char* map_dir, size_t map_dir_size, char* key,
						size_t key_size) {
	const char* slash = strrchr(rom_path, '/');
	const char* base = slash ? slash + 1 : rom_path;
	size_t dir_len = slash ? (size_t)(slash - rom_path) : 0;
	size_t root_len = strlen(roms_root);
	size_t base_len = strlen(base);

	// the folder game's own cue/m3u: "<parent>/<name>/<name>.cue" under a console folder
	bool playlist = base_len > 4 && (strcmp(base + base_len - 4, ".cue") == 0 || strcmp(base + base_len - 4, ".m3u") == 0);
	if (playlist && dir_len > root_len + 1 && strncmp(rom_path, roms_root, root_len) == 0 && rom_path[root_len] == '/') {
		const char* name = rom_path + dir_len; // the parent folder's name ends here
		while (name > rom_path && name[-1] != '/')
			name--;
		size_t name_len = (size_t)(rom_path + dir_len - name);
		size_t parent_len = name > rom_path ? (size_t)(name - 1 - rom_path) : 0; // the folder's own parent
		bool console_folder = parent_len == root_len;							 // Roms/<Console>/<Console>.m3u
		if (!console_folder && parent_len > root_len && name_len == base_len - 4 && strncmp(name, base, name_len) == 0) {
			snprintf(map_dir, map_dir_size, "%.*s", (int)parent_len, rom_path);
			snprintf(key, key_size, "%.*s", (int)name_len, name);
			return true;
		}
	}
	snprintf(map_dir, map_dir_size, "%.*s", (int)dir_len, rom_path);
	snprintf(key, key_size, "%s", base);
	return false;
}

bool RomKey_artReserved(const char* png_path) {
	const char* slash = strrchr(png_path, '/');
	if (!slash || slash - png_path < 7 || strncmp(slash - 7, "/.media", 7) != 0)
		return false; // a variant folder's picture (.media/<variant>/bg.png) is a game's
	return strcmp(slash + 1, "bg.png") == 0 || strcmp(slash + 1, "bglist.png") == 0 || strcmp(slash + 1, "logo.png") == 0;
}
