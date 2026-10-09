#!/bin/sh
# Host-side download of what build-libretro.sh needs but the toolchain image
# cannot fetch (it has no curl): the SDL2_net source for core netplay.
# $1 = platform. Run before `make build-core CORE=mupen64plus_next`
# (CI's build-core job runs it automatically).
set -e
PLAT="$1"
HERE=$(cd "$(dirname "$0")" && pwd)
DEPS="$HERE/../../../$PLAT/cores/src/_deps"
TARBALL="$DEPS/SDL2_net-2.2.0.tar.gz"
SHA256=4e4a891988316271974ff4e9585ed1ef729a123d22c08bd473129179dc857feb
mkdir -p "$DEPS"
if [ ! -f "$TARBALL" ]; then
	curl -fsSL -o "$TARBALL.tmp" https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
	mv "$TARBALL.tmp" "$TARBALL"
fi
echo "$SHA256  $TARBALL" | sha256sum -c - 2>/dev/null || echo "$SHA256  $TARBALL" | shasum -a 256 -c -
