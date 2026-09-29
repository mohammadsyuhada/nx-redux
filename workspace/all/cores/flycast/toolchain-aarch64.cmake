# Toolchain for flycast's libretro core on the NextUI aarch64 toolchains (tg5040
# and tg5050 Docker images share the prefix). Unlike the standalone build's
# toolchain file (all/other/flycast), the libretro core needs no curl/OpenSSL,
# so nothing here depends on hand-built prebuilts.
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

# GLES/EGL headers from the workspace (workspace/all/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)
include_directories(${LIBC_ROOT}/usr/include)
link_directories(${LIBC_ROOT}/usr/lib)

set(ZLIB_LIBRARY ${LIBC_ROOT}/usr/lib/libz.so)
set(ZLIB_INCLUDE_DIR ${LIBC_ROOT}/usr/include)
