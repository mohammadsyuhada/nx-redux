set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TOOLCHAIN_ROOT /opt/aarch64-nextui-linux-gnu)
set(SYSROOT ${TOOLCHAIN_ROOT}/aarch64-nextui-linux-gnu/sysroot)
set(LIBC_ROOT ${TOOLCHAIN_ROOT}/aarch64-nextui-linux-gnu/libc)

set(CMAKE_C_COMPILER ${TOOLCHAIN_ROOT}/bin/aarch64-nextui-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/bin/aarch64-nextui-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH ${SYSROOT} ${LIBC_ROOT}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# GLES/EGL headers from workspace
include_directories(/root/workspace/all/include)

# aarch64 libpng and zlib from toolchain libc
include_directories(${LIBC_ROOT}/usr/include)
link_directories(${LIBC_ROOT}/usr/lib)

# Set PNG/ZLIB paths for find_package
set(PNG_LIBRARY ${LIBC_ROOT}/usr/lib/libpng.a)
set(PNG_PNG_INCLUDE_DIR ${LIBC_ROOT}/usr/include)
set(ZLIB_LIBRARY ${LIBC_ROOT}/usr/lib/libz.so)
set(ZLIB_INCLUDE_DIR ${LIBC_ROOT}/usr/include)

# Freetype2 for text rendering
set(FREETYPE_INCLUDE_DIRS ${LIBC_ROOT}/usr/include/freetype2)
set(FREETYPE_LIBRARIES ${LIBC_ROOT}/usr/lib/libfreetype.so ${LIBC_ROOT}/usr/lib/libbz2.a)
set(FREETYPE_FOUND TRUE)

# SDL_ttf and SDL_image for overlay menu fonts and button icons
set(SDL_TTF_INCLUDE_DIRS ${LIBC_ROOT}/usr/include/SDL2)
set(SDL_TTF_LIBRARIES ${LIBC_ROOT}/usr/lib/libSDL2_ttf.so)
set(SDL_IMAGE_LIBRARIES ${LIBC_ROOT}/usr/lib/libSDL2_image.so)
