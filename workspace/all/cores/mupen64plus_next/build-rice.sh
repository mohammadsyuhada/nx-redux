#!/bin/sh
# Builds the Rice video plugin into one relocatable object for the
# mupen64plus-next libretro core (called from build-libretro.sh).
# $1 = core checkout, $2 = output object. Rice is upstream
# mupen64plus-video-rice at the commit the standalone N64.pak ships.
set -e
CORE="$1"
OUT="$2"
HERE=$(cd "$(dirname "$0")" && pwd)
RICE_HASH=038882dd2e7cb660fd7512c0745032f17edd0bfa
SRC="$CORE/rice-src"
TC=/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-
SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc

if [ ! -d "$SRC/.git" ]; then
	git clone -q https://github.com/mupen64plus/mupen64plus-video-rice.git "$SRC"
	git -C "$SRC" checkout -q "$RICE_HASH"
fi
if [ ! -f "$SRC/.nx-patched" ]; then
	for p in "$HERE"/rice/patches/*.patch; do
		[ -f "$p" ] || continue
		git -C "$SRC" apply "$p"
	done
	touch "$SRC/.nx-patched"
fi

OBJ="$SRC/nx-obj"
rm -rf "$OBJ" && mkdir -p "$OBJ"
FLAGS="-O3 -fPIC -fvisibility=hidden -mcpu=cortex-a53 -DUSE_GLES -DNO_ASM -D__LIBRETRO__ -DHAVE_OPENGLES -DHAVE_OPENGLES3 -DGLES3 -DNDEBUG -ffast-math -fno-strict-aliasing
	-include $HERE/rice/rice_libretro.h
	-I$SRC/src -I$SRC/src/liblinux -I$CORE/mupen64plus-core/src/api
	-I$CORE/libretro-common/include -I$HERE/../../include
	-I$SYSROOT/usr/include/SDL2 -I$CORE/custom/dependencies/libpng -I$CORE/custom/dependencies/libzlib"
for f in "$SRC"/src/*.cpp "$SRC"/src/liblinux/*.c "$SRC"/src/liblinux/*.cpp "$HERE"/rice/*.cpp; do
	[ -f "$f" ] || continue
	case "$f" in *win32*|*osal_dynamiclib_unix*) continue ;; esac
	o="$OBJ/$(basename "$f").o"
	case "$f" in
	*.c) ${TC}gcc $FLAGS -std=gnu11 -c "$f" -o "$o" ;;
	*) ${TC}g++ $FLAGS -std=gnu++11 -fvisibility-inlines-hidden -c "$f" -o "$o" ;;
	esac
done
${TC}ld -r -o "$OUT" "$OBJ"/*.o
# Only Rice's prefixed plugin entry points stay global; Rice's other strong
# symbols (its own globals named like the core's config functions, helper
# classes) become local so they cannot collide with the core or GLideN64.
# Weak symbols (C++ inline/template COMDAT copies) stay as they are: the
# final link keeps one copy of each, and localizing them leaves references
# into discarded sections.
${TC}readelf -sW "$OUT" | awk '$5 == "GLOBAL" && $7 != "UND" && $8 !~ /^rice/ { print $8 }' > "$OBJ/localize.txt"
${TC}objcopy --localize-symbols="$OBJ/localize.txt" "$OUT"
