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

// The collection file's stamps as last stat()ed, so a frame that asks for the same count from every visible
// tile stats each file once per generation instead of once per call. `ok` false records a failed stat (the
// file is gone): it is not re-stat()ed either until the generation moves. The caller owns the generation
// (collcount.c: a local invalidation counter + the worker's report counter + MenuTabs_generation()).
typedef struct {
	char name[256];
	long long mtime, size;
	bool ok;
	unsigned gen;
} CollCountStamp;

typedef struct {
	CollCountStamp* items;
	int len, cap;
} CollCountStamps;

// The stamp recorded for name in generation gen; NULL when there is none, or it is from another generation.
const CollCountStamp* CollCount_stampFind(const CollCountStamps* stamps, const char* name, unsigned gen);
// Records (or replaces) name's stamp for gen; false when name does not fit or memory runs out (the caller
// just stats again next time).
bool CollCount_stampPut(CollCountStamps* stamps, const char* name, bool ok, long long mtime, long long size,
						unsigned gen);
void CollCount_stampsFree(CollCountStamps* stamps);

#endif
