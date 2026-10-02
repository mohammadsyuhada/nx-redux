#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////
// Array

typedef struct Array {
	int count;
	int capacity;
	void** items;
} Array;

Array* Array_new(void);
void Array_push(Array* self, void* item);
void Array_unshift(Array* self, void* item);
void* Array_pop(Array* self);
void Array_remove(Array* self, void* item);
void Array_free(Array* self);
void Array_yoink(Array* self, Array* other);

void StringArray_free(Array* self);

///////////////////////////////////////
// Hash

#define HASH_BUCKETS 256
typedef struct HashNode {
	char* key;
	char* value;
	struct HashNode* next;
} HashNode;
typedef struct Hash {
	HashNode* buckets[HASH_BUCKETS];
} Hash;

Hash* Hash_new(void);
void Hash_free(Hash* self);
void Hash_set(Hash* self, const char* key, const char* value);
char* Hash_get(Hash* self, const char* key);

///////////////////////////////////////
// Entry

enum EntryType {
	ENTRY_DIR,
	ENTRY_PAK,
	ENTRY_ROM,
};

typedef struct Entry {
	char* path;
	char* name;
	char* unique;
	int type;
	int alpha; // index in parent Directory's alphas Array, which points to the index of an Entry in its entries Array :sweat_smile:
} Entry;

Entry* Entry_new(const char* path, int type);
Entry* Entry_newNamed(const char* path, int type, const char* displayName);
void Entry_free(Entry* self);
void EntryArray_sort(Array* self);
void EntryArray_free(Array* self);

///////////////////////////////////////
// IntArray

#define INT_ARRAY_MAX 27
typedef struct IntArray {
	int count;
	int items[INT_ARRAY_MAX];
} IntArray;

void IntArray_init(IntArray* self);
void IntArray_push(IntArray* self, int i);

///////////////////////////////////////
// Directory

typedef struct Directory {
	char* path;
	char* name;
	Array* entries;
	IntArray alphas;
	// rendering
	int selected;
	int start;
	int end;
	// Unique per Directory_new (never 0): per-list caches key on it, not on the pointer, since a freed
	// Directory's address is often reused by the next one (a tab switch, openDirectory's stack rebuild).
	unsigned serial;
} Directory;

void Directory_free(Directory* self);
void DirectoryArray_pop(Array* self);
void DirectoryArray_free(Array* self);

#endif // TYPES_H
