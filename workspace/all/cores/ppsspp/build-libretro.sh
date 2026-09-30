#!/bin/sh
# Builds PPSSPP's libretro core for the cores Makefile (ppsspp_MAKE). Run from
# the ppsspp checkout; $1 is the platform, the cores template appends -jN.
# Leaves ppsspp_libretro.so in the checkout root (collected by the template) and
# stages the runtime assets in $NX_CORES_OUTPUT/ppsspp-assets/PPSSPP, next to the
# collected core: the release workflow caches, uploads and packages only the cores
# output dir, never the checkout, so the assets have to live there too.
set -e
PLAT="$1"
JOBS="${2:--j4}"
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=build-libretro-"$PLAT"
if [ "$JOBS" = clean ]; then
	# clean-ppsspp: the template calls "$ppsspp_MAKE clean"
	rm -rf "$BUILD" ppsspp_libretro.so
	[ -n "${NX_CORES_OUTPUT:-}" ] && rm -rf "$NX_CORES_OUTPUT/ppsspp-assets"
	exit 0
fi
TC=/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu
EXTRA=""
if [ "$PLAT" = tg5040 ]; then
	# GCC 8.3: std::filesystem lives in libstdc++fs, and libgcc lacks the
	# outline-atomics helpers the prebuilt ffmpeg needs (see outline_atomics.c)
	mkdir -p "$BUILD"
	$TC-gcc -O2 -fPIC -c "$HERE/outline_atomics.c" -o "$BUILD/outline_atomics.o"
	EXTRA="-DCMAKE_CXX_STANDARD_LIBRARIES=-lstdc++fs -DCMAKE_SHARED_LINKER_FLAGS=$PWD/$BUILD/outline_atomics.o"
fi
cmake -S . -B "$BUILD" $EXTRA \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/../flycast/toolchain-aarch64.cmake" \
	-DLIBRETRO=ON -DUSING_GLES2=ON -DUSING_EGL=OFF -DVULKAN=OFF \
	-DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
	-DUSE_SYSTEM_FFMPEG=OFF -DUSE_SYSTEM_LIBZIP=OFF -DUSE_SYSTEM_LIBPNG=OFF \
	-DUSE_SYSTEM_ZSTD=OFF -DUSE_SYSTEM_SNAPPY=OFF -DUSE_CCACHE=OFF -DHEADLESS=OFF -DUNITTEST=OFF
cmake --build "$BUILD" --target ppsspp_libretro "$JOBS"
$TC-strip -o ppsspp_libretro.so "$BUILD"/lib/ppsspp_libretro.so
# Runtime assets minus frontend-UI-only content (no libretro code path reads them)
OUT="${NX_CORES_OUTPUT:?NX_CORES_OUTPUT (the cores output dir) is required}/ppsspp-assets"
rm -rf "$OUT" && mkdir -p "$OUT/PPSSPP"
cp -R assets/. "$OUT/PPSSPP/"
cd "$OUT/PPSSPP"
rm -rf debugger upload themes ui_images mime sfx_*.wav gamecontrollerdb.txt
