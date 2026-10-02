#include "../../nextui/collcount_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void collcount_roundtrip(void) {
	CollCountLine in = {"Faves", 1790000000LL, 512, "a1b2c3", 24}, out;
	char line[512];
	CollCount_formatLine(&in, line, sizeof(line));
	assert(CollCount_parseLine(line, &out));
	assert(strcmp(out.name, "Faves") == 0 && out.mtime == in.mtime && out.size == 512 && out.count == 24 &&
		   strcmp(out.libfp, "a1b2c3") == 0);
}

static void collcount_rejects_corrupt(void) {
	CollCountLine out;
	assert(!CollCount_parseLine("", &out));
	assert(!CollCount_parseLine("Faves\t1\t2\tfp", &out));	   // missing count
	assert(!CollCount_parseLine("Faves\tx\t2\tfp\t3", &out));  // non-numeric mtime
	assert(!CollCount_parseLine("Faves\t1\t2\tfp\t-4", &out)); // negative count
	assert(!CollCount_parseLine("\t1\t2\tfp\t3", &out));	   // empty name
}

static void collcount_stamps(void) {
	CollCountLine c = {"Faves", 100, 50, "fp1", 7};
	assert(CollCount_matches(&c, 100, 50, "fp1"));
	assert(!CollCount_matches(&c, 101, 50, "fp1")); // file edited
	assert(!CollCount_matches(&c, 100, 51, "fp1"));
	assert(!CollCount_matches(&c, 100, 50, "fp2")); // library rescanned
	assert(!CollCount_matches(&c, 100, 50, ""));	// fingerprint unknown: never trust
}

static void collcount_stamp_cache(void) {
	CollCountStamps s = {0};
	assert(!CollCount_stampFind(&s, "Faves.txt", 0)); // empty
	assert(CollCount_stampPut(&s, "Faves.txt", true, 100, 50, 3));
	const CollCountStamp* st = CollCount_stampFind(&s, "Faves.txt", 3);
	assert(st && st->ok && st->mtime == 100 && st->size == 50);
	assert(!CollCount_stampFind(&s, "Faves.txt", 4)); // generation moved: stat again
	assert(!CollCount_stampFind(&s, "Other.txt", 3));
	assert(CollCount_stampPut(&s, "Faves.txt", false, 0, 0, 4)); // re-stamped, now missing: same slot
	st = CollCount_stampFind(&s, "Faves.txt", 4);
	assert(st && !st->ok && s.len == 1);
	for (int i = 0; i < 40; i++) { // growth keeps earlier stamps
		char name[32];
		snprintf(name, sizeof(name), "C%d.txt", i);
		assert(CollCount_stampPut(&s, name, true, i, i, 4));
	}
	st = CollCount_stampFind(&s, "C7.txt", 4);
	assert(s.len == 41 && st && st->mtime == 7 && CollCount_stampFind(&s, "Faves.txt", 4));
	char big[300];
	memset(big, 'a', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	assert(!CollCount_stampPut(&s, big, true, 1, 1, 4)); // name too long
	CollCount_stampsFree(&s);
	assert(!s.items && s.len == 0);
}

int main(void) {
	collcount_roundtrip();
	collcount_rejects_corrupt();
	collcount_stamps();
	collcount_stamp_cache();
	printf("test_collcount_model: ok\n");
	return 0;
}
