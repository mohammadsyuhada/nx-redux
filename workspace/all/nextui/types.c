#include "types.h"
#include "defines.h"
#include "utils.h"

///////////////////////////////////////
// Array

Array* Array_new(void) {
	Array* self = malloc(sizeof(Array));
	if (!self)
		return NULL;
	self->count = 0;
	self->capacity = 8;
	self->items = malloc(sizeof(void*) * self->capacity);
	if (!self->items) {
		free(self);
		return NULL;
	}
	return self;
}
void Array_push(Array* self, void* item) {
	if (self->count >= self->capacity) {
		int new_capacity = self->capacity * 2;
		void** tmp = realloc(self->items, sizeof(void*) * new_capacity);
		if (!tmp)
			return;
		self->items = tmp;
		self->capacity = new_capacity;
	}
	self->items[self->count++] = item;
}
void Array_unshift(Array* self, void* item) {
	if (self->count == 0)
		return Array_push(self, item);
	int prev_count = self->count;
	Array_push(self, NULL); // ensures we have enough capacity
	if (self->count == prev_count)
		return; // push failed (realloc OOM); don't shift out of bounds
	for (int i = self->count - 2; i >= 0; i--) {
		self->items[i + 1] = self->items[i];
	}
	self->items[0] = item;
}
void* Array_pop(Array* self) {
	if (self->count == 0)
		return NULL;
	return self->items[--self->count];
}
void Array_remove(Array* self, void* item) {
	if (self->count == 0 || item == NULL)
		return;
	int i = 0;
	while (i < self->count && self->items[i] != item)
		i++;
	if (i == self->count)
		return;
	for (int j = i; j < self->count - 1; j++)
		self->items[j] = self->items[j + 1];
	self->count--;
}
void Array_free(Array* self) {
	free(self->items);
	free(self);
}
void Array_yoink(Array* self, Array* other) {
	// append entries to self and take ownership
	for (int i = 0; i < other->count; i++)
		Array_push(self, other->items[i]);
	Array_free(other); // `self` now owns the entries
}

void StringArray_free(Array* self) {
	for (int i = 0; i < self->count; i++) {
		free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////
// Hash

// djb2 over the key bytes; lookups are exact-match (case-sensitive), same as
// the old linear implementation
static unsigned int Hash_bucket(const char* key) {
	return hashString(key) % HASH_BUCKETS;
}
Hash* Hash_new(void) {
	Hash* self = calloc(1, sizeof(Hash));
	return self;
}
void Hash_free(Hash* self) {
	if (!self)
		return;
	for (int i = 0; i < HASH_BUCKETS; i++) {
		HashNode* node = self->buckets[i];
		while (node) {
			HashNode* next = node->next;
			free(node->key);
			free(node->value);
			free(node);
			node = next;
		}
	}
	free(self);
}
void Hash_set(Hash* self, const char* key, const char* value) {
	unsigned int b = Hash_bucket(key);
	for (HashNode* node = self->buckets[b]; node; node = node->next) {
		if (exactMatch(node->key, key)) {
			char* dup = strdup(value);
			if (dup) {
				free(node->value);
				node->value = dup;
			}
			return;
		}
	}
	HashNode* node = malloc(sizeof(HashNode));
	if (!node)
		return;
	node->key = strdup(key);
	node->value = strdup(value);
	if (!node->key || !node->value) {
		free(node->key);
		free(node->value);
		free(node);
		return;
	}
	node->next = self->buckets[b];
	self->buckets[b] = node;
}
char* Hash_get(Hash* self, const char* key) {
	for (HashNode* node = self->buckets[Hash_bucket(key)]; node; node = node->next) {
		if (exactMatch(node->key, key))
			return node->value;
	}
	return NULL;
}

///////////////////////////////////////
// Entry

Entry* Entry_new(const char* path, int type) {
	char display_name[MAX_PATH];
	getDisplayName(path, display_name);
	Entry* self = malloc(sizeof(Entry));
	self->path = strdup(path);
	self->name = strdup(display_name);
	self->unique = NULL;
	self->type = type;
	self->alpha = 0;
	return self;
}

Entry* Entry_newNamed(const char* path, int type, const char* displayName) {
	Entry* self = Entry_new(path, type);
	free(self->name);
	self->name = strdup(displayName);
	return self;
}

void Entry_free(Entry* self) {
	if (!self)
		return;
	free(self->path);
	free(self->name);
	if (self->unique)
		free(self->unique);
	free(self);
}

static int EntryArray_sortEntry(const void* a, const void* b) {
	Entry* item1 = *(Entry**)a;
	Entry* item2 = *(Entry**)b;
	int c = strcasecmp(item1->name, item2->name);
	if (c)
		return c;
	// a total order, so qsort is deterministic: "Tetris" and "TETRIS" don't interleave (Directory_index's
	// same-name runs stay adjacent), and same-name rows keep one order from boot to boot
	c = strcmp(item1->name, item2->name);
	return c ? c : strcmp(item1->path, item2->path);
}
void EntryArray_sort(Array* self) {
	qsort(self->items, self->count, sizeof(void*), EntryArray_sortEntry);
}

void EntryArray_free(Array* self) {
	for (int i = 0; i < self->count; i++) {
		Entry_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////
// IntArray

void IntArray_init(IntArray* self) {
	self->count = 0;
	memset(self->items, 0, sizeof(int) * INT_ARRAY_MAX);
}
void IntArray_push(IntArray* self, int i) {
	if (self->count >= INT_ARRAY_MAX)
		return;
	self->items[self->count++] = i;
}

///////////////////////////////////////
// Directory

void Directory_free(Directory* self) {
	free(self->path);
	free(self->name);
	EntryArray_free(self->entries);
	free(self);
}

///////////////////////////////////////
// DirectoryArray helpers

void DirectoryArray_pop(Array* self) {
	Directory* dir = Array_pop(self);
	if (dir)
		Directory_free(dir);
}
void DirectoryArray_free(Array* self) {
	for (int i = 0; i < self->count; i++) {
		Directory_free(self->items[i]);
	}
	Array_free(self);
}
