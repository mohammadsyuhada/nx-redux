#!/bin/sh
# Builds flycast's libretro core for the cores Makefile (flycast_MAKE). Run from
# the flycast checkout; $1 is the platform, the cores template appends -jN.
# Leaves flycast_libretro.so in the checkout root, where the template collects it.
set -e
PLAT="$1"
JOBS="${2:--j4}"
HERE=$(cd "$(dirname "$0")" && pwd)
# The template initialises flycast_SUBMODULES non-recursively; tinygettext
# carries its own submodule (external/tinycmmc), so finish it here.
git submodule update --init --recursive core/deps/tinygettext
# tg5040's GCC 8.3 keeps std::filesystem (used by v2.7's tinygettext) in a
# separate libstdc++fs; CMAKE_CXX_STANDARD_LIBRARIES lands at the end of the
# link line, after the static libs that need it. GCC 10 (tg5050) has it built in.
EXTRA=""
[ "$PLAT" = tg5040 ] && EXTRA="-DCMAKE_CXX_STANDARD_LIBRARIES=-lstdc++fs"
cmake -S . -B build-libretro-"$PLAT" $EXTRA \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/toolchain-aarch64.cmake" \
	-DLIBRETRO=ON -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_OPENMP=OFF
cmake --build build-libretro-"$PLAT" "$JOBS"
/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-strip -o flycast_libretro.so build-libretro-"$PLAT"/flycast_libretro.so
