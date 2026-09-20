// Host unit test: the [bench] line minarch prints once per second under
// NX_BENCH=1 (ma_bench.c). The harness (scripts/bench) parses exactly this.
#include <stdio.h>
#include <string.h>
#include "api.h"
#include "ma_bench.h"

// Bench_tick references the shared extern PerfProfile perf (defined in api.c on
// device). This host test links only the pure formatter, so it owns a stand-in.
PerfProfile perf;

static int failures = 0;
#define CHECK(cond, ...)                                \
	do {                                                \
		if (!(cond)) {                                  \
			failures++;                                 \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);                        \
			printf("\n");                               \
		}                                               \
	} while (0)

int main(void) {
	PerfProfile p = {0};
	p.fps = 59.73;
	p.req_fps = 59.7275;
	p.avg_frame_ms = 4.2;
	p.max_frame_ms = 11.9;
	p.frame_drops = 0;
	p.buffer_free = 1200;
	p.buffer_target = 1024;
	p.cpu_usage = 57.6;
	char line[256];
	int n = Bench_format(line, sizeof(line), &p, 17, 1416000, 0);
	CHECK(n > 0 && n < (int)sizeof(line), "formats into the buffer (n=%d)", n);
	const char* want = "[bench] t=17 fps=59.7 req=59.7 avg_ms=4.2 max_ms=11.9 drops=0 buf_free=1200 buf_target=1024 cpu=58 khz0=1416000 khz4=0\n";
	CHECK(strcmp(line, want) == 0, "exact line\n got: %s want: %s", line, want);
	// truncation is safe
	char tiny[16];
	n = Bench_format(tiny, sizeof(tiny), &p, 1, 408000, 408000);
	CHECK(n >= (int)sizeof(tiny) && tiny[15] == '\0', "snprintf semantics on a short buffer");
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("ma_bench: all tests passed\n");
	return 0;
}
