#!/bin/sh
# SPIKE (minarch-gpu-spike): build flycast's libretro core for tg5040 from the
# same commit as the standalone DC.pak build, in a separate clean worktree of
# the flycast checkout. Only flycast.patch's core hunks are applied:
# sh4_interrupts.cpp (tg5040 GCC 8.3.0 ICEs without it) and the two naomi
# files (modern awbios dump); the standalone/overlay hunks are not.
set -e
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
	ghcr.io/loveretro/tg5040-toolchain:latest bash -c '
	cmake -B build-tg5040 -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=/root/workspace/all/other/flycast/toolchain-aarch64.cmake \
		-DFLYCAST_TOOLCHAIN_PLATFORM=tg5040 \
		-DLIBRETRO=ON -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_OPENMP=OFF &&
	cmake --build build-tg5040 -j$(nproc) &&
	/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-strip build-tg5040/flycast_libretro.so'
