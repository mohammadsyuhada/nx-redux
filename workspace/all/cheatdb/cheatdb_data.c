#include "cheatdb_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const CheatdbMap CHEATDB_MAP[] = {
	{"FC", "Nintendo - Nintendo Entertainment System"},
	{"FDS", "Nintendo - Family Computer Disk System"},
	{"GB", "Nintendo - Game Boy"},
	{"SGB", "Nintendo - Game Boy"},
	{"GBC", "Nintendo - Game Boy Color"},
	{"GBA", "Nintendo - Game Boy Advance"},
	{"MGBA", "Nintendo - Game Boy Advance"},
	{"SFC", "Nintendo - Super Nintendo Entertainment System"},
	{"SUPA", "Nintendo - Super Nintendo Entertainment System"},
	{"VB", "Nintendo - Virtual Boy"},
	{"N64", "Nintendo - Nintendo 64"},
	{"NDS", "Nintendo - Nintendo DS"},
	{"SMS", "Sega - Master System - Mark III"},
	{"GG", "Sega - Game Gear"},
	{"MD", "Sega - Mega Drive - Genesis"},
	{"32X", "Sega - 32X"},
	{"SEGACD", "Sega - Mega-CD - Sega CD"},
	{"SG1000", "Sega - SG-1000"},
	{"DC", "Sega - Dreamcast"},
	{"PS", "Sony - PlayStation"},
	{"PCE", "NEC - PC Engine - TurboGrafx 16"},
	{"A2600", "Atari - 2600"},
	{"A5200", "Atari - 5200"},
	{"A7800", "Atari - 7800"},
	{"LYNX", "Atari - Lynx"},
	{"COLECO", "Coleco - ColecoVision"},
	{"NGP", "SNK - Neo Geo Pocket"},
	{"NGPC", "SNK - Neo Geo Pocket Color"},
	{"FBN", "FBNeo - Arcade Games"},
	{"MSX", "Microsoft - MSX - MSX2 - MSX2P - MSX Turbo R"},
	{"PRBOOM", "PrBoom"},
};
const int CHEATDB_MAP_COUNT = (int)(sizeof(CHEATDB_MAP) / sizeof(CHEATDB_MAP[0]));

static const char* env_or(const char* k, const char* fallback) {
	const char* v = getenv(k);
	return (v && v[0]) ? v : fallback;
}

void Cheatdb_initPaths(CheatdbPaths* p) {
	const char* sd = env_or("SDCARD_PATH", "/mnt/SDCARD");
	const char* cheats = getenv("CHEATS_PATH");
	if (cheats && cheats[0])
		snprintf(p->cheats_dir, sizeof(p->cheats_dir), "%s", cheats);
	else
		snprintf(p->cheats_dir, sizeof(p->cheats_dir), "%s/Cheats", sd);
	const char* su = getenv("SHARED_USERDATA_PATH");
	if (su && su[0])
		snprintf(p->state_dir, sizeof(p->state_dir), "%s/xtras", su);
	else
		snprintf(p->state_dir, sizeof(p->state_dir), "%s/.userdata/shared/xtras", sd);
	snprintf(p->manifest, sizeof(p->manifest), "%s/cheatdb.manifest", p->state_dir);
	snprintf(p->dbver, sizeof(p->dbver), "%s/cheatdb.db_lastmod", p->state_dir);
	snprintf(p->tmpdir, sizeof(p->tmpdir), "%s/.extras_tmp", sd);
	const char* testzip = getenv("CHEATDB_TEST_ZIP");
	if (testzip && testzip[0])
		snprintf(p->zip, sizeof(p->zip), "%s", testzip);
	else
		snprintf(p->zip, sizeof(p->zip), "%s/cheats.zip", p->tmpdir);
	char dflt[512];
	snprintf(dflt, sizeof(dflt), "%s/.system/shared/bin/7zzs.aarch64", sd);
	snprintf(p->unzip, sizeof(p->unzip), "%s", env_or("NX_EXTRAS_UNZIP", dflt));
	mkdir(p->state_dir, 0755);
}

bool Cheatdb_installed(const CheatdbPaths* p) {
	struct stat st;
	return stat(p->manifest, &st) == 0 && st.st_size > 0;
}

int Cheatdb_totalCount(const CheatdbPaths* p) {
	FILE* f = fopen(p->manifest, "r");
	if (!f)
		return 0;
	int n = 0, c;
	int prev = '\n';
	while ((c = fgetc(f)) != EOF) {
		if (c == '\n')
			n++;
		prev = c;
	}
	if (prev != '\n')
		n++; // last line without trailing newline
	fclose(f);
	return n;
}

bool Cheatdb_readDbVersion(const CheatdbPaths* p, char* out, size_t n) {
	FILE* f = fopen(p->dbver, "r");
	if (!f)
		return false;
	if (!fgets(out, (int)n, f)) {
		fclose(f);
		return false;
	}
	fclose(f);
	out[strcspn(out, "\r\n")] = '\0';
	return out[0] != '\0';
}

void Cheatdb_writeDbVersion(const CheatdbPaths* p, const char* lastmod) {
	mkdir(p->state_dir, 0755);
	FILE* f = fopen(p->dbver, "w");
	if (f) {
		fprintf(f, "%s\n", lastmod ? lastmod : "");
		fclose(f);
	}
}

void Cheatdb_truncateManifest(const CheatdbPaths* p) {
	mkdir(p->state_dir, 0755);
	FILE* f = fopen(p->manifest, "w");
	if (f)
		fclose(f);
}

// Extract a folder's *.cht FLAT into destdir via 7zzs (or unzip fallback).
int Cheatdb_extractFolder(const CheatdbPaths* p, const char* folder, const char* destdir) {
	mkdir(destdir, 0755);
	char cmd[2048];
	if (strstr(p->unzip, "7zzs"))
		snprintf(cmd, sizeof(cmd), "'%s' e -y -o'%s' '%s' '%s/*.cht' >/dev/null 2>&1", p->unzip, destdir, p->zip, folder);
	else
		snprintf(cmd, sizeof(cmd), "'%s' -j -o -q '%s' '%s/*.cht' -d '%s' >/dev/null 2>&1", p->unzip, p->zip, folder, destdir);
	system(cmd);
	return 0; // missing folder is not fatal
}

// Append the folder's *.cht basenames from the ARCHIVE INDEX to the manifest.
int Cheatdb_appendManifest(const CheatdbPaths* p, const char* folder, const char* destdir) {
	char cmd[2048];
	if (strstr(p->unzip, "7zzs"))
		snprintf(cmd, sizeof(cmd), "'%s' l '%s' '%s/*.cht' 2>/dev/null | grep '\\.cht$' | sed 's#.*/##'", p->unzip, p->zip, folder);
	else
		snprintf(cmd, sizeof(cmd), "'%s' -l '%s' '%s/*.cht' 2>/dev/null | grep '\\.cht$' | sed 's#.*/##'", p->unzip, p->zip, folder);
	FILE* in = popen(cmd, "r");
	if (!in)
		return -1;
	FILE* man = fopen(p->manifest, "a");
	if (!man) {
		pclose(in);
		return -1;
	}
	char line[512];
	while (fgets(line, sizeof(line), in)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0])
			fprintf(man, "%s/%s\n", destdir, line);
	}
	fclose(man);
	pclose(in);
	return 0;
}

void Cheatdb_removeAll(const CheatdbPaths* p) {
	FILE* f = fopen(p->manifest, "r");
	if (f) {
		char line[600];
		// Collect unique parent dirs to prune afterwards (bounded, 64 tags).
		char dirs[64][512];
		int ndirs = 0;
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (!line[0])
				continue;
			remove(line);
			char d[512];
			snprintf(d, sizeof(d), "%s", line);
			char* slash = strrchr(d, '/');
			if (slash)
				*slash = '\0';
			int seen = 0;
			for (int i = 0; i < ndirs; i++)
				if (strcmp(dirs[i], d) == 0) {
					seen = 1;
					break;
				}
			if (!seen && ndirs < 64)
				snprintf(dirs[ndirs++], 512, "%s", d);
		}
		fclose(f);
		for (int i = 0; i < ndirs; i++)
			rmdir(dirs[i]); // fails (kept) if non-empty
	}
	remove(p->manifest);
	remove(p->dbver);
}

// Probe remote Last-Modified via `wget -S --spider`. Returns length (0 = unknown).
int Cheatdb_remoteLastModified(char* out, size_t n) {
	out[0] = '\0';
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "wget -S --spider --no-check-certificate --timeout=30 --tries=1 '%s' 2>&1 "
			 "| sed -n 's/.*[Ll]ast-[Mm]odified: *//p' | head -1 | tr -d '\\r'",
			 CHEATDB_URL);
	FILE* in = popen(cmd, "r");
	if (!in)
		return 0;
	if (fgets(out, (int)n, in))
		out[strcspn(out, "\r\n")] = '\0';
	pclose(in);
	return (int)strlen(out);
}
