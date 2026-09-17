#include "ma_internal.h"
#include "utils.h"
#include "config.h"
#include "ma_cheats.h"
#include "ma_cheat_match.h"
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include "ma_menu.h"

// cheat types live in ma_cheats.h
struct Cheats cheatcodes;

static size_t parse_count(FILE* file) {
	size_t count = 0;
	fscanf(file, " cheats = %lu\n", (unsigned long*)&count);
	return count;
}

static const char* find_val(const char* start) {
	// skip the key token, then whitespace, expect '=', then skip whitespace;
	// every loop stops at NUL so a line without spaces can't scan out of bounds
	while (*start && !isspace((unsigned char)*start))
		start++;
	while (*start && isspace((unsigned char)*start))
		start++;

	if (*start != '=')
		return NULL;
	start++;

	while (*start && isspace((unsigned char)*start))
		start++;

	return start;
}

static int parse_bool(const char* ptr, int* out) {
	if (!strncasecmp(ptr, "true", 4)) {
		*out = 1;
	} else if (!strncasecmp(ptr, "false", 5)) {
		*out = 0;
	} else {
		return -1;
	}
	return 0;
}

static int parse_string(const char* ptr, char* buf, size_t len) {
	int index = 0;
	size_t input_len = strlen(ptr);

	buf[0] = '\0';

	if (*ptr++ != '"')
		return -1;

	while (*ptr != '\0' && *ptr != '"' && index < len - 1) {
		if (*ptr == '\\' && *(ptr + 1) != '\0') {
			ptr++;
			buf[index++] = *ptr++;
		} else if (*ptr == '&' && !strncmp(ptr, "&quot;", 6)) {
			buf[index++] = '"';
			ptr += 6;
		} else {
			buf[index++] = *ptr++;
		}
	}

	if (*ptr != '"') {
		buf[0] = '\0';
		return -1;
	}

	buf[index] = '\0';
	return 0;
}

static int parse_cheats(struct Cheats* cheats, FILE* file) {
	int ret = -1;
	char line[512];
	char buf[512];
	const char* ptr;

	do {
		if (!fgets(line, sizeof(line), file)) {
			ret = 0;
			break;
		}

		size_t line_len = strlen(line);
		if (line_len == 0)
			continue; // line starting with NUL (binary garbage)

		if (line[line_len - 1] != '\n' && !feof(file)) {
			LOG_warn("Cheat line too long\n");
			continue;
		}

		if ((ptr = strstr(line, "cheat"))) {
			int index = -1;
			struct Cheat* cheat;
			size_t len;
			sscanf(ptr, "cheat%d", &index);

			if (index < 0 || index >= cheats->count)
				continue;
			cheat = &cheats->cheats[index];

			if (strstr(ptr, "_desc")) {
				ptr = find_val(ptr);
				if (!ptr || parse_string(ptr, buf, sizeof(buf))) {
					LOG_warn("Couldn't parse cheat %d description\n", index);
					continue;
				}

				len = strlen(buf);
				if (len == 0)
					continue;

				// duplicate keys in the .cht overwrite the previous value
				free((char*)cheat->name);
				free((char*)cheat->info);
				cheat->info = NULL;
				cheat->name = calloc(len + 1, sizeof(char));
				if (!cheat->name)
					goto finish;

				strncpy((char*)cheat->name, buf, len);
				truncateString((char*)cheat->name, CHEAT_MAX_DESC_LEN);

				if (len >= CHEAT_MAX_DESC_LEN) {
					cheat->info = calloc(len + 1, sizeof(char));
					if (!cheat->info)
						goto finish;

					strncpy((char*)cheat->info, buf, len);
					wrapString((char*)cheat->info, CHEAT_MAX_LINE_LEN, CHEAT_MAX_LINES);
				}
			} else if (strstr(ptr, "_code")) {
				ptr = find_val(ptr);
				if (!ptr || parse_string(ptr, buf, sizeof(buf))) {
					LOG_warn("Couldn't parse cheat %d code\n", index);
					continue;
				}

				len = strlen(buf);
				if (len == 0)
					continue;

				free((char*)cheat->code);
				cheat->code = calloc(len + 1, sizeof(char));
				if (!cheat->code)
					goto finish;

				strncpy((char*)cheat->code, buf, len);
			} else if (strstr(ptr, "_enable")) {
				ptr = find_val(ptr);
				if (!ptr || parse_bool(ptr, &cheat->enabled)) {
					LOG_warn("Couldn't parse cheat %d enabled\n", index);
					continue;
				}
			}
		}
	} while (1);

finish:
	return ret;
}

// return variations with/without extensions and other cruft
void Cheat_getPaths(char paths[CHEAT_MAX_PATHS][MAX_PATH], int* count) {
	// Generate possible paths, ordered by most likely to be used (pre v6.2.3 style first)
	snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, game.name); // /mnt/SDCARD/Cheats/GB/Super Example World.<ext>.cht
	if (CFG_getUseExtractedFileName())
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, game.alt_name); // /mnt/SDCARD/Cheats/GB/Super Example World (USA).<ext>.cht

	// game.alt_name, but with all extension-like stuff removed (apart from .cht)
	// eg. Super Example World (USA).zip -> Super Example World (USA).cht
	{
		int i = 0;
		char* ext;
		char exts[128];
		// skip only the extension-based variants when extensions are unusable;
		// the sanitized/alias/wildcard paths below don't depend on them
		if (strlen(core.extensions) < sizeof(exts)) {
			strcpy(exts, core.extensions);
			while ((ext = strtok(i ? NULL : exts, "|"))) {
				if (*count >= CHEAT_MAX_PATHS - 1) {
					break;
				}

				char rom_name[MAX_PATH];
				if (strlen(game.alt_name) >= MAX_PATH) {
					i++;
					continue;
				}

				strcpy(rom_name, game.alt_name);
				char* tmp = strrchr(rom_name, '.');
				if (tmp != NULL && strlen(tmp) > 2 && strlen(tmp) <= 5) {
					tmp[0] = '\0';

					// Add length check before sprintf to prevent buffer overflow
					int needed_len = strlen(core.cheats_dir) + strlen(rom_name) + strlen(ext) + 10; // +10 for "/", ".", ".cht", etc.
					if (needed_len < MAX_PATH) {
						snprintf(paths[(*count)++], MAX_PATH, "%s/%s.%s.cht", core.cheats_dir, rom_name, ext);
					}
				}
				i++;
			}
		}
	}

	// Sanitized: remove extension and other cruft
	// eg. Super Example World (USA).zip -> Super Example World
	//     Super Example World (USA) [!].7z -> Super Example World
	//     Super Example World (USA) (Rev 1).rar -> Super Example World
	char rom_name[MAX_PATH];
	getDisplayName(game.alt_name, rom_name);
	if (*count < CHEAT_MAX_PATHS)
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, rom_name); // /mnt/SDCARD/Cheats/GB/Super Example World.cht
	// Respect map.txt: use alias if available
	// eg. 1941.zip	-> 1941: Counter Attack
	if (getAlias(game.path, rom_name) && *count < CHEAT_MAX_PATHS)
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, rom_name); // /mnt/SDCARD/Cheats/GB/Super Example World.cht

	// Santitized alias, ignoring all extra cruft - including Cheat specifics like "(Game Breaker)" etc.
	// This is a wildcard that may match something unexpected, but also may find something when nothing else does.
	getDisplayName(game.alt_name, rom_name);
	getAlias(game.path, rom_name);
	if (*count < CHEAT_MAX_PATHS)
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s*.cht", core.cheats_dir, rom_name); // /mnt/SDCARD/Cheats/GB/Super Example World*.cht
}

void Cheats_free() {
	if (cheatcodes.cheats) {
		size_t i;
		for (i = 0; i < cheatcodes.count; i++) {
			struct Cheat* cheat = &cheatcodes.cheats[i];
			free((char*)cheat->name);
			free((char*)cheat->info);
			free((char*)cheat->code);
		}
		free(cheatcodes.cheats);
		cheatcodes.cheats = NULL;
	}
	cheatcodes.count = 0;
}

// getDisplayName-based stem for the current game, for matcher comparison.
static const char* game_display_for_cheats(void) {
	static char disp[MAX_PATH];
	getDisplayName(game.alt_name, disp);
	getAlias(game.path, disp);
	return disp;
}

// Free one standalone struct Cheats (used when an append fails mid-way).
static void Cheats_free_one(struct Cheats* c) {
	if (!c->cheats)
		return;
	for (size_t i = 0; i < c->count; i++) {
		free((char*)c->cheats[i].name);
		free((char*)c->cheats[i].info);
		free((char*)c->cheats[i].code);
	}
	free(c->cheats);
	c->cheats = NULL;
	c->count = 0;
}

// Parse exactly one .cht file into `out` (out->cheats allocated here).
// Returns 1 on success, 0 on failure (out left empty/freed by the caller).
static int load_one_file(const char* path, struct Cheats* out) {
	out->count = 0;
	out->cheats = NULL;

	FILE* file = fopen(path, "r");
	if (!file) {
		LOG_error("Couldn't open cheat file: %s\n", path);
		return 0;
	}

	out->count = parse_count(file);
	if (out->count <= 0) {
		LOG_error("Couldn't read cheat count\n");
		fclose(file);
		return 0;
	}

	out->cheats = calloc(out->count, sizeof(struct Cheat));
	if (!out->cheats) {
		LOG_error("Couldn't allocate memory for cheats\n");
		out->count = 0;
		fclose(file);
		return 0;
	}

	if (parse_cheats(out, file)) {
		LOG_error("Error parsing cheat file: %s\n", path);
		fclose(file);
		return 0;
	}

	fclose(file);
	return 1;
}

// Append every cheat in `src` onto the global cheatcodes array, taking
// ownership of src's allocated strings (src's array itself is freed, its
// entries are moved). When `source` is non-empty (a merged multi-variant
// load), prefix each cheat's detail (info) line with "(<source>) " so the
// Y-detail view shows which cheat system it came from.
static int cheats_append(struct Cheats* src, const char* source) {
	if (src->count <= 0)
		return 1;
	size_t new_count = cheatcodes.count + src->count;
	struct Cheat* grown = realloc(cheatcodes.cheats, new_count * sizeof(struct Cheat));
	if (!grown)
		return 0;
	cheatcodes.cheats = grown;

	for (size_t i = 0; i < src->count; i++) {
		struct Cheat* c = &src->cheats[i];
		if (source && source[0]) {
			// Build "(source) " + existing info-or-name into a fresh info line.
			const char* base = c->info ? c->info : (c->name ? c->name : "");
			size_t len = strlen(source) + strlen(base) + 4; // "(" ") " NUL
			char* tagged = calloc(len, sizeof(char));
			if (tagged) {
				snprintf(tagged, len, "(%s) %s", source, base);
				free((char*)c->info);
				c->info = tagged;
			}
		}
		cheatcodes.cheats[cheatcodes.count + i] = *c; // move pointers
	}
	cheatcodes.count = new_count;
	free(src->cheats); // entries moved; free only the array
	src->cheats = NULL;
	src->count = 0;
	return 1;
}

bool Cheats_load() {
	int success = 0;
	size_t i;

	// we get our paths frrom Cheat_getPaths, some might be wildcards
	char paths[CHEAT_MAX_PATHS][MAX_PATH];
	int path_count = 0;
	Cheat_getPaths(paths, &path_count);

	for (i = 0; i < path_count; i++) {
		if (strchr(paths[i], '*')) {
			// Wildcard: glob, then rank + merge the matches.
			glob_t g;
			memset(&g, 0, sizeof(g));
			if (glob(paths[i], 0, NULL, &g) != 0 || g.gl_pathc == 0) {
				globfree(&g);
				continue;
			}

			// Keep only real .cht matches; collect full paths + basenames.
			int gc = 0;
			int max = (int)g.gl_pathc;
			if (max > 512)
				max = 512; // sane cap for a single game's glob
			char (*fulls)[MAX_PATH] = calloc((size_t)max, sizeof(*fulls));
			char (*bases)[MAX_PATH] = calloc((size_t)max, sizeof(*bases));
			const char** baseptrs = calloc((size_t)max, sizeof(char*));
			if (!fulls || !bases || !baseptrs) {
				free(fulls);
				free(bases);
				free(baseptrs);
				globfree(&g);
				continue;
			}
			for (int gi = 0; gi < max; gi++) {
				if (!suffixMatch(".cht", g.gl_pathv[gi]))
					continue;
				if (!exists(g.gl_pathv[gi]))
					continue;
				snprintf(fulls[gc], MAX_PATH, "%s", g.gl_pathv[gi]);
				char tmp[MAX_PATH];
				snprintf(tmp, sizeof(tmp), "%s", g.gl_pathv[gi]);
				snprintf(bases[gc], MAX_PATH, "%s", basename(tmp));
				baseptrs[gc] = bases[gc];
				gc++;
			}
			globfree(&g);

			if (gc > 0) {
				int sel[512];
				int nsel = CheatMatch_select(game_display_for_cheats(),
											 game.alt_name,
											 baseptrs, gc, sel, gc);
				if (nsel <= 0) {
					// No confident match: legacy behaviour, load the first hit.
					sel[0] = 0;
					nsel = 1;
				}
				for (int s = 0; s < nsel; s++) {
					struct Cheats one;
					if (load_one_file(fulls[sel[s]], &one)) {
						char src[64] = {0};
						if (nsel > 1)
							CheatMatch_sourceLabel(bases[sel[s]], src, sizeof(src));
						if (cheats_append(&one, src)) {
							success = 1;
						} else {
							Cheats_free_one(&one);
						}
					}
				}
			}
			free(fulls);
			free(bases);
			free(baseptrs);
			if (success)
				break;
			continue;
		} else {
			// Exact candidate: unchanged single-file behaviour.
			if (!exists(paths[i]))
				continue;
			struct Cheats one;
			if (load_one_file(paths[i], &one)) {
				if (cheats_append(&one, NULL))
					success = 1;
				else
					Cheats_free_one(&one);
			}
			if (success)
				break;
		}
	}

	if (!success)
		Cheats_free();
	return success;
}
