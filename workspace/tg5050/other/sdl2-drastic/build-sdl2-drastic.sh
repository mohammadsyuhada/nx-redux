#!/bin/bash
set -ex

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXTRA_PREFIX="$(pwd)/extra-deps"

# ============================================================
# Step 1: Install missing headers and create pkg-config files
# ============================================================
if [ ! -d "$EXTRA_PREFIX/include/EGL" ]; then
    mkdir -p "$EXTRA_PREFIX/include" "$EXTRA_PREFIX/lib" "$EXTRA_PREFIX/lib/pkgconfig"

    # EGL/KHR headers (standard Khronos headers)
    git clone --depth 1 https://github.com/KhronosGroup/EGL-Registry.git /tmp/egl-registry
    cp -r /tmp/egl-registry/api/EGL "$EXTRA_PREFIX/include/"
    cp -r /tmp/egl-registry/api/KHR "$EXTRA_PREFIX/include/"
    rm -rf /tmp/egl-registry

    # GBM header
    wget -qO "$EXTRA_PREFIX/include/gbm.h" \
        "https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/src/gbm/main/gbm.h"

    # libdrm headers
    git clone --depth 1 https://gitlab.freedesktop.org/mesa/drm.git /tmp/libdrm
    mkdir -p "$EXTRA_PREFIX/include/libdrm"
    cp /tmp/libdrm/include/drm/*.h "$EXTRA_PREFIX/include/libdrm/"
    cp /tmp/libdrm/include/drm/*.h "$EXTRA_PREFIX/include/"
    cp /tmp/libdrm/xf86drm.h /tmp/libdrm/xf86drmMode.h "$EXTRA_PREFIX/include/"
    rm -rf /tmp/libdrm

    # json-c headers
    git clone --depth 1 --branch json-c-0.15 https://github.com/json-c/json-c.git /tmp/json-c
    mkdir -p "$EXTRA_PREFIX/include/json-c"
    cp /tmp/json-c/*.h "$EXTRA_PREFIX/include/json-c/"
    rm -rf /tmp/json-c

    # SDL2_ttf and SDL2_image headers (needed by hook code in drastic_video.h)
    # Copy from sysroot — these are stable headers across SDL2 minor versions
    cp ${SYSROOT}/usr/include/SDL2/SDL_ttf.h "$EXTRA_PREFIX/include/"
    cp ${SYSROOT}/usr/include/SDL2/SDL_image.h "$EXTRA_PREFIX/include/"

    # Create stub libraries for EGL and GBM (they forward to libmali on device)
    ${CC} -shared -o "$EXTRA_PREFIX/lib/libEGL.so" -Wl,--no-as-needed -L${SYSROOT}/usr/lib -lmali
    ${CC} -shared -o "$EXTRA_PREFIX/lib/libgbm.so" -Wl,--no-as-needed -L${SYSROOT}/usr/lib -lmali

    # json-c stub
    cat > /tmp/json_stub.c << 'STUBEOF'
void json_object_new_int(void){} void json_object_put(void){} void json_object_to_file_ext(void){}
void json_object_from_file(void){} void json_object_object_add(void){} void json_object_get_string(void){}
void json_object_get_int(void){} void json_object_object_get_ex(void){} void json_object_new_string(void){}
void json_object_new_object(void){} void json_object_new_array(void){} void json_object_array_add(void){}
void json_object_array_length(void){} void json_object_array_get_idx(void){}
STUBEOF
    ${CC} -shared -o "$EXTRA_PREFIX/lib/libjson-c.so" /tmp/json_stub.c

    # pkg-config for libdrm (so configure detects KMSDRM)
    cat > "$EXTRA_PREFIX/lib/pkgconfig/libdrm.pc" << EOF
prefix=${EXTRA_PREFIX}
libdir=${SYSROOT}/usr/lib
includedir=${EXTRA_PREFIX}/include

Name: libdrm
Description: Direct Rendering Manager library
Version: 2.4.114
Libs: -L\${libdir} -ldrm
Cflags: -I\${includedir} -I\${includedir}/libdrm
EOF

    # pkg-config for gbm
    cat > "$EXTRA_PREFIX/lib/pkgconfig/gbm.pc" << EOF
prefix=${EXTRA_PREFIX}
libdir=${EXTRA_PREFIX}/lib
includedir=${EXTRA_PREFIX}/include

Name: gbm
Description: Generic Buffer Management
Version: 23.0.0
Libs: -L\${libdir} -lgbm
Cflags: -I\${includedir}
EOF

    # pkg-config for egl
    cat > "$EXTRA_PREFIX/lib/pkgconfig/egl.pc" << EOF
prefix=${EXTRA_PREFIX}
libdir=${EXTRA_PREFIX}/lib
includedir=${EXTRA_PREFIX}/include

Name: egl
Description: EGL library
Version: 1.5
Libs: -L\${libdir} -lEGL
Cflags: -I\${includedir}
EOF

    echo "=== Extra deps installed ==="
fi

# ============================================================
# Step 2: SDL_drastic source must be pre-cloned and patched
# Run 'make other/sdl2-drastic/SDL_drastic' from workspace/tg5050
# ============================================================
if [ ! -d SDL_drastic ]; then
    echo "ERROR: SDL_drastic source not found. Run from workspace/tg5050:"
    echo "  make other/sdl2-drastic/SDL_drastic"
    exit 1
fi

cd SDL_drastic

# ============================================================
# Step 3: Generate configure
# ============================================================
if [ ! -f configure ]; then
    ./autogen.sh
fi

# Clean previous build (force rebuild when CFLAGS change)
[ -f Makefile ] && make clean || true
rm -f .configured

# Fix broken .la files in sysroot (hardcoded paths from Trimui build env)
BROKEN_PREFIX="/home2/trimuidev/tg5050/tina_1.0.0/out/a523/gb1/buildroot/buildroot/host/bin/../aarch64-buildroot-linux-gnu/sysroot"
for la in ${SYSROOT}/usr/lib/*.la; do
    if grep -q "$BROKEN_PREFIX" "$la" 2>/dev/null; then
        sed -i "s|$BROKEN_PREFIX|$SYSROOT|g" "$la"
    fi
done

# ============================================================
# Step 5: Configure — KMSDRM + OpenGL ES
# Note: Do NOT include sysroot SDL2 headers (version mismatch).
# The hook code's SDL2 includes resolve from the source tree.
# ============================================================
export PKG_CONFIG_PATH="${EXTRA_PREFIX}/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""

./configure \
    --host=${CROSS_TRIPLE} \
    --prefix=/usr \
    --enable-shared \
    --disable-static \
    --disable-rpath \
    --disable-arts \
    --disable-esd \
    --disable-dbus \
    --disable-pulseaudio \
    --disable-video-vivante \
    --disable-video-cocoa \
    --disable-video-metal \
    --enable-video-dummy \
    --disable-video-offscreen \
    --disable-video-x11 \
    --disable-video-wayland \
    --disable-ime \
    --disable-ibus \
    --disable-fcitx \
    --disable-joystick-mfi \
    --disable-directx \
    --disable-xinput \
    --disable-wasapi \
    --disable-hidapi-joystick \
    --disable-hidapi-libusb \
    --disable-joystick-virtual \
    --disable-render-d3d \
    --disable-hidapi \
    --enable-video-kmsdrm \
    --enable-video-opengles \
    --enable-video-opengles1 \
    --enable-video-opengles2 \
    --enable-alsa \
    --enable-libudev \
    CFLAGS="-O2 -Wno-error -DDEVICE_TRIMUI -DADVDRASTIC_DRM -I${EXTRA_PREFIX}/include" \
    CPPFLAGS="-I${EXTRA_PREFIX}/include" \
    LDFLAGS="-L${EXTRA_PREFIX}/lib -L${SYSROOT}/usr/lib"

# Verify KMSDRM was detected
if ! grep -q "SDL_VIDEO_DRIVER_KMSDRM" include/SDL_config.h; then
    echo "ERROR: KMSDRM video driver was not enabled!"
    exit 1
fi
echo "=== KMSDRM detected ==="

# Suppress -Werror flags (hook code has many harmless warnings)
sed -i 's/-Werror=declaration-after-statement/-Wno-error/g' Makefile

# ============================================================
# Step 6: Build — add hook link deps via EXTRA_LDFLAGS
# ============================================================
make -j$(nproc) \
    EXTRA_LDFLAGS="-L${EXTRA_PREFIX}/lib -L${SYSROOT}/usr/lib -lSDL2_image -lSDL2_ttf -ljson-c -lpthread -lEGL -lGLESv2 -ldrm -lgbm"

# ============================================================
# Step 7: Output
# ============================================================
mkdir -p "$SCRIPT_DIR/output"
cp build/.libs/libSDL2-2.0.so.0 "$SCRIPT_DIR/output/"
echo "=== Build complete (ADVDRASTIC_DRM enabled) ==="
ls -la "$SCRIPT_DIR/output/libSDL2-2.0.so.0"
file "$SCRIPT_DIR/output/libSDL2-2.0.so.0"
strings "$SCRIPT_DIR/output/libSDL2-2.0.so.0" | grep -i "drm_init\|gbm_create\|Cannot create gbm"
