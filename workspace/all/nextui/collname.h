#ifndef COLLNAME_H
#define COLLNAME_H

#include <stdbool.h>
#include <stddef.h>

// Collection rename rules (docs/superpowers/specs/2026-10-01-menu-sp7-page-titles-design.md, "Collections"). Names are
// the file stem: Collections/<name>.txt. Pure: no file system.
typedef enum {
	COLLNAME_OK = 0,
	COLLNAME_SAME,		// exactly the current name: nothing to do
	COLLNAME_EMPTY,		// empty or only spaces
	COLLNAME_DOT,		// a leading '.' would hide the file
	COLLNAME_RESERVED,	// "map" (any case): map.txt holds the aliases
	COLLNAME_BADCHAR,	// one of / \ : * ? " < > |
	COLLNAME_TOOLONG,	// "<name>.txt.part" would not fit a file name
	COLLNAME_DUPLICATE, // another collection has it (case-insensitive)
} CollNameResult;

// existing: every collection's name (the renamed one may be among them; an exact match of old_name is skipped).
CollNameResult CollName_validate(const char* old_name, const char* new_name, const char* const* existing, int count);
// The two names differ only in letter case (a rename through <name>.txt.part on case-insensitive storage).
bool CollName_isCaseOnly(const char* old_name, const char* new_name);
// A leftover "<name>.txt.part" (a case-only rename cut short): writes "<name>.txt" to out.
bool CollName_partTarget(const char* filename, char* out, size_t size);
// The user-facing reason for a rejection (NULL for OK and SAME).
const char* CollName_reason(CollNameResult r);

#endif
