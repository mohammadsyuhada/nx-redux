#include "nx_legacy_save.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static int ends_with_key(const char* name, const char* key) {
	size_t n = strlen(name), k = strlen(key);
	if (n < k)
		return 0;
	for (size_t i = 0; i < k; i++)
		if (tolower((unsigned char)name[n - k + i]) != tolower((unsigned char)key[i]))
			return 0;
	return 1;
}

static int load(const char* dir, const char* name, uint8_t* buf, size_t size) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE* f = fopen(path, "rb");
	if (!f)
		return 0;
	size_t got = fread(buf, 1, size, f);
	fclose(f);
	return got > 0;
}

int nx_legacy_save_import(const char* dir, const char* md5,
						  uint8_t* eeprom, size_t eeprom_size, uint8_t* mempak, size_t mempak_size,
						  uint8_t* sram, size_t sram_size, uint8_t* flashram, size_t flashram_size) {
	if (!dir || !*dir || !md5 || strlen(md5) < 8)
		return 0;
	static const char* exts[4] = {"eep", "mpk", "sra", "fla"};
	uint8_t* bufs[4] = {eeprom, mempak, sram, flashram};
	size_t sizes[4] = {eeprom_size, mempak_size, sram_size, flashram_size};
	char keys[4][16];
	for (int i = 0; i < 4; i++)
		snprintf(keys[i], sizeof keys[i], "-%.8s.%s", md5, exts[i]);
	DIR* d = opendir(dir);
	if (!d)
		return 0;
	int mask = 0;
	struct dirent* e;
	while ((e = readdir(d)))
		for (int i = 0; i < 4; i++)
			if (!(mask & (1 << i)) && ends_with_key(e->d_name, keys[i]) && load(dir, e->d_name, bufs[i], sizes[i]))
				mask |= 1 << i;
	closedir(d);
	return mask;
}
