#include "collcount_model.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Copies the field ending at the next tab (or the end) into out; false when it is empty, too
// long, or (for a non-last field) not tab-terminated. *cursor moves past the tab.
static bool takeField(const char** cursor, char* out, size_t size, bool last) {
	const char* start = *cursor;
	const char* end = strchr(start, '\t');
	if (last) {
		if (end)
			return false; // extra fields
		end = start + strcspn(start, "\r\n");
		if (end[0] && end[strspn(end, "\r\n")])
			return false; // text after the line break
	} else if (!end)
		return false;
	size_t len = (size_t)(end - start);
	if (len == 0 || len >= size)
		return false;
	memcpy(out, start, len);
	out[len] = '\0';
	*cursor = last ? end : end + 1;
	return true;
}

static bool parseNumber(const char* text, long long min, long long* out) {
	errno = 0;
	char* end = NULL;
	long long v = strtoll(text, &end, 10);
	if (errno || end == text || *end || v < min)
		return false;
	*out = v;
	return true;
}

bool CollCount_parseLine(const char* line, CollCountLine* out) {
	if (!line || !out)
		return false;
	CollCountLine parsed;
	char mtime[32], size[32], count[32];
	const char* cursor = line;
	if (!takeField(&cursor, parsed.name, sizeof(parsed.name), false) ||
		!takeField(&cursor, mtime, sizeof(mtime), false) ||
		!takeField(&cursor, size, sizeof(size), false) ||
		!takeField(&cursor, parsed.libfp, sizeof(parsed.libfp), false) ||
		!takeField(&cursor, count, sizeof(count), true))
		return false;
	long long n = 0;
	if (!parseNumber(mtime, LLONG_MIN, &parsed.mtime) || !parseNumber(size, 0, &parsed.size) ||
		!parseNumber(count, 0, &n) || n > INT_MAX)
		return false;
	parsed.count = (int)n;
	*out = parsed;
	return true;
}

void CollCount_formatLine(const CollCountLine* in, char* out, size_t size) {
	snprintf(out, size, "%s\t%lld\t%lld\t%s\t%d\n", in->name, in->mtime, in->size, in->libfp, in->count);
}

bool CollCount_matches(const CollCountLine* cached, long long mtime, long long size, const char* libfp) {
	return libfp && libfp[0] && cached->mtime == mtime && cached->size == size &&
		   strcmp(cached->libfp, libfp) == 0;
}

static CollCountStamp* stampSlot(const CollCountStamps* stamps, const char* name) {
	for (int i = 0; i < stamps->len; i++) {
		if (strcmp(stamps->items[i].name, name) == 0)
			return &stamps->items[i];
	}
	return NULL;
}

const CollCountStamp* CollCount_stampFind(const CollCountStamps* stamps, const char* name, unsigned gen) {
	if (!stamps || !name)
		return NULL;
	const CollCountStamp* slot = stampSlot(stamps, name);
	return slot && slot->gen == gen ? slot : NULL;
}

bool CollCount_stampPut(CollCountStamps* stamps, const char* name, bool ok, long long mtime, long long size,
						unsigned gen) {
	if (!stamps || !name || strlen(name) >= sizeof(stamps->items[0].name))
		return false;
	CollCountStamp* slot = stampSlot(stamps, name);
	if (!slot) { // one per collection file name: bounded by the Collections folder, never pruned
		if (stamps->len == stamps->cap) {
			int cap = stamps->cap ? stamps->cap * 2 : 16;
			CollCountStamp* grown = realloc(stamps->items, (size_t)cap * sizeof(CollCountStamp));
			if (!grown)
				return false;
			stamps->items = grown;
			stamps->cap = cap;
		}
		slot = &stamps->items[stamps->len++];
		snprintf(slot->name, sizeof(slot->name), "%s", name);
	}
	slot->ok = ok;
	slot->mtime = ok ? mtime : 0;
	slot->size = ok ? size : 0;
	slot->gen = gen;
	return true;
}

void CollCount_stampsFree(CollCountStamps* stamps) {
	if (!stamps)
		return;
	free(stamps->items);
	stamps->items = NULL;
	stamps->len = stamps->cap = 0;
}
