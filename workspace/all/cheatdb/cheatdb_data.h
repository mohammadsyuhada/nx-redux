#pragma once
#include <stdbool.h>
#include <stddef.h>

#define CHEATDB_URL "https://buildbot.libretro.com/assets/frontend/cheats.zip"
#define CHEATDB_NEED_KB 262144 // ~256 MB free-space floor for the unpack

typedef struct {
	char cheats_dir[512]; // $CHEATS_PATH, else $SDCARD_PATH/Cheats
	char state_dir[512];  // $SHARED_USERDATA_PATH/xtras
	char manifest[600];	  // state_dir/cheatdb.manifest
	char dbver[600];	  // state_dir/cheatdb.db_lastmod
	char tmpdir[512];	  // $SDCARD_PATH/.extras_tmp
	char zip[600];		  // tmpdir/cheats.zip  (or $CHEATDB_TEST_ZIP if set)
	char unzip[512];	  // $NX_EXTRAS_UNZIP, else 7zzs.aarch64
} CheatdbPaths;

typedef struct {
	const char* tag;
	const char* folder;
} CheatdbMap;
extern const CheatdbMap CHEATDB_MAP[];
extern const int CHEATDB_MAP_COUNT;

void Cheatdb_initPaths(CheatdbPaths* p);
bool Cheatdb_installed(const CheatdbPaths* p);
int Cheatdb_totalCount(const CheatdbPaths* p);
bool Cheatdb_readDbVersion(const CheatdbPaths* p, char* out, size_t n);
void Cheatdb_writeDbVersion(const CheatdbPaths* p, const char* lastmod);
void Cheatdb_truncateManifest(const CheatdbPaths* p);
int Cheatdb_extractFolder(const CheatdbPaths* p, const char* folder, const char* destdir);
int Cheatdb_appendManifest(const CheatdbPaths* p, const char* folder, const char* destdir);
void Cheatdb_removeAll(const CheatdbPaths* p);
int Cheatdb_remoteLastModified(char* out, size_t n);
