#ifndef COLLCOUNT_MODEL_H
#define COLLCOUNT_MODEL_H

#include <stdbool.h>
#include <stddef.h>

// One line of the collection count cache file: "name\tmtime\tsize\tlibfp\tcount\n".
// name is the collection file's name (e.g. "Faves.txt"); mtime/size stamp that file and libfp the
// rom library (Content_libraryFingerprint) the count was resolved against.
typedef struct {
	char name[256];
	long long mtime, size;
	char libfp[64];
	int count;
} CollCountLine;

bool CollCount_parseLine(const char* line, CollCountLine* out); // false on any malformed field
void CollCount_formatLine(const CollCountLine* in, char* out, size_t size);
// The cached count is reusable only when every stamp matches.
bool CollCount_matches(const CollCountLine* cached, long long mtime, long long size, const char* libfp);

#endif
