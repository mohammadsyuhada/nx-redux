#!/bin/bash
#
# Build patched ffplay for NextUI (tg5040/tg5050)
#
# This script runs INSIDE the Docker toolchain container.
# Usage from host:
#   docker run --rm -v .../workspace:/root/workspace \
#     ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c \
#     'source ~/.bashrc && cd /root/workspace/nextui-video-player/ffplay && bash build.sh'
#
set -e
source ~/.bashrc

FFPLAY_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR=/tmp/ffplay-build
FFMPEG_VERSION=6.1
INSTALL=$BUILD_DIR/install
DEPS_DIR=$BUILD_DIR/deps

echo "=== ffplay build for NextUI ==="
echo "Source dir: $FFPLAY_DIR"

# Download FFmpeg source if not cached
if [ ! -f "$BUILD_DIR/configure" ]; then
    echo "=== Downloading FFmpeg $FFMPEG_VERSION ==="
    mkdir -p $BUILD_DIR
    cd /tmp
    wget -q "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" -O ffmpeg.tar.xz
    tar xf ffmpeg.tar.xz -C $BUILD_DIR --strip-components=1
    rm ffmpeg.tar.xz
fi

cd $BUILD_DIR
mkdir -p $INSTALL/include/SDL2 $INSTALL/lib/pkgconfig $DEPS_DIR

# ============================================================
# Build fribidi (static) - required by libass for bidi text
# ============================================================
if [ ! -f "$INSTALL/lib/libfribidi.a" ]; then
    echo "=== Building fribidi ==="
    cd $DEPS_DIR
    if [ ! -d "fribidi-1.0.13" ]; then
        wget -q "https://github.com/fribidi/fribidi/releases/download/v1.0.13/fribidi-1.0.13.tar.xz" -O fribidi.tar.xz
        tar xf fribidi.tar.xz
        rm fribidi.tar.xz
    fi
    cd fribidi-1.0.13
    # fribidi uses meson, but we can use the simple configure-based build
    if [ ! -f Makefile ]; then
        CC=aarch64-nextui-linux-gnu-gcc \
        AR=aarch64-nextui-linux-gnu-ar \
        RANLIB=aarch64-nextui-linux-gnu-ranlib \
        CFLAGS="-O2" \
        ./configure --host=aarch64-linux-gnu --prefix=$INSTALL \
            --enable-static --disable-shared --disable-debug 2>&1 | tail -3
    fi
    make -j$(nproc) 2>&1 | tail -3
    make install 2>&1 | tail -3
    echo "fribidi installed"
fi

# ============================================================
# Build libxml2 (static) - required by DASH demuxer
# ============================================================
if [ ! -f "$INSTALL/lib/libxml2.a" ]; then
    echo "=== Building libxml2 ==="
    cd $DEPS_DIR
    if [ ! -d "libxml2-2.11.9" ]; then
        wget -q "https://download.gnome.org/sources/libxml2/2.11/libxml2-2.11.9.tar.xz" -O libxml2.tar.xz
        tar xf libxml2.tar.xz
        rm libxml2.tar.xz
    fi
    cd libxml2-2.11.9
    if [ ! -f Makefile ]; then
        CC=aarch64-nextui-linux-gnu-gcc \
        AR=aarch64-nextui-linux-gnu-ar \
        RANLIB=aarch64-nextui-linux-gnu-ranlib \
        CFLAGS="-O2" \
        ./configure --host=aarch64-linux-gnu --prefix=$INSTALL \
            --enable-static --disable-shared \
            --without-python --without-lzma --without-zlib \
            --without-iconv --without-icu \
            --without-http --without-ftp \
            2>&1 | tail -3
    fi
    make -j$(nproc) 2>&1 | tail -3
    make install 2>&1 | tail -3
    echo "libxml2 installed"
fi

# ============================================================
# Build libass (static) - subtitle rendering library
# ============================================================
if [ ! -f "$INSTALL/lib/libass.a" ]; then
    echo "=== Building libass ==="

    # Set up device's freetype and fontconfig .so for linking
    cp $FFPLAY_DIR/syslibs/libfreetype.so.6 $INSTALL/lib/
    cd $INSTALL/lib
    ln -sf libfreetype.so.6 libfreetype.so
    cd $DEPS_DIR

    cp $FFPLAY_DIR/syslibs/libfontconfig.so.1 $INSTALL/lib/
    cd $INSTALL/lib
    ln -sf libfontconfig.so.1 libfontconfig.so
    cd $DEPS_DIR

    cp $FFPLAY_DIR/syslibs/libexpat.so.1 $INSTALL/lib/
    cd $INSTALL/lib
    ln -sf libexpat.so.1 libexpat.so
    cd $DEPS_DIR

    cp $FFPLAY_DIR/syslibs/libpng.so.3 $INSTALL/lib/
    cd $INSTALL/lib
    ln -sf libpng.so.3 libpng.so
    cd $DEPS_DIR

    # Download freetype and fontconfig headers
    if [ ! -d "$INSTALL/include/freetype2" ]; then
        echo "=== Downloading freetype headers ==="
        wget -q "https://download.savannah.gnu.org/releases/freetype/freetype-2.11.0.tar.xz" -O freetype.tar.xz
        tar xf freetype.tar.xz
        rm freetype.tar.xz
        cp -r freetype-2.11.0/include/* $INSTALL/include/
        # Create freetype2.pc
        cat > $INSTALL/lib/pkgconfig/freetype2.pc << EOF
prefix=$INSTALL
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: FreeType 2
Description: A free font rendering engine
Version: 23.4.17
Libs: -L\${libdir} -lfreetype
Cflags: -I\${includedir}/freetype2
EOF
    fi

    if [ ! -d "$INSTALL/include/fontconfig" ]; then
        echo "=== Downloading fontconfig headers ==="
        mkdir -p $INSTALL/include/fontconfig
        for hdr in fontconfig.h fcfreetype.h fcprivate.h; do
            wget -q "https://gitlab.freedesktop.org/fontconfig/fontconfig/-/raw/2.13.1/fontconfig/$hdr" \
                -O $INSTALL/include/fontconfig/$hdr
        done
        cat > $INSTALL/lib/pkgconfig/fontconfig.pc << EOF
prefix=$INSTALL
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: Fontconfig
Description: Font configuration and customization library
Version: 2.13.1
Libs: -L\${libdir} -lfontconfig
Cflags: -I\${includedir}
EOF
    fi

    # Download and build libass (0.14.0 - supports --disable-harfbuzz)
    if [ ! -d "libass-0.14.0" ]; then
        wget -q "https://github.com/libass/libass/releases/download/0.14.0/libass-0.14.0.tar.xz" -O libass.tar.xz
        tar xf libass.tar.xz
        rm libass.tar.xz
    fi
    cd libass-0.14.0

    export PKG_CONFIG_PATH=""
    export PKG_CONFIG_LIBDIR=$INSTALL/lib/pkgconfig
    export PKG_CONFIG_SYSROOT_DIR=""

    if [ ! -f Makefile ]; then
        CC=aarch64-nextui-linux-gnu-gcc \
        AR=aarch64-nextui-linux-gnu-ar \
        RANLIB=aarch64-nextui-linux-gnu-ranlib \
        CFLAGS="-O2 -I${INSTALL}/include -I${INSTALL}/include/freetype2" \
        LDFLAGS="-L${INSTALL}/lib" \
        FREETYPE_CFLAGS="-I${INSTALL}/include/freetype2 -I${INSTALL}/include" \
        FREETYPE_LIBS="-L${INSTALL}/lib -lfreetype" \
        FONTCONFIG_CFLAGS="-I${INSTALL}/include" \
        FONTCONFIG_LIBS="-L${INSTALL}/lib -lfontconfig" \
        FRIBIDI_CFLAGS="-I${INSTALL}/include/fribidi" \
        FRIBIDI_LIBS="-L${INSTALL}/lib -lfribidi" \
        ./configure --host=aarch64-linux-gnu --prefix=$INSTALL \
            --enable-static --disable-shared \
            --disable-harfbuzz \
            --disable-asm \
            2>&1 | tail -10
    fi
    make -j$(nproc) 2>&1 | tail -5
    make install 2>&1 | tail -5
    echo "libass installed"
fi

cd $BUILD_DIR

# ============================================================
# Set up device shared libs (always, even if deps cached)
# ============================================================
cp $FFPLAY_DIR/syslibs/libfreetype.so.6 $INSTALL/lib/
ln -sf libfreetype.so.6 $INSTALL/lib/libfreetype.so
cp $FFPLAY_DIR/syslibs/libfontconfig.so.1 $INSTALL/lib/
ln -sf libfontconfig.so.1 $INSTALL/lib/libfontconfig.so
cp $FFPLAY_DIR/syslibs/libexpat.so.1 $INSTALL/lib/
ln -sf libexpat.so.1 $INSTALL/lib/libexpat.so
cp $FFPLAY_DIR/syslibs/libpng.so.3 $INSTALL/lib/
ln -sf libpng.so.3 $INSTALL/lib/libpng.so

# ============================================================
# Set up SDL2
# ============================================================
echo "=== Setting up SDL2 ==="
cp $FFPLAY_DIR/sdl2-headers/*.h $INSTALL/include/SDL2/

# Remove any static SDL2 so linker uses shared
rm -f $INSTALL/lib/libSDL2.a $INSTALL/lib/libSDL2main.a $INSTALL/lib/libSDL2_test.a

# Copy device's SDL2 .so and create symlink
cp $FFPLAY_DIR/syslibs/libSDL2-2.0.so.0 $INSTALL/lib/
cd $INSTALL/lib
ln -sf libSDL2-2.0.so.0 libSDL2.so
cd $BUILD_DIR

# Write pkg-config for SDL2
cat > $INSTALL/lib/pkgconfig/sdl2.pc << EOF
prefix=$INSTALL
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: sdl2
Description: SDL2
Version: 2.30.8
Libs: -L\${libdir} -lSDL2 -lpthread
Cflags: -I\${includedir} -I\${includedir}/SDL2 -D_REENTRANT
EOF

export PKG_CONFIG_PATH=""
export PKG_CONFIG_LIBDIR=$INSTALL/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=""

# Ensure all pkg-config files exist for FFmpeg configure
# (these may already exist from the libass build step, but recreate to be safe)
cat > $INSTALL/lib/pkgconfig/freetype2.pc << EOF
prefix=$INSTALL
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: FreeType 2
Description: A free font rendering engine
Version: 23.4.17
Libs: -L\${libdir} -lfreetype
Cflags: -I\${includedir}/freetype2
EOF

cat > $INSTALL/lib/pkgconfig/fontconfig.pc << EOF
prefix=$INSTALL
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: Fontconfig
Description: Font configuration and customization library
Version: 2.13.1
Libs: -L\${libdir} -lfontconfig
Cflags: -I\${includedir}
EOF

# Copy our patched ffplay.c over the original
echo "=== Applying patched ffplay.c ==="
cp $FFPLAY_DIR/ffplay.c $BUILD_DIR/fftools/ffplay.c

# Configure (force reconfigure to pick up libass)
if [ ! -f "$BUILD_DIR/config.h" ] || ! grep -q "CONFIG_LIBASS 1" "$BUILD_DIR/config.h" || ! grep -q "CONFIG_LIBXML2 1" "$BUILD_DIR/config.h"; then
    echo "=== Configuring FFmpeg (with libass + libxml2/DASH support) ==="
    make distclean 2>/dev/null || true

    ./configure \
        --arch=aarch64 \
        --target-os=linux \
        --cross-prefix=aarch64-nextui-linux-gnu- \
        --enable-cross-compile \
        --prefix=$INSTALL \
        --pkg-config=pkg-config \
        --enable-ffplay \
        --enable-sdl2 \
        --disable-ffmpeg \
        --disable-ffprobe \
        --disable-doc \
        --enable-static \
        --disable-shared \
        --enable-gpl \
        --enable-nonfree \
        --enable-libass \
        --enable-libfreetype \
        --enable-libfontconfig \
        --enable-libfribidi \
        --enable-network \
        --enable-openssl \
        --enable-libxml2 \
        --disable-devices \
        --disable-encoders \
        --disable-muxers \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-decoder=mpeg4 \
        --enable-decoder=mpeg2video \
        --enable-decoder=vp8 \
        --enable-decoder=vp9 \
        --enable-decoder=aac \
        --enable-decoder=aac_latm \
        --enable-decoder=mp3 \
        --enable-decoder=mp3float \
        --enable-decoder=opus \
        --enable-decoder=vorbis \
        --enable-decoder=flac \
        --enable-decoder=pcm_s16le \
        --enable-decoder=pcm_s16be \
        --enable-decoder=ac3 \
        --enable-decoder=eac3 \
        --enable-decoder=srt \
        --enable-decoder=ass \
        --enable-decoder=ssa \
        --enable-decoder=subrip \
        --enable-decoder=pgssub \
        --enable-demuxer=mov \
        --enable-demuxer=matroska \
        --enable-demuxer=avi \
        --enable-demuxer=mpegts \
        --enable-demuxer=mpegps \
        --enable-demuxer=flv \
        --enable-demuxer=mp3 \
        --enable-demuxer=aac \
        --enable-demuxer=ogg \
        --enable-demuxer=wav \
        --enable-demuxer=srt \
        --enable-demuxer=ass \
        --enable-demuxer=concat \
        --enable-demuxer=hls \
        --enable-demuxer=dash \
        --enable-parser=h264 \
        --enable-parser=hevc \
        --enable-parser=mpeg4video \
        --enable-parser=mpegaudio \
        --enable-parser=mpegvideo \
        --enable-parser=aac \
        --enable-parser=aac_latm \
        --enable-parser=vp8 \
        --enable-parser=vp9 \
        --enable-parser=opus \
        --enable-parser=vorbis \
        --enable-parser=flac \
        --enable-parser=ac3 \
        --enable-protocol=file \
        --enable-protocol=pipe \
        --enable-protocol=http \
        --enable-protocol=https \
        --enable-protocol=hls \
        --enable-protocol=tcp \
        --enable-protocol=tls \
        --enable-protocol=crypto \
        --enable-filter=scale \
        --enable-filter=aresample \
        --enable-filter=aformat \
        --enable-filter=format \
        --enable-filter=null \
        --enable-filter=anull \
        --enable-filter=volume \
        --enable-filter=subtitles \
        --enable-filter=overlay \
        --extra-cflags="-I${INSTALL}/include -I${INSTALL}/include/SDL2 -I${INSTALL}/include/freetype2" \
        --extra-ldflags="-L${INSTALL}/lib -Wl,--unresolved-symbols=ignore-in-shared-libs -Wl,-rpath,/rom/usr/trimui/lib -Wl,-rpath,/usr/lib" \
        --extra-libs="-lass -lfribidi -lfreetype -lfontconfig -lexpat -lxml2 -lm -ldl -lpthread -lrt" \
        2>&1 | tail -10

    echo "SDL2 enabled: $(grep CONFIG_SDL2 config.h 2>/dev/null)"
    echo "LIBASS enabled: $(grep CONFIG_LIBASS config.h 2>/dev/null)"
    echo "LIBXML2 enabled: $(grep CONFIG_LIBXML2 config.h 2>/dev/null)"
    echo "DASH demuxer: $(grep CONFIG_DASH_DEMUXER config.h 2>/dev/null)"
fi

# Build
echo "=== Building ffplay ==="
make ffplay -j$(nproc) 2>&1 | tail -10

# Copy result back to project bin/
echo "=== Copying ffplay binary ==="
cp $BUILD_DIR/ffplay $FFPLAY_DIR/../bin/ffplay

echo "=== Done ==="
ls -la $FFPLAY_DIR/../bin/ffplay
file $FFPLAY_DIR/../bin/ffplay
aarch64-nextui-linux-gnu-readelf -d $BUILD_DIR/ffplay 2>/dev/null | grep -E "NEEDED|RPATH|RUNPATH"
