#include "collname.h"
#include <string.h>
#include <strings.h>

#define COLLNAME_MAX 240 // + ".txt.part" stays under the 255-byte FAT/ext4 name limit

CollNameResult CollName_validate(const char* old_name, const char* new_name, const char* const* existing, int count) {
	if (!new_name || strspn(new_name, " ") == strlen(new_name))
		return COLLNAME_EMPTY;
	if (old_name && strcmp(old_name, new_name) == 0)
		return COLLNAME_SAME;
	if (new_name[0] == '.')
		return COLLNAME_DOT;
	if (strcasecmp(new_name, "map") == 0)
		return COLLNAME_RESERVED;
	if (strpbrk(new_name, "/\\:*?\"<>|"))
		return COLLNAME_BADCHAR;
	if (strlen(new_name) > COLLNAME_MAX)
		return COLLNAME_TOOLONG;
	// strcasecmp folds ASCII only: "É" and "é" pass here. That is fail-safe: the rename refuses a target that
	// already exists on the card (gamelist.c moveCollectionFile checks exists()), so nothing is overwritten.
	for (int i = 0; i < count; i++) {
		if (!existing[i] || (old_name && strcmp(existing[i], old_name) == 0))
			continue;
		if (strcasecmp(existing[i], new_name) == 0)
			return COLLNAME_DUPLICATE;
	}
	return COLLNAME_OK;
}

bool CollName_isCaseOnly(const char* old_name, const char* new_name) {
	return old_name && new_name && strcmp(old_name, new_name) != 0 && strcasecmp(old_name, new_name) == 0;
}

bool CollName_partTarget(const char* filename, char* out, size_t size) {
	static const char suffix[] = ".txt.part";
	size_t len = strlen(filename), slen = sizeof(suffix) - 1;
	if (len <= slen || strcmp(filename + len - slen, suffix) != 0)
		return false;
	size_t keep = len - 5; // drop ".part"
	if (keep + 1 > size)
		return false;
	memcpy(out, filename, keep);
	out[keep] = '\0';
	return true;
}

const char* CollName_reason(CollNameResult r) {
	switch (r) {
	case COLLNAME_EMPTY:
		return "The name can't be empty.";
	case COLLNAME_DOT:
		return "The name can't start with a dot.";
	case COLLNAME_RESERVED:
		return "\"map\" is reserved.";
	case COLLNAME_BADCHAR:
		return "The name can't contain / \\ : * ? \" < > |";
	case COLLNAME_TOOLONG:
		return "The name is too long.";
	case COLLNAME_DUPLICATE:
		return "Another collection has that name.";
	default:
		return NULL;
	}
}
