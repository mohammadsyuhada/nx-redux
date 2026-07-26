set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Forward our custom platform-selector cache var into the internal
# try_compile() sub-builds CMake uses for compiler ID/ABI detection - without
# this, those sub-builds re-run this toolchain file without seeing the -D on
# the real command line, tripping the FATAL_ERROR guard below.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FLYCAST_TOOLCHAIN_PLATFORM)

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
include_directories(${LIBC_ROOT}/usr/include)
link_directories(${LIBC_ROOT}/usr/lib)

# SDL2 CONFIG-mode discovery hint: see cmake-hints/lib/cmake/SDL2/SDL2Config.cmake
# for why this is needed (-DUSE_HOST_SDL=ON alone silently falls back to the
# vendored core/deps/SDL static build otherwise).
list(APPEND CMAKE_PREFIX_PATH /root/workspace/all/other/flycast/cmake-hints)

set(ZLIB_LIBRARY ${LIBC_ROOT}/usr/lib/libz.so)
set(ZLIB_INCLUDE_DIR ${LIBC_ROOT}/usr/include)

# libcurl: the toolchain sysroot ships no libcurl at all (headers or library),
# and flycast's CMakeLists.txt hard-requires it (find_package(CURL REQUIRED))
# for any UNIX-not-APPLE build with no option to disable/vendor it. Cross-built
# from upstream curl 8.9.1 as a shared lib (see README "Build" section for the
# exact recipe) and installed next to the flycast checkout (gitignored, like
# the build-* dirs). Must be rebuilt manually before a clean-checkout build.
#
# Built PER PLATFORM, not shared: the two toolchain images (different GCC
# versions, 8.3.0 vs 10.3.0) are not ABI-interchangeable for this, and each
# has its own curl-prebuilt-aarch64-<platform>/ dir. Both are SSL-enabled:
# tg5040's sysroot ships a complete OpenSSL 1.1.1 (libssl + libcrypto), so
# curl links against that directly; tg5050's sysroot ships OpenSSL *headers*
# and libcrypto but NO libssl anywhere (not `.so`, not `.a`) so OpenSSL 1.1.1w
# is cross-built from source too (see README "Build" step 0a) and curl links
# against that prebuilt copy instead of the (incomplete) sysroot one.
#
# Which prebuilt dir to use is selected via the FLYCAST_TOOLCHAIN_PLATFORM
# cache variable (-DFLYCAST_TOOLCHAIN_PLATFORM=tg5040|tg5050 on the cmake
# command line) rather than sniffing the sysroot for a marker file/lib -
# explicit and fails loudly if forgotten, instead of silently picking the
# wrong platform's prebuilt (which would configure fine but fail to link,
# since the two curl builds are not interchangeable).
if(NOT DEFINED FLYCAST_TOOLCHAIN_PLATFORM)
	message(FATAL_ERROR "FLYCAST_TOOLCHAIN_PLATFORM must be set (-DFLYCAST_TOOLCHAIN_PLATFORM=tg5040 or tg5050) to select the matching prebuilt curl/OpenSSL - see README \"Build\" section.")
endif()
if(NOT FLYCAST_TOOLCHAIN_PLATFORM STREQUAL "tg5040" AND NOT FLYCAST_TOOLCHAIN_PLATFORM STREQUAL "tg5050")
	message(FATAL_ERROR "FLYCAST_TOOLCHAIN_PLATFORM must be exactly 'tg5040' or 'tg5050', got '${FLYCAST_TOOLCHAIN_PLATFORM}'")
endif()
set(CURL_PREBUILT_ROOT /root/workspace/all/other/flycast/curl-prebuilt-aarch64-${FLYCAST_TOOLCHAIN_PLATFORM})
set(CURL_INCLUDE_DIR ${CURL_PREBUILT_ROOT}/include)
set(CURL_LIBRARY ${CURL_PREBUILT_ROOT}/lib/libcurl.so)

# tg5050 only: our prebuilt libcurl.so links against a self-built OpenSSL
# 1.1.1w (see README "Build" step 0a - the sysroot's own OpenSSL is missing
# libssl entirely) rather than anything in the sysroot, and libcurl.so itself
# carries no RPATH pointing at it. This toolchain's `ld` needs to actually
# locate libssl.so.1.1/libcrypto.so.1.1 to validate libcurl.so's own
# transitive OPENSSL_1_1_* symbol references at link time (this ld build
# checks transitively-NEEDED shared libraries even without `--no-undefined`
# on the command line - confirmed no such flag is present in the generated
# link.txt, so this isn't a `--no-undefined` interaction). The mechanism for
# that is `-rpath-link`, not `-L`/link_directories() (which only affects
# where `-lNAME`-style linking searches, and nothing here links openssl that
# way - `link_directories()` was tried first and verified to add no `-L` to
# the generated link.txt at all, confirming it's a no-op for this target).
# (Not needed on tg5040: its curl links the sysroot's own complete OpenSSL,
# already inside CMAKE_FIND_ROOT_PATH / the default sysroot lib search path.)
if(FLYCAST_TOOLCHAIN_PLATFORM STREQUAL "tg5050")
	set(OPENSSL_PREBUILT_ROOT /root/workspace/all/other/flycast/openssl-prebuilt-aarch64-tg5050)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${OPENSSL_PREBUILT_ROOT}/lib" CACHE STRING "" FORCE)
endif()
