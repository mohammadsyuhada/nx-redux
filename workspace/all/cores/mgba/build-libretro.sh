#!/bin/sh
# Builds mGBA's libretro core for the cores Makefile (mgba_MAKE). Run from the
# mgba checkout; $1 is the platform, the cores template appends -jN. Upstream
# libretro/mgba dropped Makefile.libretro for CMake (8940477), so this replaces
# the old platform patch: the CPU flags it carried are passed here.
# Leaves mgba_libretro.so in the checkout root (collected by the template).
# NX_SIOLS_DIR (the shared lockstep state machine, compiled in by
# all/cores/patches/mgba/001) comes from the cores Makefile's environment.
set -e
PLAT="$1"
JOBS="${2:--j4}"
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=build-libretro-"$PLAT"
if [ "$JOBS" = clean ]; then
	# clean-mgba: the template calls "$mgba_MAKE clean"
	rm -rf "$BUILD" mgba_libretro.so
	exit 0
fi
TC=/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu
case "$PLAT" in
tg5040) CPU="-mtune=cortex-a53 -mcpu=cortex-a53 -march=armv8-a" ;;
tg5050) CPU="-mcpu=cortex-a55" ;;
*) echo "build-libretro.sh: unknown platform $PLAT" >&2 && exit 1 ;;
esac
# The flags the old Makefile.libretro platform block used (upstream pins -O3
# for the libretro target itself)
CFLAGS="$CPU -fomit-frame-pointer -ffast-math -fno-common -ftree-vectorize -funswitch-loops"
cmake -S . -B "$BUILD" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/../flycast/toolchain-aarch64.cmake" \
	-DCMAKE_C_FLAGS="$CFLAGS" \
	-DLIBMGBA_ONLY=ON -DBUILD_LIBRETRO=ON \
	-DNX_SIOLS_DIR="${NX_SIOLS_DIR:?NX_SIOLS_DIR (all/cores/sio_lockstep) is required}"
cmake --build "$BUILD" --target mgba_libretro "$JOBS"
$TC-strip -o mgba_libretro.so "$BUILD"/mgba_libretro.so
