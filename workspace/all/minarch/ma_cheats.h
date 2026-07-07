#pragma once

#include <stdlib.h>
#include <stdbool.h>

#include "defines.h"

struct Cheat {
	const char* name;
	const char* info;
	int enabled;
	const char* code;
};

struct Cheats {
	int enabled;
	size_t count;
	struct Cheat* cheats;
};

#define CHEAT_MAX_DESC_LEN 27
#define CHEAT_MAX_LINE_LEN 52
#define CHEAT_MAX_LINES 3

#define CHEAT_MAX_PATHS 16
#define CHEAT_MAX_DISPLAY_PATHS 8
// the list of displayed paths will be a bit shorter, we cant render that much text
#define CHEAT_MAX_LIST_LENGTH (CHEAT_MAX_DISPLAY_PATHS * MAX_PATH)

extern struct Cheats cheatcodes;

void Cheat_getPaths(char paths[CHEAT_MAX_PATHS][MAX_PATH], int* count);
void Cheats_free();
bool Cheats_load();
