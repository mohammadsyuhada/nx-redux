#!/usr/bin/env bash
# Host unit test for minarch's env-gated [bench] log line (ma_bench.c).
# Compiles the module with the host compiler against the shared api.h types;
# only Bench_format is exercised, so nothing else is linked.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PREFIX="${PREFIX:-/opt/homebrew}"
cc -std=gnu99 -Wall -Wextra -Werror -O1 -DUSE_SDL2 -DPLATFORM=\"desktop\" -DHAS_RUNTIME_PATHS -DMA_BENCH_NO_SYSFS \
    -I workspace/all/minarch -I workspace/all/common -I workspace/all/common/ui \
    -I workspace/desktop/platform -I workspace/desktop/libmsettings \
    -I "$PREFIX/include" -I "$PREFIX/include/SDL2" \
    -o "$TMP/bench_format_test" \
    workspace/all/minarch/ma_bench.c scripts/tests/minarch_bench/bench_format_test.c
"$TMP/bench_format_test"
