#include "arcade_names.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
	const char* stem;
	const char* title;
} ArcadeName;

struct ArcadeNames {
	char* buf; // the whole file; stems and titles point into it
	ArcadeName* items;
	int count;
};

static int compareName(const void* a, const void* b) {
	return strcmp(((const ArcadeName*)a)->stem, ((const ArcadeName*)b)->stem);
}

ArcadeNames* ArcadeNames_load(const char* path) {
	FILE* file = fopen(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 0) {
		fclose(file);
		return NULL;
	}

	char* buf = malloc(size + 1);
	if (!buf || fread(buf, 1, size, file) != (size_t)size) {
		free(buf);
		fclose(file);
		return NULL;
	}
	fclose(file);
	buf[size] = '\0';

	int lines = 1;
	for (long i = 0; i < size; i++)
		if (buf[i] == '\n')
			lines++;
	ArcadeName* items = malloc(sizeof(ArcadeName) * lines);
	if (!items) {
		free(buf);
		return NULL;
	}

	// split in place: "stem\ttitle\n" -> "stem\0title\0"
	int count = 0;
	char* line = buf;
	while (line && *line) {
		char* next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\r')
			line[len - 1] = '\0';
		char* tab = strchr(line, '\t');
		if (tab && tab != line && tab[1] != '\0') {
			*tab = '\0';
			items[count].stem = line;
			items[count].title = tab + 1;
			count++;
		}
		line = next;
	}
	if (count == 0) {
		free(items);
		free(buf);
		return NULL;
	}
	// the generator already sorts, but bsearch must not depend on that
	qsort(items, count, sizeof(ArcadeName), compareName);

	ArcadeNames* self = malloc(sizeof(ArcadeNames));
	if (!self) {
		free(items);
		free(buf);
		return NULL;
	}
	self->buf = buf;
	self->items = items;
	self->count = count;
	return self;
}

void ArcadeNames_free(ArcadeNames* self) {
	if (!self)
		return;
	free(self->items);
	free(self->buf);
	free(self);
}

const char* ArcadeNames_get(const ArcadeNames* self, const char* filename) {
	if (!self || !filename)
		return NULL;
	const char* dot = strrchr(filename, '.');
	if (!dot || dot == filename || (strcasecmp(dot, ".zip") != 0 && strcasecmp(dot, ".7z") != 0))
		return NULL;

	// set names are lowercase; FAT keeps whatever case a copy tool wrote
	char stem[256];
	size_t len = dot - filename;
	if (len >= sizeof(stem))
		return NULL;
	for (size_t i = 0; i < len; i++)
		stem[i] = tolower((unsigned char)filename[i]);
	stem[len] = '\0';

	ArcadeName key = {stem, NULL};
	const ArcadeName* hit = bsearch(&key, self->items, self->count, sizeof(ArcadeName), compareName);
	return hit ? hit->title : NULL;
}
