#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "positions.h"

// Tests compile on the host with -DPOSITIONS_DIR/-DPOSITIONS_FILE;
// device builds derive both from APP_DATA_DIR.
#ifndef POSITIONS_FILE
#include "vp_defines.h"
#define POSITIONS_DIR APP_DATA_DIR
#define POSITIONS_FILE APP_DATA_DIR "/positions.cfg"
#endif

#define MAX_POSITIONS 200

typedef struct {
	int sec;
	char path[512];
} PositionEntry;

// Most-recently-updated first
static PositionEntry entries[MAX_POSITIONS];
static int entry_count = 0;

static void save_to_disk(void) {
	mkdir(POSITIONS_DIR, 0755);

	FILE* f = fopen(POSITIONS_FILE, "w");
	if (!f)
		return;

	for (int i = 0; i < entry_count; i++)
		fprintf(f, "%d|%s\n", entries[i].sec, entries[i].path);
	fclose(f);
}

static int find_entry(const char* path) {
	for (int i = 0; i < entry_count; i++)
		if (strcmp(entries[i].path, path) == 0)
			return i;
	return -1;
}

void Positions_init(void) {
	entry_count = 0;

	FILE* f = fopen(POSITIONS_FILE, "r");
	if (!f)
		return;

	char line[1024];
	while (fgets(line, sizeof(line), f) && entry_count < MAX_POSITIONS) {
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';

		char* sep = strchr(line, '|');
		if (!sep)
			continue;
		*sep = '\0';

		int sec = atoi(line);
		const char* path = sep + 1;
		// Absolute paths only; positive positions only
		if (sec <= 0 || path[0] != '/')
			continue;

		entries[entry_count].sec = sec;
		snprintf(entries[entry_count].path, sizeof(entries[0].path), "%s", path);
		entry_count++;
	}
	fclose(f);
}

int Positions_get(const char* path) {
	if (!path || !path[0])
		return 0;
	int i = find_entry(path);
	return (i >= 0) ? entries[i].sec : 0;
}

void Positions_set(const char* path, int sec) {
	if (!path || !path[0] || sec <= 0)
		return;

	int i = find_entry(path);
	if (i >= 0) {
		// Remove old slot; re-inserted at the front below
		memmove(&entries[i], &entries[i + 1], (entry_count - i - 1) * sizeof(PositionEntry));
		entry_count--;
	} else if (entry_count == MAX_POSITIONS) {
		entry_count--; // drop oldest
	}

	memmove(&entries[1], &entries[0], entry_count * sizeof(PositionEntry));
	entries[0].sec = sec;
	snprintf(entries[0].path, sizeof(entries[0].path), "%s", path);
	entry_count++;

	save_to_disk();
}

void Positions_remove(const char* path) {
	if (!path || !path[0])
		return;

	int i = find_entry(path);
	if (i < 0)
		return;

	memmove(&entries[i], &entries[i + 1], (entry_count - i - 1) * sizeof(PositionEntry));
	entry_count--;

	save_to_disk();
}
