#!/bin/sh
# Builds mupen64plus-next (libretro) for the cores Makefile (mupen64plus_next_MAKE).
# Run from the checkout; $1 is the platform, the cores template appends -jN.
# GLES3 + GLideN64 + new aarch64 dynarec. Leaves the .so in the checkout root.
#
# Core netplay (patch 0002) needs SDL2_net, which the toolchain images do not
# all ship (tg5040 has none). SDL2_net 2.2.0 is built static + PIC inside
# this container run and linked into the .so, so no new runtime library ships.
# The image has no curl: the tarball must already be at $NX_SDL2_NET_TARBALL
# (default: the platform's cores/src/_deps/; fetch-deps.sh downloads it on the
# host). NX_NETPLAY=0 skips it.
set -e
PLAT="$1"
JOBS="${2:--j4}"
HERE=$(cd "$(dirname "$0")" && pwd)
INC="$HERE/../../include"
if [ "$JOBS" = clean ]; then
	make platform=arm64_cortex_a53_gles3 clean
	exit 0
fi
# tg5040 (PowerVR) ships libEGL; tg5050 (Mali) has the EGL symbols in libmali.so.0
# only (the device also has a libEGL.so, but the toolchain sysroot does not).
EGL_LIB=-lEGL
[ "$PLAT" = tg5050 ] && EGL_LIB=-lmali

NX_NETPLAY="${NX_NETPLAY:-1}"
NET_CPPFLAGS=""
NET_LDFLAGS=""
if [ "$NX_NETPLAY" = 1 ]; then
	TARBALL="${NX_SDL2_NET_TARBALL:-$HERE/../../../$PLAT/cores/src/_deps/SDL2_net-2.2.0.tar.gz}"
	[ -f "$TARBALL" ] || { echo "missing $TARBALL: run workspace/all/cores/mupen64plus_next/fetch-deps.sh $PLAT on the host" >&2; exit 1; }
	SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
	NETDIR=/tmp/sdl2net-build
	rm -rf "$NETDIR" && mkdir -p "$NETDIR" && tar xzf "$TARBALL" -C "$NETDIR"
	(cd "$NETDIR"/SDL2_net-2.2.0 &&
		./configure --host=aarch64-nextui-linux-gnu --prefix="$NETDIR/prefix" \
			--disable-shared --enable-static --with-pic \
			CC=/opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-gcc \
			SDL_CFLAGS="-I$SYSROOT/usr/include/SDL2 -D_REENTRANT" SDL_LIBS="-lSDL2" >/dev/null &&
		make -j4 >/dev/null && make install >/dev/null)
	# <SDL2/SDL_net.h> from the prefix, "SDL.h" from the sysroot
	NET_CPPFLAGS="-I$NETDIR/prefix/include -I$SYSROOT/usr/include/SDL2"
	NET_LDFLAGS="$NETDIR/prefix/lib/libSDL2_net.a -lSDL2"
fi

# Rice video plugin (patch 0003): one relocatable object linked into the
# core. NX_RICE=0 builds GLideN64 only.
NX_RICE="${NX_RICE:-1}"
RICE_ARGS=""
if [ "$NX_RICE" = 1 ]; then
	sh "$HERE/build-rice.sh" "$(pwd)" "$(pwd)/rice.o"
	RICE_ARGS="HAVE_RICE=1 RICE_OBJ=$(pwd)/rice.o"
fi
# The licence that ships beside the core (mupen64plus_next_LICENSE): the
# core's own, plus Rice's when it is built in.
cp LICENSE NX-LICENSES.txt
if [ "$NX_RICE" = 1 ]; then
	printf '\n==== mupen64plus-video-rice (built in) ====\nSource: https://github.com/mupen64plus/mupen64plus-video-rice\nCommit: %s\n\n' \
		"$(git -C rice-src rev-parse HEAD)" >> NX-LICENSES.txt
	cat rice-src/LICENSES >> NX-LICENSES.txt
fi

# GLES3/EGL/KHR headers from workspace/all/include (tg5050's sysroot only has
# GLES2). Passed through the environment: a command-line INCFLAGS would replace
# the Makefile's own include list.
CPPFLAGS="-I$INC $NET_CPPFLAGS" LDFLAGS="$NET_LDFLAGS" \
	make platform=arm64_cortex_a53_gles3 ARCH=aarch64 NX_NETPLAY="$NX_NETPLAY" \
	EGL_LIB="$EGL_LIB" $RICE_ARGS NX_SRC_DIR="$HERE/nx" "$JOBS"
[ -n "$NX_NOSTRIP" ] || /opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-strip mupen64plus_next_libretro.so
