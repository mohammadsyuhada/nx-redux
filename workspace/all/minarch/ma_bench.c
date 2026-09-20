#include "ma_bench.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef MA_BENCH_NO_SYSFS
#include "utils.h" // getInt, exists
#endif

static int bench_enabled = 0;
static uint32_t bench_start_ms = 0;
static uint32_t bench_last_ms = 0;

int Bench_format(char* out, size_t out_size, const PerfProfile* p, unsigned t_s, int khz0, int khz4) {
	return snprintf(out, out_size,
					"[bench] t=%u fps=%.1f req=%.1f avg_ms=%.1f max_ms=%.1f drops=%d buf_free=%d buf_target=%d cpu=%.0f khz0=%d khz4=%d\n",
					t_s, p->fps, p->req_fps, p->avg_frame_ms, p->max_frame_ms, p->frame_drops,
					p->buffer_free, p->buffer_target, p->cpu_usage, khz0, khz4);
}

void Bench_init(void) {
	const char* v = getenv("NX_BENCH");
	bench_enabled = v && v[0] && v[0] != '0';
}

void Bench_tick(uint32_t now_ms) {
	if (!bench_enabled)
		return;
	if (bench_start_ms == 0) {
		bench_start_ms = now_ms;
		bench_last_ms = now_ms;
		return;
	}
	if (now_ms - bench_last_ms < 1000)
		return;
	bench_last_ms = now_ms;
	int khz0 = 0, khz4 = 0;
#ifndef MA_BENCH_NO_SYSFS
	// policy0 / policy4 current frequency; 0 when the policy is absent (single
	// cluster) or offline. Which one is non-zero shows the cluster in use.
	khz0 = getInt("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
	if (exists("/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq"))
		khz4 = getInt("/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq");
#endif
	char line[256];
	Bench_format(line, sizeof(line), &perf, (now_ms - bench_start_ms) / 1000, khz0, khz4);
	fputs(line, stdout);
	fflush(stdout); // minarch's stdout is a file; the harness tails it live
}
