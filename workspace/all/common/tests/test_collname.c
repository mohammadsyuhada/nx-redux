#include "../../nextui/collname.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* const existing[] = {"Favorites", "RPGs", "Racing"};
#define N_EXISTING 3

static CollNameResult check(const char* old_name, const char* new_name) {
	return CollName_validate(old_name, new_name, existing, N_EXISTING);
}

static void collname_rejects(void) {
	assert(check("RPGs", "") == COLLNAME_EMPTY);
	assert(check("RPGs", "   ") == COLLNAME_EMPTY); // only spaces is empty too
	assert(check("RPGs", NULL) == COLLNAME_EMPTY);
	assert(check("RPGs", ".hidden") == COLLNAME_DOT);
	assert(check("RPGs", "map") == COLLNAME_RESERVED);
	assert(check("RPGs", "MAP") == COLLNAME_RESERVED); // FAT is case-insensitive: MAP.txt is map.txt
	assert(check("RPGs", "maps") == COLLNAME_OK);
	const char* bad = "/\\:*?\"<>|";
	for (const char* c = bad; *c; c++) {
		char name[16];
		snprintf(name, sizeof(name), "a%cb", *c);
		assert(check("RPGs", name) == COLLNAME_BADCHAR);
	}
	char longname[300];
	memset(longname, 'a', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	assert(check("RPGs", longname) == COLLNAME_TOOLONG);
	longname[240] = '\0'; // the limit: 240 bytes fit, 241 don't
	assert(check("RPGs", longname) == COLLNAME_OK);
	longname[240] = 'a';
	longname[241] = '\0';
	assert(check("RPGs", longname) == COLLNAME_TOOLONG);
}

static void collname_duplicates(void) {
	assert(check("RPGs", "Racing") == COLLNAME_DUPLICATE);
	assert(check("RPGs", "racing") == COLLNAME_DUPLICATE); // case-insensitive clash
	assert(check("RPGs", "FAVORITES") == COLLNAME_DUPLICATE);
	assert(check("RPGs", "Shooters") == COLLNAME_OK);
	// the collection's own name is in the list: renaming it to anything else (or a case-only change) is no clash
	assert(check("RPGs", "Role playing") == COLLNAME_OK);
	// UTF-8 names are fine (case folding is ASCII-only: see collname.c)
	assert(check("RPGs", "Pok\xc3\xa9mon \xe3\x81\x82") == COLLNAME_OK);
}

static void collname_same_and_case_only(void) {
	assert(check("RPGs", "RPGs") == COLLNAME_SAME);
	assert(check("RPGs", "rpgs") == COLLNAME_OK); // case-only: allowed
	assert(CollName_isCaseOnly("RPGs", "rpgs"));
	assert(!CollName_isCaseOnly("RPGs", "RPGs"));
	assert(!CollName_isCaseOnly("RPGs", "RPG"));
}

static void collname_part_target(void) {
	char out[256];
	assert(CollName_partTarget("Faves.txt.part", out, sizeof(out)));
	assert(strcmp(out, "Faves.txt") == 0);
	assert(!CollName_partTarget("Faves.txt", out, sizeof(out)));
	assert(!CollName_partTarget(".txt.part", out, sizeof(out))); // no name
	assert(!CollName_partTarget("Faves.part", out, sizeof(out)));
}

int main(void) {
	collname_rejects();
	collname_duplicates();
	collname_same_and_case_only();
	collname_part_target();
	printf("test_collname: ok\n");
	return 0;
}
