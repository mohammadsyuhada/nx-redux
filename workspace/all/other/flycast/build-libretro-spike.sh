#!/bin/sh
# SPIKE (minarch-gpu-spike): build flycast's libretro core for tg5040/tg5050
# (arg 1, default tg5040) from the
# same commit as the standalone DC.pak build, in a separate clean worktree of
# the flycast checkout. Only flycast.patch's core hunks are applied:
# sh4_interrupts.cpp (tg5040 GCC 8.3.0 ICEs without it) and the two naomi
# files (modern awbios dump); the standalone/overlay hunks are not.
set -e
PLAT=${1:-tg5040}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/flycast-libretro"
COMMIT=392a429e8
if [ ! -d "$SRC" ]; then
	git -C "$HERE/flycast" worktree add --detach "$SRC" "$COMMIT"
	git -C "$SRC" submodule update --init --recursive
	git -C "$SRC" apply --include='core/hw/sh4/sh4_interrupts.cpp' \
		--include='core/hw/naomi/naomi_roms.cpp' --include='core/hw/naomi/naomi_cart.cpp' "$HERE/flycast.patch"
fi
WS=$(cd "$HERE/../../.." && pwd)
docker run --rm -v "$WS":/root/workspace -w /root/workspace/all/other/flycast/flycast-libretro \
	ghcr.io/loveretro/$PLAT-toolchain:latest bash -c "
	cmake -B build-$PLAT -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=/root/workspace/all/other/flycast/toolchain-aarch64.cmake \
		-DFLYCAST_TOOLCHAIN_PLATFORM=$PLAT \
		-DLIBRETRO=ON -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_OPENMP=OFF &&
	cmake --build build-$PLAT -j\$(nproc) &&
	/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-strip build-$PLAT/flycast_libretro.so"
