#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../nx/nx_legacy_save.h"

static int fails;
#define CHECK(c, m)                  \
	do {                             \
		if (c)                       \
			printf("PASS: %s\n", m); \
		else {                       \
			printf("FAIL: %s\n", m); \
			fails++;                 \
		}                            \
	} while (0)

static void put(const char* dir, const char* name, int fill, size_t n) {
	char p[512];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	FILE* f = fopen(p, "wb");
	for (size_t i = 0; i < n; i++)
		fputc(fill, f);
	fclose(f);
}

int main(void) {
	char dir[] = "/tmp/nxlegacyXXXXXX";
	if (!mkdtemp(dir))
		return 1;
	uint8_t eep[0x800], mpk[0x8000 * 4], sra[0x8000], fla[0x20000];
	const char* md5 = "3A67D998AABBCCDDEEFF001122334455";

	// 1. nothing there: no import, buffers untouched
	memset(eep, 0xEE, sizeof eep);
	CHECK(nx_legacy_save_import(dir, md5, eep, sizeof eep, mpk, sizeof mpk, sra, sizeof sra, fla, sizeof fla) == 0, "empty dir imports nothing");
	CHECK(eep[0] == 0xEE, "empty dir leaves eeprom alone");

	// 2. goodname-independent match on the MD5 suffix, any case
	put(dir, "Mario Kart 64 (U) [!]-3a67d998.eep", 0x11, 512);
	put(dir, "Some Other Name-3A67D998.mpk", 0x22, 0x8000 * 4);
	put(dir, "Other Game-12345678.sra", 0x33, 0x8000);
	int got = nx_legacy_save_import(dir, md5, eep, sizeof eep, mpk, sizeof mpk, sra, sizeof sra, fla, sizeof fla);
	CHECK(got == (1 | 2), "eep + mpk imported, other game's sra ignored");
	CHECK(eep[0] == 0x11 && eep[511] == 0x11 && eep[512] == 0xEE, "512-byte eeprom copied, rest kept");
	CHECK(mpk[0] == 0x22 && mpk[sizeof mpk - 1] == 0x22, "all four controller paks copied");

	// 3. oversized file is clipped to the buffer
	put(dir, "X-3A67D998.fla", 0x44, 0x20000 + 100);
	got = nx_legacy_save_import(dir, md5, eep, sizeof eep, mpk, sizeof mpk, sra, sizeof sra, fla, sizeof fla);
	CHECK(got & 8, "flashram imported");
	CHECK(fla[0x20000 - 1] == 0x44, "flashram clipped to its size");

	// 4. NULL / empty dir or md5 is a no-op
	CHECK(nx_legacy_save_import(NULL, md5, eep, sizeof eep, mpk, sizeof mpk, sra, sizeof sra, fla, sizeof fla) == 0, "NULL dir");
	CHECK(nx_legacy_save_import(dir, "", eep, sizeof eep, mpk, sizeof mpk, sra, sizeof sra, fla, sizeof fla) == 0, "empty md5");

	// 5. the directory is never written to
	struct stat st;
	char p[512];
	snprintf(p, sizeof p, "%s/Mario Kart 64 (U) [!]-3a67d998.eep", dir);
	stat(p, &st);
	CHECK(st.st_size == 512, "source file untouched");

	printf(fails ? "FAILED (%d failures)\n" : "ALL PASS (0 failures)\n", fails);
	return fails != 0;
}
