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

int main(void) {
	collcount_roundtrip();
	collcount_rejects_corrupt();
	collcount_stamps();
	printf("test_collcount_model: ok\n");
	return 0;
}
