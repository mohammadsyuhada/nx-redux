#ifndef MA_BENCH_H
#define MA_BENCH_H

#include <stddef.h>
#include <stdint.h>
#include "api.h"

// Env-gated benchmark log (NX_BENCH=1): one "[bench] ..." line per second on
// stdout, read by scripts/bench. Off by default; Bench_init reads the env once.
// Bench_format is pure so the exact line is host-tested.
int Bench_format(char* out, size_t out_size, const PerfProfile* p, unsigned t_s, int khz0, int khz4);
void Bench_init(void);
void Bench_tick(uint32_t now_ms);

#endif
