#!/usr/bin/env bash
# Host unit test for the "Show search hint" Appearance setting round-trip in
# workspace/all/common/config.c (default visible for installs whose settings
# file predates the key, parse both ways, persist on set). Compiles config.c
# plus its two pure-libc dependencies (paths.c, utils.c) with the host compiler
# against the desktop platform headers; nothing is built for a device.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PREFIX="${PREFIX:-/opt/homebrew}"
CFLAGS=(-std=gnu99 -O1 -DUSE_SDL2 -DPLATFORM=\"desktop\" -DHAS_RUNTIME_PATHS
    -I workspace/all/common -I workspace/all/common/ui
    -I workspace/desktop/platform -I workspace/desktop/libmsettings
    -I "$PREFIX/include" -I "$PREFIX/include/SDL2")
for src in config paths utils; do
    cc "${CFLAGS[@]}" -w -c -o "$TMP/$src.o" "workspace/all/common/$src.c"
done
cc "${CFLAGS[@]}" -Wall -Wextra -Werror -c -o "$TMP/test.o" \
    scripts/tests/searchhint/search_hint_cfg_test.c
cc -o "$TMP/search_hint_cfg_test" "$TMP"/config.o "$TMP"/paths.o "$TMP"/utils.o "$TMP/test.o"
mkdir -p "$TMP/userdata"
"$TMP/search_hint_cfg_test" "$TMP/userdata"
