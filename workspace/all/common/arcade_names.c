#include "arcade_names.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
	const char* stem;
	const char* title;
	const char* qualifier; // NULL when the line has none
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

	// split in place: "stem\ttitle[\tqualifier]\n" -> "stem\0title\0[qualifier\0]"
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
		if (tab && tab != line && tab[1] != '\0' && tab[1] != '\t') {
			*tab = '\0';
			items[count].stem = line;
			items[count].title = tab + 1;
			items[count].qualifier = NULL;
			char* qtab = strchr(tab + 1, '\t');
			if (qtab) {
				*qtab = '\0';
				if (qtab[1] != '\0')
					items[count].qualifier = qtab + 1;
			}
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

int ArcadeNames_isArcadeFile(const char* filename) {
	if (!filename)
		return 0;
	const char* dot = strrchr(filename, '.');
	return dot && dot != filename && (strcasecmp(dot, ".zip") == 0 || strcasecmp(dot, ".7z") == 0);
}

static const ArcadeName* lookup(const ArcadeNames* self, const char* filename) {
	if (!self || !ArcadeNames_isArcadeFile(filename))
		return NULL;
	const char* dot = strrchr(filename, '.');

	// set names are lowercase; FAT keeps whatever case a copy tool wrote
	char stem[256];
	size_t len = dot - filename;
	if (len >= sizeof(stem))
		return NULL;
	for (size_t i = 0; i < len; i++)
		stem[i] = tolower((unsigned char)filename[i]);
	stem[len] = '\0';

	ArcadeName key = {stem, NULL, NULL};
	return bsearch(&key, self->items, self->count, sizeof(ArcadeName), compareName);
}

const char* ArcadeNames_get(const ArcadeNames* self, const char* filename) {
	const ArcadeName* hit = lookup(self, filename);
	return hit ? hit->title : NULL;
}

const char* ArcadeNames_getQualifier(const ArcadeNames* self, const char* filename) {
	const ArcadeName* hit = lookup(self, filename);
	return hit ? hit->qualifier : NULL;
}

// Region words that open a DAT qualifier ("(Japan 940520)", "(US, rev A)").
// Anything else there ("bootleg", "set 1", "rev A", "World?") is not a region.
static const char* const REGIONS[] = {
	"Argentina",
	"Asia",
	"Australia",
	"Austria",
	"Brazil",
	"Canada",
	"China",
	"Euro",
	"Europe",
	"France",
	"Germany",
	"Greece",
	"Hispanic",
	"Hong Kong",
	"Italy",
	"Japan",
	"Korea",
	"New Zealand",
	"NZ",
	"Oceania",
	"Portugal",
	"Spain",
	"Switzerland",
	"Taiwan",
	"UK",
	"US",
	"USA",
	"World",
};

// Length of the region word opening qualifier's first group, or 0 when it
// does not open with one.
static size_t regionLength(const char* qualifier) {
	if (qualifier[0] != '(')
		return 0;
	const char* word = qualifier + 1;
	for (size_t i = 0; i < sizeof(REGIONS) / sizeof(REGIONS[0]); i++) {
		size_t len = strlen(REGIONS[i]);
		if (strncasecmp(word, REGIONS[i], len) != 0)
			continue; // also stops before a shorter word's terminator
		char end = word[len];
		if (end == ' ' || end == ',' || end == ')')
			return len;
	}
	return 0;
}

static int hasQualifier(const char* qualifier) {
	return qualifier && qualifier[0];
}

// qualifier labels (see arcade_names.h); 0 when they cannot tell every row
// apart or a label does not fit
static int qualifierLabels(const char* const* qualifiers, int n, char** labels, size_t label_size) {
	// one row may lack a qualifier (usually the parent set): it is the only
	// one that keeps the bare title
	int bare = 0;
	for (int i = 0; i < n; i++)
		if (!hasQualifier(qualifiers[i]) && ++bare > 1)
			return 0;

	int regions_unique = 1;
	for (int i = 0; i < n && regions_unique; i++) {
		if (!hasQualifier(qualifiers[i]))
			continue;
		size_t len = regionLength(qualifiers[i]);
		if (!len) {
			regions_unique = 0;
			break;
		}
		for (int j = 0; j < i; j++) {
			if (hasQualifier(qualifiers[j]) && regionLength(qualifiers[j]) == len && strncasecmp(qualifiers[i] + 1, qualifiers[j] + 1, len) == 0) {
				regions_unique = 0;
				break;
			}
		}
	}
	if (!regions_unique) {
		for (int i = 0; i < n; i++)
			for (int j = 0; j < i; j++)
				if (hasQualifier(qualifiers[i]) && hasQualifier(qualifiers[j]) && strcmp(qualifiers[i], qualifiers[j]) == 0)
					return 0;
	}

	for (int i = 0; i < n; i++) {
		int len = !hasQualifier(qualifiers[i]) ? snprintf(labels[i], label_size, "%s", "")
				  : regions_unique			   ? snprintf(labels[i], label_size, "(%.*s)", (int)regionLength(qualifiers[i]), qualifiers[i] + 1)
											   : snprintf(labels[i], label_size, "%s", qualifiers[i]);
		if (len < 0 || (size_t)len >= label_size)
			return 0;
	}
	return 1;
}

static size_t stemLength(const char* filename) {
	const char* dot = strrchr(filename, '.');
	return dot && dot != filename ? (size_t)(dot - filename) : strlen(filename);
}

int ArcadeNames_disambiguate(const char* const* qualifiers, const char* const* filenames, int n, char** labels, size_t label_size) {
	if (n < 2)
		return 0;
	for (int i = 0; i < n; i++)
		if (!filenames[i])
			return 0;
	if (qualifierLabels(qualifiers, n, labels, label_size))
		return 1;

	// "(stem)", or "(filename)" for every row when two stems collide
	// ("avsp.zip" vs "avsp.7z")
	int stems_unique = 1;
	for (int i = 0; i < n && stems_unique; i++)
		for (int j = 0; j < i; j++)
			if (stemLength(filenames[i]) == stemLength(filenames[j]) && strncmp(filenames[i], filenames[j], stemLength(filenames[i])) == 0) {
				stems_unique = 0;
				break;
			}
	for (int i = 0; i < n; i++) {
		size_t len = stems_unique ? stemLength(filenames[i]) : strlen(filenames[i]);
		int written = snprintf(labels[i], label_size, "(%.*s)", (int)len, filenames[i]);
		if (written < 0 || (size_t)written >= label_size)
			return 0;
	}
	return 1;
}

const char* ArcadeNames_tableTag(const char* tag) {
	if (tag && strcmp(tag, "DCX") == 0)
		return "DC";
	return tag;
}
