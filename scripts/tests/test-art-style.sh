#!/usr/bin/env bash
# Host unit tests for the "Game art style" (Thumbnail / Background) feature.
#
#  1. Config round-trip in workspace/all/common/config.c (defaults Thumbnail /
#     Mix so installs predating the artStyle and artType keys are unchanged;
#     parse; clamp; persist; the background style forcing Screenshot), and the
#     art-path resolution in utils.c (variant subfolder, fallback to the mix
#     composite). Compiles those TUs like test-search-hint.sh; needs no SDL and
#     always runs.
#  2. The Background compositor workspace/all/nextui/artbg.c (geometry, right-
#     alignment, diagonal fade, monotonic opacity, straight alpha). Needs host
#     SDL2; SKIPs (exit 0) when SDL2 cannot be found.
#
# Nothing here builds anything for a device.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PREFIX="${PREFIX:-/opt/homebrew}"

CFLAGS=(-std=gnu99 -O1 -DUSE_SDL2 -DPLATFORM=\"desktop\" -DHAS_RUNTIME_PATHS
    -I workspace/all/common -I workspace/all/common/ui
    -I workspace/desktop/platform -I workspace/desktop/libmsettings
    -I "$PREFIX/include" -I "$PREFIX/include/SDL2")

# ---------------------------------------------------------------------------
# 1. Config round-trip (no SDL needed)
# ---------------------------------------------------------------------------
echo "== config round-trip =="
for src in config paths utils; do
    cc "${CFLAGS[@]}" -w -c -o "$TMP/$src.o" "workspace/all/common/$src.c"
done
cc "${CFLAGS[@]}" -Wall -Wextra -Werror -c -o "$TMP/cfg_test.o" \
    scripts/tests/artstyle/art_style_cfg_test.c
cc -o "$TMP/art_style_cfg_test" "$TMP"/config.o "$TMP"/paths.o "$TMP"/utils.o "$TMP/cfg_test.o"
mkdir -p "$TMP/userdata"
"$TMP/art_style_cfg_test" "$TMP/userdata"

echo "== art path resolution =="
cc "${CFLAGS[@]}" -Wall -Wextra -Werror -c -o "$TMP/path_test.o" \
    scripts/tests/artstyle/art_path_test.c
cc -o "$TMP/art_path_test" "$TMP"/utils.o "$TMP"/paths.o "$TMP/path_test.o"
mkdir -p "$TMP/rom"
"$TMP/art_path_test" "$TMP/rom"

# ---------------------------------------------------------------------------
# 2. Background compositor (needs host SDL2)
# ---------------------------------------------------------------------------
# Detect SDL2. Prefer pkg-config when present; otherwise fall back to the
# PREFIX layout (headers + libSDL2), same PREFIX convention as above.
SDL_CFLAGS=""
SDL_LIBS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 2>/dev/null; then
    SDL_CFLAGS="$(pkg-config --cflags sdl2)"
    SDL_LIBS="$(pkg-config --libs sdl2)"
elif [ -f "$PREFIX/include/SDL2/SDL.h" ] && ls "$PREFIX"/lib/libSDL2* >/dev/null 2>&1; then
    SDL_CFLAGS="-I$PREFIX/include -I$PREFIX/include/SDL2"
    SDL_LIBS="-L$PREFIX/lib -lSDL2"
else
    echo "SKIP: test-art-style (host SDL2 not found; config round-trip passed)"
    exit 0
fi

echo "== artbg compositor =="
# artbg.c is deliberately self-contained (SDL + libm), so it compiles clean
# with the strict flags and links only against SDL2.
cc -std=gnu99 -O1 -Wall -Wextra -Werror $SDL_CFLAGS \
    -I workspace/all/nextui \
    -c -o "$TMP/artbg.o" workspace/all/nextui/artbg.c
cc -std=gnu99 -O1 -Wall -Wextra -Werror $SDL_CFLAGS \
    -I workspace/all/nextui \
    -c -o "$TMP/test_artbg.o" scripts/tests/artstyle/test_artbg.c
cc -o "$TMP/test_artbg" "$TMP/artbg.o" "$TMP/test_artbg.o" $SDL_LIBS -lm
"$TMP/test_artbg"

echo "PASS: test-art-style"
