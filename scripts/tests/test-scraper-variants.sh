#!/usr/bin/env bash
# Host unit test for the Artwork Manager art variants: the variant-path helper
# Scraper_variantPath (workspace/all/scraper/scraper_core.c) and the
# single-image compositor Compositor_createSingle (scraper_compositor.c).
# Compiles those device TUs with the host compiler + host SDL2/SDL2_image and
# drives them directly; nothing is built for a device.
#
# Prefers pkg-config for the SDL flags; when pkg-config is absent it falls back
# to a homebrew-style $PREFIX (the same approach as test-search-hint.sh) so the
# test still runs. SKIPs (exit 0) only when neither can supply SDL2 + SDL2_image.
set -euo pipefail
cd "$(dirname "$0")/../.."

CC="${CC:-cc}"

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 SDL2_image 2>/dev/null; then
	SDL_CFLAGS="$(pkg-config --cflags sdl2 SDL2_image)"
	SDL_LIBS="$(pkg-config --libs sdl2 SDL2_image)"
else
	PREFIX="${PREFIX:-/opt/homebrew}"
	if [ ! -f "$PREFIX/include/SDL2/SDL.h" ] || [ ! -f "$PREFIX/include/SDL2/SDL_image.h" ]; then
		echo "SKIP: test-scraper-variants (no pkg-config and no SDL2/SDL2_image under $PREFIX)"
		exit 0
	fi
	SDL_CFLAGS="-I$PREFIX/include -I$PREFIX/include/SDL2"
	SDL_LIBS="-L$PREFIX/lib -lSDL2 -lSDL2_image"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

read -ra SDL_CFLAGS_ARR <<<"$SDL_CFLAGS"
read -ra SDL_LIBS_ARR <<<"$SDL_LIBS"

CFLAGS=(-std=gnu99 -O1 -DUSE_SDL2 -DPLATFORM=\"tg5040\" -DHOSTTEST_SDCARD=\"$TMP/sd\"
	-I workspace/all/scraper -I workspace/all/common -I workspace/all/common/ui
	-I scripts/tests/hostplat -I workspace/tg5040/platform -I workspace/tg5040/libmsettings
	"${SDL_CFLAGS_ARR[@]}")

# Device sources: their own known host warnings are silenced (-w), like the
# other host tests do; our test TU is held to -Wall -Wextra -Werror.
for src in \
	workspace/all/scraper/scraper_compositor.c \
	workspace/all/scraper/scraper_paths.c \
	workspace/all/common/utils.c; do
	"$CC" "${CFLAGS[@]}" -w -c -o "$TMP/$(basename "${src%.c}").o" "$src"
done

"$CC" "${CFLAGS[@]}" -Wall -Wextra -Werror -c -o "$TMP/test.o" \
	scripts/tests/scraper-variants/test_variants.c

"$CC" -o "$TMP/test_variants" "$TMP"/*.o "${SDL_LIBS_ARR[@]}"

mkdir -p "$TMP/sd/.userdata/shared"
"$TMP/test_variants" "$TMP/sd/.userdata/shared"

echo "PASS: test-scraper-variants"
