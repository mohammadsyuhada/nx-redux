#!/usr/bin/env bash
# Host unit test for the shared selection-pill glide (workspace/all/common/ui/
# ui_list.c) honouring the "Show menu animations" setting. Compiles ui_list.c
# with the host compiler against the tg5040 platform headers; only the pill
# animation entry points are exercised, so every other symbol ui_list.c
# references (GFX_*, TTF_*, ...) is left unresolved at link time on purpose.
# Needs SDL2 headers (brew install sdl2 / apt install libsdl2-dev).
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PREFIX="${PREFIX:-/opt/homebrew}"
case "$(uname -s)" in
    Darwin) UNRESOLVED="-Wl,-undefined,dynamic_lookup" ;;
    *)      UNRESOLVED="-Wl,--unresolved-symbols=ignore-all" ;;
esac
cc -std=gnu99 -O1 -w -DUSE_SDL2 -DPLATFORM=\"tg5040\" \
    -I workspace/all/common -I workspace/all/common/ui \
    -I scripts/tests/hostplat -I workspace/tg5040/platform -I workspace/tg5040/libmsettings \
    -I "$PREFIX/include" -I "$PREFIX/include/SDL2" \
    -c -o "$TMP/ui_list.o" workspace/all/common/ui/ui_list.c
cc -std=c11 -Wall -Wextra -Werror -O1 -DUSE_SDL2 -DPLATFORM=\"tg5040\" \
    -I workspace/all/common -I workspace/all/common/ui \
    -I scripts/tests/hostplat -I workspace/tg5040/platform -I workspace/tg5040/libmsettings \
    -I "$PREFIX/include" -I "$PREFIX/include/SDL2" \
    -c -o "$TMP/pill_anim_test.o" scripts/tests/menuanim/pill_anim_test.c
cc -o "$TMP/pill_anim_test" "$TMP/ui_list.o" "$TMP/pill_anim_test.o" \
    -L"$PREFIX/lib" -lSDL2 $UNRESOLVED
"$TMP/pill_anim_test"
