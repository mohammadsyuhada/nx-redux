// Host-compiled unit test for positions.c (no device toolchain needed).
// Build & run:
//   cc -DPOSITIONS_DIR='"/tmp/pos-test"' -DPOSITIONS_FILE='"/tmp/pos-test/positions.cfg"' \
//      positions.c tests/test_positions.c -o /tmp/test_positions && /tmp/test_positions
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../positions.h"

int main(void) {
	system("rm -rf /tmp/pos-test");

	// Empty store: no file on disk yet
	Positions_init();
	assert(Positions_get("/Videos/a.mp4") == 0);
	assert(Positions_get(NULL) == 0);

	// Set + get roundtrip
	Positions_set("/Videos/a.mp4", 120);
	assert(Positions_get("/Videos/a.mp4") == 120);

	// Update overwrites
	Positions_set("/Videos/a.mp4", 300);
	assert(Positions_get("/Videos/a.mp4") == 300);

	// Second entry; both retrievable
	Positions_set("/Videos/b.mkv", 45);
	assert(Positions_get("/Videos/a.mp4") == 300);
	assert(Positions_get("/Videos/b.mkv") == 45);

	// Rejects garbage inputs without corrupting the store
	Positions_set("", 50);
	Positions_set("/Videos/c.avi", 0);
	Positions_set(NULL, 10);
	assert(Positions_get("/Videos/c.avi") == 0);

	// Persistence across re-init
	Positions_init();
	assert(Positions_get("/Videos/a.mp4") == 300);
	assert(Positions_get("/Videos/b.mkv") == 45);

	// Remove persists
	Positions_remove("/Videos/a.mp4");
	assert(Positions_get("/Videos/a.mp4") == 0);
	Positions_init();
	assert(Positions_get("/Videos/a.mp4") == 0);

	// LRU cap at 200: inserting 201 fresh entries evicts the oldest
	for (int i = 0; i <= 200; i++) {
		char p[64];
		snprintf(p, sizeof(p), "/Videos/f%03d.mp4", i);
		Positions_set(p, i + 1);
	}
	assert(Positions_get("/Videos/b.mkv") == 0);	// evicted first
	assert(Positions_get("/Videos/f000.mp4") == 0); // evicted next
	assert(Positions_get("/Videos/f001.mp4") == 2);
	assert(Positions_get("/Videos/f200.mp4") == 201);

	// Corrupt lines are skipped on load
	system("mkdir -p /tmp/pos-test && printf 'garbage\\nx|/no/number\\n-5|/neg/sec\\n77|relative/path\\n90|/ok/file.mp4\\n' > /tmp/pos-test/positions.cfg");
	Positions_init();
	assert(Positions_get("/no/number") == 0);
	assert(Positions_get("/neg/sec") == 0);
	assert(Positions_get("relative/path") == 0);
	assert(Positions_get("/ok/file.mp4") == 90);

	printf("all tests passed\n");
	return 0;
}
