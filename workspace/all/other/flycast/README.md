# flycast (Sega Dreamcast) — DC.pak

Standalone flycast pinned at v2.6 (`392a429e`), patched with the shared NxRedux
in-game overlay menu (`workspace/all/common/emu_overlay*`). Both tg5040 and
tg5050 build and link clean with `flycast.patch` applied — the "Build"
section below documents the baseline (unpatched) build first, including a
tg5040 compiler bug that once blocked it, before the "Patch" section
explains the fix.

Build/deploy docs are completed alongside the implementation — see git history
of this file. Fetch sources: `make ../all/other/flycast/flycast` inside
`workspace/tg5040/` (or tg5050; the checkout is shared).

## Build

Baseline (unpatched) Docker cross-build, validated for both platforms against
the pinned v2.6 checkout. Build dirs are `flycast/build-tg5040` and
`flycast/build-tg5050` (gitignored). Both platforms need a one-time prebuilt
`libcurl` (Step 0b below; tg5050 additionally needs OpenSSL, Step 0a) before
the `cmake` commands in Step 1/2 will configure.

### Step 0a (one-time, tg5050 only): cross-build OpenSSL 1.1.1w

RetroAchievements is an explicit requirement and the Smart Pro S (tg5050) is
one of the two physical target devices, so a curl without working HTTPS is
not acceptable there (see revision history of this section — an earlier pass
shipped tg5050 curl `--without-ssl` as a stopgap; superseded by this step).

tg5050's sysroot ships OpenSSL *headers* (`openssl/ssl.h` etc.) and
`libcrypto.so`/`libcrypto.a`, but **no `libssl` at all** anywhere in the
image — not `.so`, not `.a` (confirmed with an exhaustive `find`; the only
`libssl*` found is the *host* Ubuntu image's own unrelated `libssl.so.3`
under `/usr/lib/aarch64-linux-gnu`, OpenSSL 3.x, ABI-incompatible with the
sysroot's 1.1.1 headers). No other TLS backend is available in that sysroot
either (no GnuTLS, no mbedTLS). So OpenSSL 1.1.1 is cross-built from source
too — the 1.1.1 series' final release, 1.1.1w, matching the ABI (symbol
versions like `OPENSSL_1_1_0`/`OPENSSL_1_1_1`) the sysroot's existing headers
and libcrypto already expect. Installed to
`workspace/all/other/flycast/openssl-prebuilt-aarch64-tg5050/` — **not** into
the toolchain image's sysroot (kept entirely in the mounted workspace, like
every other prebuilt dependency here; gitignored, rebuild recipe below).

```sh
mkdir -p workspace/all/other/flycast/openssl-build-work
cd workspace/all/other/flycast/openssl-build-work
curl -sL https://www.openssl.org/source/openssl-1.1.1w.tar.gz -o openssl-1.1.1w.tar.gz
tar xf openssl-1.1.1w.tar.gz
```

```sh
docker run --rm -v "$PWD/workspace":/root/workspace \
  ghcr.io/loveretro/tg5050-toolchain:latest bash -c '
set -e
cd /root/workspace/all/other/flycast/openssl-build-work/openssl-1.1.1w
# The image presets CC/CXX/AR/AS/LD as absolute paths (e.g. CC=/opt/.../aarch64-nextui-linux-gnu-gcc)
# AND --cross-compile-prefix prepends "aarch64-nextui-linux-gnu-" onto whatever
# $CC/$AR/etc already are - combining both double-prefixes every tool name into
# a broken path. Unset them all first and let --cross-compile-prefix + PATH do it.
unset CC CXX CROSS_COMPILE AR AS LD RANLIB NM STRIP OBJCOPY OBJDUMP
export PATH=/opt/aarch64-nextui-linux-gnu/bin:$PATH
./Configure linux-aarch64 shared no-tests \
  --cross-compile-prefix=aarch64-nextui-linux-gnu- \
  --prefix=/root/workspace/all/other/flycast/openssl-prebuilt-aarch64-tg5050 \
  --openssldir=/root/workspace/all/other/flycast/openssl-prebuilt-aarch64-tg5050/ssl
make -j$(nproc)
make install_sw'
```

`install_sw` (not plain `install`) installs libraries/headers/binaries only,
skipping man pages and docs.

### Step 0b (one-time, per platform): cross-build libcurl

flycast's `CMakeLists.txt` does `find_package(CURL REQUIRED)` unconditionally
for any UNIX-not-Apple build (`core/oslib/http_client.cpp`, used for the
in-app update checker, boxart downloads, and RetroAchievements HTTP calls) —
there is no CMake option to disable or vendor it, and neither toolchain
sysroot ships libcurl (headers or library) at all. It is cross-built from
upstream curl 8.9.1 as a **shared** library (a static build needs its SSL/zlib
dependencies re-specified explicitly at flycast's link time, since
`FindCURL.cmake`'s module-mode `CURL::libcurl` target carries no
`INTERFACE_LINK_LIBRARIES`; a shared `libcurl.so` carries its own `NEEDED`
entries instead and just works) and installed to
`workspace/all/other/flycast/curl-prebuilt-aarch64-<platform>/` (gitignored,
like the build dirs — rebuild from scratch on a clean checkout).

**This is built separately per platform, not shared** — the two toolchain
images (different GCC versions, 8.3.0 vs 10.3.0) are not ABI-interchangeable
for it. Both are now SSL-enabled: tg5040's curl links the sysroot's own
complete OpenSSL 1.1.1 directly; tg5050's curl links the prebuilt OpenSSL
1.1.1w from Step 0a instead (its sysroot's OpenSSL is incomplete — see Step
0a).

```sh
# Download once (either platform):
mkdir -p workspace/all/other/flycast/curl-build-work
cd workspace/all/other/flycast/curl-build-work
curl -sL https://curl.se/download/curl-8.9.1.tar.gz -o curl-8.9.1.tar.gz
tar xf curl-8.9.1.tar.gz
```

**tg5040 (SSL against the sysroot's own OpenSSL 1.1.1):**

```sh
docker run --rm -v "$PWD/workspace":/root/workspace \
  ghcr.io/loveretro/tg5040-toolchain:latest bash -c '
set -e
cd /root/workspace/all/other/flycast/curl-build-work/curl-8.9.1
export PATH=/opt/aarch64-nextui-linux-gnu/bin:$PATH
SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
export PKG_CONFIG_PATH=$SYSROOT/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$SYSROOT
./configure --host=aarch64-nextui-linux-gnu --build=x86_64-pc-linux-gnu \
  --prefix=/root/workspace/all/other/flycast/curl-prebuilt-aarch64-tg5040 \
  --with-openssl="$SYSROOT/usr" --with-zlib="$SYSROOT/usr" \
  --enable-shared --disable-static \
  --disable-ldap --disable-ldaps --disable-manual --disable-docs \
  --without-nghttp2 --without-nghttp3 --without-ngtcp2 --without-quiche \
  --without-brotli --without-zstd --without-libidn2 --without-librtmp \
  --without-libssh2 --without-libpsl --without-libgsasl --disable-ares
make -j$(nproc)
make install'
```

**tg5050 (SSL against the Step 0a prebuilt OpenSSL 1.1.1w):**

```sh
docker run --rm -v "$PWD/workspace":/root/workspace \
  ghcr.io/loveretro/tg5050-toolchain:latest bash -c '
set -e
cd /root/workspace/all/other/flycast/curl-build-work/curl-8.9.1
unset CC CXX CROSS_COMPILE AR AS LD RANLIB NM STRIP OBJCOPY OBJDUMP
export PATH=/opt/aarch64-nextui-linux-gnu/bin:$PATH
SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
OPENSSL_PREFIX=/root/workspace/all/other/flycast/openssl-prebuilt-aarch64-tg5050
export PKG_CONFIG_PATH=$SYSROOT/usr/lib/pkgconfig:$OPENSSL_PREFIX/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=""
make distclean || true
./configure --host=aarch64-nextui-linux-gnu --build=x86_64-pc-linux-gnu \
  --prefix=/root/workspace/all/other/flycast/curl-prebuilt-aarch64-tg5050 \
  --with-openssl="$OPENSSL_PREFIX" --with-zlib="$SYSROOT/usr" \
  --enable-shared --disable-static \
  --disable-ldap --disable-ldaps --disable-manual --disable-docs \
  --without-nghttp2 --without-nghttp3 --without-ngtcp2 --without-quiche \
  --without-brotli --without-zstd --without-libidn2 --without-librtmp \
  --without-libssh2 --without-libpsl --without-libgsasl --disable-ares
make -j$(nproc)
make install'
```

Note `PKG_CONFIG_SYSROOT_DIR=""` (cleared, not the sysroot) — curl's configure
finds our OpenSSL's own `libssl.pc`/`libcrypto.pc` under
`$OPENSSL_PREFIX/lib/pkgconfig` via `--with-openssl`, which is an absolute
workspace path outside the sysroot; leaving `PKG_CONFIG_SYSROOT_DIR` pointed
at the sysroot would incorrectly re-prefix that path.

`--build=x86_64-pc-linux-gnu` is required even though the Docker host may
itself be aarch64 (e.g. Apple Silicon): without an explicit `--build` that
textually differs from `--host`, curl's `configure` script concludes it is
*not* cross-compiling (since `config.guess` on an aarch64 build machine
resolves to an aarch64 triple too) and runs a `checking run-time libs
availability` test that tries to *execute* the just-compiled target binary
directly on the build host — which fails (wrong sysroot/dynamic linker), not
because of any real problem with the libraries.

`toolchain-aarch64.cmake` picks the right prebuilt curl via an explicit,
required `FLYCAST_TOOLCHAIN_PLATFORM` cache variable
(`-DFLYCAST_TOOLCHAIN_PLATFORM=tg5040` or `tg5050` on the `cmake` command
line — see Steps 1/2 below), not by sniffing the sysroot for a marker file.
(An earlier revision of this file used `if(EXISTS .../libssl.so.1.1)` to
infer the platform — dropped because it's fragile: it silently picks the
wrong platform's prebuilt if the sysroot's shape ever changes, and now that
tg5050 also has a real libssl (its own prebuilt from Step 0a, not in the
sysroot) that check no longer even means what its name implies.)

```cmake
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FLYCAST_TOOLCHAIN_PLATFORM)
# ...
if(NOT DEFINED FLYCAST_TOOLCHAIN_PLATFORM)
	message(FATAL_ERROR "FLYCAST_TOOLCHAIN_PLATFORM must be set (-DFLYCAST_TOOLCHAIN_PLATFORM=tg5040 or tg5050) ...")
endif()
set(CURL_PREBUILT_ROOT /root/workspace/all/other/flycast/curl-prebuilt-aarch64-${FLYCAST_TOOLCHAIN_PLATFORM})
set(CURL_INCLUDE_DIR ${CURL_PREBUILT_ROOT}/include)
set(CURL_LIBRARY ${CURL_PREBUILT_ROOT}/lib/libcurl.so)

# tg5050 only: libcurl.so links our prebuilt OpenSSL (Step 0a), which lives
# outside the sysroot and outside libcurl.so's own RPATH. This ld needs
# -rpath-link (not -L/link_directories(), which was tried first and verified
# to have no effect - see the toolchain file's own comment) to locate
# libssl.so.1.1/libcrypto.so.1.1 and validate libcurl.so's transitive
# OPENSSL_1_1_* symbol references at link time.
if(FLYCAST_TOOLCHAIN_PLATFORM STREQUAL "tg5050")
	set(OPENSSL_PREBUILT_ROOT /root/workspace/all/other/flycast/openssl-prebuilt-aarch64-tg5050)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${OPENSSL_PREBUILT_ROOT}/lib" CACHE STRING "" FORCE)
endif()
```

(Full context/comments in the file itself — this is a trimmed excerpt.)

### SDL2 host-library discovery hint

`-DUSE_HOST_SDL=ON` alone is **not sufficient** on this toolchain: flycast's
`CMakeLists.txt` calls plain `find_package(SDL2 2.0.9)` (module-then-config
search), but stock CMake ships no `FindSDL2.cmake` module (only
`FindSDL.cmake`, for SDL 1.x) and neither toolchain sysroot ships an
`SDL2Config.cmake`/`sdl2-config.cmake` — only a pkg-config `sdl2.pc`. Without
a discovery mechanism, `find_package(SDL2 2.0.9)` silently fails, `SDL2_FOUND`
stays false, and flycast falls back to `add_subdirectory(core/deps/SDL)` —
**building and statically linking the vendored SDL2**, exactly what the task
brief says never to do silently.

Fixed with a small hand-written CONFIG-mode package file (tracked in git —
see the `.gitignore` exception added for this directory — at
`workspace/all/other/flycast/cmake-hints/lib/cmake/SDL2/{SDL2Config,SDL2ConfigVersion}.cmake`),
which wraps `pkg_check_modules(sdl2)` and defines the `SDL2::SDL2` imported
target flycast's `CMakeLists.txt` looks for. Discovered via
`CMAKE_PREFIX_PATH`, appended in `toolchain-aarch64.cmake`:

```cmake
list(APPEND CMAKE_PREFIX_PATH /root/workspace/all/other/flycast/cmake-hints)
```

With this in place `-DUSE_HOST_SDL=ON` resolves to the real host SDL2
(`libSDL2-2.0.so.0`, version 2.26.1 on tg5040 / 2.32.6 on tg5050) and it
shows up as a `NEEDED` entry on the final binary — no vendored-SDL2 CMake
configure banner, no static SDL2 baked in.

`USE_HOST_LIBZIP` needed no such hint: pkg-config's `libzip.pc` is enough for
flycast's own `pkg_check_modules(LIBZIP ...)` call, so the default
`USE_HOST_LIBZIP=ON` (not passed explicitly, matches the brief's flag set)
just works, and libzip shows up as `NEEDED` too.

### Step 1: configure + build (tg5040)

**Status update (post-patch):** the GCC 8.3.0 internal compiler error described
below — which left tg5040 BLOCKED for this baseline, no-source-changes task —
is fixed by `flycast.patch`'s `sh4_interrupts.cpp` hunk (see "Patch: what
`flycast.patch` does" further down this file). With the patch applied, tg5040
builds and links clean end-to-end, same as tg5050. The bisection and BLOCKED
framing immediately below are kept verbatim as the reasoning record for why
that patch hunk exists — it describes the true state of the *baseline,
unpatched* checkout at the time this section was written, not the current
build status.

```sh
docker run --rm -v "$PWD/workspace":/root/workspace -w /root/workspace/all/other/flycast/flycast \
  ghcr.io/loveretro/tg5040-toolchain:latest bash -c '
  cmake -B build-tg5040 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/root/workspace/all/other/flycast/toolchain-aarch64.cmake \
    -DFLYCAST_TOOLCHAIN_PLATFORM=tg5040 \
    -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_HOST_SDL=ON \
    -DUSE_LUA=OFF -DUSE_BREAKPAD=OFF \
    -DUSE_PULSEAUDIO=OFF -DUSE_LIBAO=OFF -DUSE_ALSA=OFF &&
  cmake --build build-tg5040 -j$(nproc)'
```

Flag set matches the brief's, plus the one required `FLYCAST_TOOLCHAIN_PLATFORM`
selector cache var (all the other extra work went into the toolchain file /
prebuilt curl / SDL2 hint above, not into further `-D`s here). Configure
succeeds cleanly (CURL, ZLIB, SDL2, libzip all found; no MiniUPnPc — optional,
`find_package(MiniUPnPc)` has no `REQUIRED`, harmless).

The **build** reliably crashes the tg5040 image's compiler compiling
`core/hw/sh4/sh4_interrupts.cpp`:

```
core/hw/sh4/sh4_interrupts.cpp:264:1: internal compiler error: in gen_reg_rtx, at emit-rtl.c:1187
```
(`aarch64-nextui-linux-gnu-g++ (crosstool-NG 1.25.0) 8.3.0`)

This was exhaustively bisected and is **flag-independent** — not a
Release/optimization issue, and not fixable via a CMake cache setting:
reproduced identically at `-O0`, `-O1`, `-O2`, `-O3`, `-Og`, with and without
each of `-fno-tree-vectorize`, `-fno-schedule-insns2`, `-fno-gcse`,
`-fno-expensive-optimizations`, `-mgeneral-regs-only`, with `-std=c++14` in
place of `-std=c++17`, and with/without `-fno-strict-aliasing`/`-DNDEBUG`.
The **identical** source file, flags, and defines compile cleanly under the
tg5050 image's compiler (`... 10.3.0`), confirming this is a GCC 8.3.0
aarch64-backend bug specific to the tg5040 toolchain, not a flycast source
defect or a build-flag problem. Per the task's constraint against improvising
source edits: **not worked around here**. Controller decision: accepted as
BLOCKED for this (baseline, no-source-changes) task — the fix belongs to
whichever task owns flycast source changes (it'll ride in `flycast.patch`,
e.g. a per-file optimization override or a small code restructure to dodge
whatever pattern trips the bug), using this exhaustive bisection as the
starting point.

### Step 2: configure + build + verify (tg5050 — succeeded)

```sh
docker run --rm -v "$PWD/workspace":/root/workspace -w /root/workspace/all/other/flycast/flycast \
  ghcr.io/loveretro/tg5050-toolchain:latest bash -c '
  cmake -B build-tg5050 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/root/workspace/all/other/flycast/toolchain-aarch64.cmake \
    -DFLYCAST_TOOLCHAIN_PLATFORM=tg5050 \
    -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_HOST_SDL=ON \
    -DUSE_LUA=OFF -DUSE_BREAKPAD=OFF \
    -DUSE_PULSEAUDIO=OFF -DUSE_LIBAO=OFF -DUSE_ALSA=OFF &&
  cmake --build build-tg5050 -j$(nproc)'
```

Same flag set as tg5040 (plus the platform selector both now need). Builds
clean (a few harmless `-Wclass-memaccess` warnings from the vendored `ggpo`
deps and a C++17-vs-C++14 argument-passing ABI note from GCC 10, both
pre-existing/harmless), produces `build-tg5050/flycast` with full HTTPS
support.

Verify the resulting binary:

```sh
file build-tg5050/flycast
readelf -d build-tg5050/flycast | grep NEEDED
```

```
build-tg5050/flycast: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, for GNU/Linux 5.15.0, stripped

 0x0000000000000001 (NEEDED)             Shared library: [libSDL2-2.0.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libcurl.so.4]
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [librt.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libdl.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libudev.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libzip.so.5]
 0x0000000000000001 (NEEDED)             Shared library: [libpthread.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

Matches expectations: dynamic aarch64 ELF, `libSDL2` present (host, not
vendored), no `libpulse`/`libao`/`liblua`. GLES/EGL isn't in the `NEEDED`
list — flycast loads it via `glad` (`dlopen`), consistent with the brief's
"via dlopen or NEEDED" wording.

`libcurl.so` itself is SSL-enabled — `readelf -d curl-prebuilt-aarch64-tg5050/lib/libcurl.so`:

```
 0x0000000000000001 (NEEDED)             Shared library: [libssl.so.1.1]
 0x0000000000000001 (NEEDED)             Shared library: [libcrypto.so.1.1]
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libpthread.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

**Deployment note (superseded):** `readelf -d` also shows an `RPATH` entry
pointing at `curl-prebuilt-aarch64-tg5050/lib` inside the Docker build mount —
that directory won't exist on-device, but a nonexistent `RPATH` entry is
simply skipped by the dynamic loader, which then falls through to
`LD_LIBRARY_PATH` (the pak's `launch.sh` puts `$PAK_DIR` first, per the
existing mupen64plus pattern for `libsamplerate.so.0`). An earlier revision of
this note said the real requirement was to ship `libcurl.so.4` (and, for
tg5050, `libssl.so.1.1`/`libcrypto.so.1.1`) in the pak directory — that has
since been checked and is **not needed**: both platforms' firmware ships its
own complete, SSL-enabled `libcurl.so.4` (verified via `LD_TRACE_LOADED_OBJECTS=1`
on the Brick's `/usr/lib64` and, later, on the Smart Pro S in a full on-device
session). Nothing from the cross-built `curl-prebuilt-aarch64-*`/
`openssl-prebuilt-aarch64-tg5050` trees is shipped in the pak — see "Runtime
libraries" below.

(At baseline this sentence read "tg5040's build never reaches this point" —
since fixed; see "Layout, pin, and fetching" below, where the patched tg5040
rebuild is verified byte-identical to the staged pak binary.)

### Summary of deviations from the brief's plan

| Deviation | Why | Where |
|---|---|---|
| Cross-built `libcurl` from source, per platform, **SSL-enabled on both** | Sysroots ship no libcurl at all; `find_package(CURL REQUIRED)` has no disable option; RetroAchievements requires working HTTPS on both physical target devices | `curl-prebuilt-aarch64-{tg5040,tg5050}/` (gitignored, rebuild recipe above) |
| Cross-built OpenSSL 1.1.1w from source, tg5050 only | tg5050 sysroot ships OpenSSL headers + libcrypto but no libssl anywhere (not `.so`, not `.a`) — its own curl couldn't link SSL support against it | `openssl-prebuilt-aarch64-tg5050/` (gitignored, rebuild recipe above) |
| Added `-DFLYCAST_TOOLCHAIN_PLATFORM=tg5040\|tg5050` as a **required extra `-D`** on the `cmake` command line, plus matching toolchain-file logic (`CURL_INCLUDE_DIR`/`CURL_LIBRARY` selection, `-Wl,-rpath-link` for tg5050's out-of-sysroot OpenSSL) | Explicit, fails-loudly selection of the right prebuilt per platform, replacing an earlier `if(EXISTS .../libssl.so.1.1)` sysroot-sniffing check that was fragile and, after the OpenSSL fix, no longer even a meaningful signal | `toolchain-aarch64.cmake`; command lines in Steps 1/2 above |
| Added hand-written `SDL2Config.cmake`/`SDL2ConfigVersion.cmake` + `CMAKE_PREFIX_PATH` hint | `-DUSE_HOST_SDL=ON` alone can't find host SDL2 (no Find/Config module anywhere); without this it silently vendors SDL2 statically | `cmake-hints/lib/cmake/SDL2/`, `toolchain-aarch64.cmake`; new `.gitignore` exception to track these two small files (unlike the curl/build-dir binaries) |
| tg5040 build **BLOCKED** in this baseline task (accepted — fix deferred to the task that owns flycast source changes); **since fixed** by `flycast.patch`'s `sh4_interrupts.cpp` hunk (constant-init `enum class IprReg` refactor of `InterruptSourceList`'s `PrioReg` — see "Patch: what `flycast.patch` does" below) | GCC 8.3.0 (crosstool-NG 1.25.0) internal compiler error compiling `core/hw/sh4/sh4_interrupts.cpp`, reproduced flag-independently, absent under tg5050's GCC 10.3.0 with identical source/flags | see Step 1 |

No brief flags were dropped or silently changed: `-DUSE_GLES=ON` stayed on
both platforms, and (once the SDL2 hint is in place) host SDL2 is what
actually links — not vendored. The one real command-line addition beyond the
brief's flag set is `-DFLYCAST_TOOLCHAIN_PLATFORM`, required so the toolchain
file can pick the right prebuilt curl/OpenSSL.

## Layout, pin, and fetching

```
workspace/all/other/flycast/
├── README.md                ← this file
├── flycast.patch             ← the 7-file, 11-hunk source patch (see below)
├── toolchain-aarch64.cmake   ← shared cross-compile toolchain file (CURL/SDL2/OpenSSL hints)
├── cmake-hints/               ← hand-written SDL2Config.cmake package (tracked, see SDL2 section above)
├── flycast/                   ← gitignored checkout — cloned + patched by the Makefile rule below
├── curl-prebuilt-aarch64-{tg5040,tg5050}/    ← gitignored, Step 0b
└── openssl-prebuilt-aarch64-tg5050/          ← gitignored, Step 0a
```

Pinned identically in both `workspace/tg5040/Makefile` and `workspace/tg5050/Makefile`
(kept in sync):

```makefile
REQUIRES_FLYCAST=../all/other/flycast/flycast
FLYCAST_REPO=https://github.com/flyinghead/flycast.git
FLYCAST_COMMIT=392a429e8b040b3e5bf6696cb4f984274fc44123  # v2.6, 2026-07
```

Fetch from either platform dir — the checkout is shared, same "build once, use
from both" layout as the GLideN64 checkout in `workspace/all/other/mupen64plus/`:

```sh
cd workspace/tg5040   # or workspace/tg5050 — resolves to the same shared checkout
make ../all/other/flycast/flycast
```

which runs this rule:

```makefile
$(REQUIRES_FLYCAST):
	[ -d $(REQUIRES_FLYCAST) ] || ( \
		git clone $(FLYCAST_REPO) $(REQUIRES_FLYCAST) && \
		cd $(REQUIRES_FLYCAST) && \
		git checkout $(FLYCAST_COMMIT) && \
		git submodule update --init --recursive && \
		git apply ../flycast.patch )
```

`[ -d ... ]` makes this idempotent — once the checkout exists, reruns are a
no-op, same guard convention the mupen64plus rules use. Verified end-to-end
from a clean state (clone → checkout → submodules → `git apply` → rebuild) in
Task 8: the rebuilt `build-tg5040/flycast` matched the already-staged pak
binary byte-for-byte except a 4-byte embedded build timestamp.

## Patch: what `flycast.patch` does

`workspace/all/other/flycast/flycast.patch` (26,357 bytes, 11 hunks across 7
files) carries the NxRedux in-game overlay integration plus one compiler
workaround and one audio-quality fix, all on top of the pinned v2.6 checkout:

| File | Change |
|---|---|
| `core/nx_overlay.h` (new) | The overlay's 3-function public interface: `nx_overlay_frame()`, `nx_overlay_after_render()`, `nx_overlay_request_menu()`. |
| `core/nx_overlay.cpp` (new) | The hook itself — menu open/close state machine, SDL joystick polling (d-pad + analog-stick hat merge), Save State / Load State / Quit actions via `dc_savestate()`/`dc_loadstate()`/`dc_exit()` (each wrapped in its own try/catch so a bad or missing slot can't strand the emulator in a stopped, unresumable state), and one-shot game-switcher resume-slot consumption on the first running frame. Compiled together with `workspace/all/common/emu_overlay*.c` — the same shared overlay code the N64 pak uses — via the CMake glue below. |
| `core/ui/mainui.cpp` | Hooks `mainui_rend_frame()`: when the overlay wants a frame it renders the overlay instead of the game and calls `imguiDriver->setFrameRendered()` so `OpenGLDriver::present()` actually swaps it to screen (without this the overlay drew into the backbuffer but the swap never happened — an invisible-menu soft-lock, found and fixed in review); otherwise it falls through to `emu.render()` immediately followed by `nx_overlay_after_render()`, so the overlay's "capture the backbuffer, then decide whether to open" timing lines up with the frame that was actually just presented — the flycast analog of GLideN64's `swapBuffers()` hook. |
| `core/input/gamepad_device.cpp` | Redirects the existing `EMU_BTN_MENU` case (previously `gui_open_settings()`) to `nx_overlay_request_menu()` — a second, mapped-input path into the same menu-open request the direct SDL button-8 polling in `nx_overlay.cpp` already provides as belt-and-braces. `EMU_BTN_ESCAPE` (still `dc_exit()`) is untouched. |
| `core/hw/sh4/sh4_interrupts.cpp` | GCC 8.3.0 (tg5040 toolchain) internal-compiler-error workaround. `InterruptSourceList` was `static const`, but its `PrioReg` field was a `const u16*` built via a reinterpret-cast macro (`GIPA`/`GIPB`/`GIPC`) — never a C++ constant expression, so the compiler had to emit a dynamic global constructor, and GCC 8.3.0's aarch64 backend crashes expanding it (`internal compiler error: in gen_reg_rtx, at emit-rtl.c:1187`). Refactored `PrioReg` into an `enum class IprReg : u8 { Fixed, IPRA, IPRB, IPRC }` tag, resolved to the live register value via a `switch` inside `GetPrLvl()` — same one dereference-shift-mask cost as before, at the same single call site — so the initializer is now a literal integer aggregate, fully constant-initializable, no dynamic constructor, no ICE. Confirmed flag-independent (reproduced at `-O0` through `-O3`/`-Og`, with/without half a dozen individual optimization-pass toggles) and toolchain-specific (absent under tg5050's GCC 10.3.0 compiling the identical file/flags) before landing this fix — see the "Status update" note in Step 1 above. |
| `CMakeLists.txt` | Overlay-sources block, guarded `if(NOT LIBRETRO AND NOT ANDROID AND NOT APPLE AND NOT WIN32)`: compiles `nx_overlay.cpp` plus the 3 shared `workspace/all/common/emu_overlay*.c` files into the `flycast` target. Scopes their `-I .../common` include path to just those sources (`set_source_files_properties(... PROPERTIES INCLUDE_DIRECTORIES ...)`) rather than target-wide — a target-wide include broke *every* pre-existing flycast `#include <SDL.h>` by resolving it against `workspace/all/common/sdl.h` (an SDL1/2 compatibility shim used by other emulator cores in this workspace) instead of the toolchain's real SDL2 headers, on this Docker bind mount's case-insensitive filesystem. Also links `GLESv2` directly (+ `mali` on tg5050, whose `libGLESv2.so` is a thin stub backed by `libmali.so.0`) — the overlay calls GLES3 functions directly (`emu_overlay_sdl.c`), unlike flycast's own renderer which resolves GL lazily via `glad`+`dlopen()`. |
| `core/audio/audiobackend_sdl2.cpp` | Sink-preferred audio rate, driven by `NX_AUDIO_RATE` (see "Audio: NX_AUDIO_RATE (sink-preferred output rate)" below). Upstream `SDLAudioBackend::init()` tried opening the SDL audio device at 44.1 kHz (the Dreamcast's native rate) first, falling back to 48 kHz + an `SDL_AudioCVT` resampler only if that failed. On this hardware the 44.1 kHz open always *succeeds* (ALSA's `plug` device accepts any rate), but the device's real output — `dmix`, fixed at 48000 Hz with no quality `rate_converter` configured — then does its own low-quality linear resample from 44.1→48 kHz in `alsa-lib`, producing constant audible distortion (the same class of bug already fixed for mupen64plus/N64.pak — see that pak's README for the precedent). The patch reverses the order and generalizes the target: `target_rate` defaults to 48000 but is overridable via the `NX_AUDIO_RATE` env var (integer Hz, range-validated (`[8000, 192000]`; out-of-range falls back to 48000)), which `DC.pak/launch.sh`'s `nx_pick_audio_rate()` helper sets from the audiomon-published `/tmp/nx_audio_sink` sink info — opening at `target_rate` (matching the device's real output rate exactly when it's 48000, so ALSA does no resampling at all) with SDL's own higher-quality `SDL_BuildAudioCVT` converter as the primary path when `target_rate != 44100`, skipping the converter entirely when the sink prefers native 44.1 kHz, and falling back to a literal native 44.1 kHz open only if the `target_rate` open or converter build fails. The pre-existing `needs_resampling`/`audioCvt` buffer machinery is unchanged (buffer sizing is driven by the device's fixed frame-count callback size, not by sample rate, so it already covered this code path when it was the fallback). Also switches the mode-report log lines from `INFO_LOG` to `NOTICE_LOG`, since `INFO_LOG` is compiled out entirely in Release builds (`MAX_LOGLEVEL`, `core/log/Log.h`) and would otherwise never reach the on-device log — `NOTICE_LOG` matches the codebase's existing convention for startup path decisions (e.g. `core/emulator.cpp`'s `"Forcing real BIOS"`/`"Forcing HLE BIOS"`) and is what lets `$LOGS_PATH/DC.txt` prove which rate path a given run actually took. |

## Regenerating the patch

From inside the live checkout (`workspace/all/other/flycast/flycast/`,
gitignored, `HEAD` detached at the pinned commit with the patch's changes
already applied as uncommitted working-tree edits):

```sh
cd workspace/all/other/flycast/flycast
git add -A ':!build-tg5040' ':!build-tg5050'   # stage everything except the gitignored build dirs
git status --short                              # sanity check: only the intended source hunks
git diff --cached --binary > ../flycast.patch
git reset                                       # unstage — working tree stays the source of truth
```

`--binary` isn't load-bearing today (no binary files are patched) but costs
nothing and future-proofs the recipe if that ever changes. Before trusting a
regenerated patch, sanity-check the round-trip: `git apply --check -R
../flycast.patch` run from the checkout should succeed with no output,
confirming the patch is byte-exact against the actual working-tree diff (not
mangled by the stage/diff/reset round-trip).

## Deploy

```sh
adb push skeleton/SYSTEM/tg5040/paks/Emus/DC.pak /mnt/SDCARD/.system/paks/Emus/
adb push skeleton/BASE/Emus/shared/flycast /mnt/SDCARD/Emus/shared/
adb shell chmod +x "/mnt/SDCARD/.system/paks/Emus/DC.pak/launch.sh" "/mnt/SDCARD/.system/paks/Emus/DC.pak/flycast"
```

Same pattern for tg5050 (substitute the platform directory). No reboot needed
— nextui.elf isn't touched by pushing a pak. Verified on both physical target
devices: the Brick (tg5040) in Task 7, and the Smart Pro S (tg5050) in a later
full on-device session.

## Runtime layout (on-device)

`DC.pak/launch.sh` (tg5040 and tg5050 versions are otherwise identical —
only the CPU-cluster block, `LD_LIBRARY_PATH`'s `.system/<platform>/lib`
component, and per-model vs. single config-dir branching differ) points
flycast's own `XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`FLYCAST_BIOS_PATH`
resolution (`core/linux-dist/main.cpp`) at:

Accepted deviation: `launch.sh` sets the CPU governor to `performance` on
launch but never restores it on exit — the same convention N64.pak already
uses on this repo, not something new introduced for DC.pak.

| Path | Purpose |
|---|---|
| `$SHARED_USERDATA_PATH/DC-flycast/config/<model>/flycast/emu.cfg` | Per-device config (`XDG_CONFIG_HOME=.../DC-flycast/config/<model>`). `<model>` is one of `tg5040-brick`, `tg5040-brickpro`, `tg5040-smart-pro`, `tg5050` — separate resolution/CPU-governor defaults per model, seeded on first run from `DC.pak/default-{brick,brickpro,smartpro}.cfg` (tg5040) or `default.cfg` (tg5050). |
| `$SHARED_USERDATA_PATH/DC-flycast/data/flycast/` | VMUs + native flycast save states (`XDG_DATA_HOME=.../DC-flycast/data`) — **shared across all 4 device configs**, since a memory card or save state should follow the game regardless of which device made it, unlike screen resolution. |
| `$SDCARD_PATH/Bios/DC/` | BIOS search path (`FLYCAST_BIOS_PATH`) — `dc_boot.bin` required for real-BIOS boot, `dc_flash.bin` optional (auto-created), `naomi.zip` optional (arcade, out of scope v1). See [BIOS](#bios) below. |

## Save-state model

Slots follow the shared overlay convention (`workspace/all/common/emu_overlay.c`)
— the same scheme N64.pak already ships:

- **Slots 0–7** — visible, user-managed save states via the in-game overlay
  (MENU button opens it; Save State / Load State pick a slot, with a
  screenshot shown for each occupied slot).
- **Slot 8** — the launcher's fresh-start sentinel. NxRedux writes `8` to
  `/tmp/resume_slot.txt` on a non-resume ("A START") launch; the overlay's
  one-shot resume check ignores it — never saved to or loaded from
  automatically.
- **Slot 9** — hidden auto-save-on-quit (`EMU_OVL_AUTO_SLOT`). Choosing Quit
  from the overlay auto-saves to slot 9 plus a Game Switcher screenshot,
  before calling `dc_exit()`. NxRedux writes `9` on a Game Switcher RESUME
  launch, and the overlay's one-shot resume check loads it. Net effect:
  **every quit is Game-Switcher-resumable** without the player needing to
  remember to save first — the same standalone-resume convention already
  shipped for N64.pak (mupen64plus + GLideN64), consuming the identical
  `/tmp/resume_slot.txt` handshake nxredux writes on every launch.

## BIOS

`launch.sh` checks for `$SDCARD_PATH/Bios/DC/dc_boot.bin` before every launch
and seds `emu.cfg`'s `UseReios` accordingly: present → `UseReios = no` (boots
the genuine Dreamcast BIOS); missing → `UseReios = yes` (flycast's HLE/Reios
boot). **Launch is never blocked** either way — this only steers which boot
path flycast takes, not whether it runs.

Only `dc_boot.bin` gates the check. `dc_flash.bin` (the flash/EEPROM image
holding region/language settings and save data for BIOS-level features) is
**not** required — flycast auto-creates it on demand when missing
(`core/hw/flashrom/nvmem.cpp`), so gating on its presence was overly strict
and would force HLE boot even with a valid `dc_boot.bin` on a fresh SD card.
`naomi.zip` (Naomi arcade BIOS) is unrelated to console boot and only matters
for arcade-board content, which is out of scope for v1 — flycast never checks
for it in `launch.sh`.

Both `UseReios` branches were verified round-trip in Task 7 (with placeholder
BIOS files; no real BIOS dump was available in that test session). A real,
known-good `dc_boot.bin` (md5 `e10c53c2`) was later provided and pushed to
the device, which is what surfaced this bug — the old two-file gate forced
HLE boot despite the valid BIOS being present. On-device verification of the
corrected single-file gate is **done**: confirmed on both the Brick (tg5040)
and the Smart Pro S (tg5050) with the real `dc_boot.bin` in place — real-BIOS
boot takes over cleanly on both, and the HLE fallback still works with the
file absent.

## Audio: NX_AUDIO_RATE (sink-preferred output rate)

`core/audio/audiobackend_sdl2.cpp`'s `SDLAudioBackend::init()` opens the SDL
audio device at a rate driven by the `NX_AUDIO_RATE` environment variable
(integer Hz), which both `DC.pak/launch.sh` scripts (tg5040 and tg5050,
identical insertion) export before invoking `./flycast`. Source of truth is
`/tmp/nx_audio_sink`, published by the `audiomon` daemon as a `rates=` line
(a single value or a space-separated list, most-preferred rate first).
`launch.sh`'s `nx_pick_audio_rate()` helper picks: the Dreamcast's native
44100 Hz if it's exactly listed in `rates=`; otherwise the sink's first
(most-preferred) listed rate; 48000 Hz if `/tmp/nx_audio_sink` is absent
(audiomon not running, or no sink info published yet) — the same 48 kHz
default this code path used to hard-code unconditionally. This read happens
once, at launch: `NX_AUDIO_RATE` is exported before `./flycast` starts and
the SDL device is opened once against it, so a sink change mid-game (e.g. a
Bluetooth hotplug or USB DAC plug/unplug) does not reopen the audio device —
flycast keeps playing at the rate it launched with, matching the pre-existing
routing behavior elsewhere in this branch (nothing re-probes `/tmp/nx_audio_sink`
after startup).

Env contract in `SDLAudioBackend::init()`:
- `NX_AUDIO_RATE` unset (or unparsable, or outside the `[8000, 192000]` sanity range) → `target_rate` falls back to 48000 — byte-identical to the pre-`NX_AUDIO_RATE` hard-coded behavior below.
- `NX_AUDIO_RATE=44100` → opens natively at the Dreamcast's own rate; `needs_resampling` is false, so no `SDL_BuildAudioCVT` build and no per-callback CVT copy.
- Any other value in range → opens at that rate and resamples 44100→`target_rate` via `SDL_BuildAudioCVT`/`SDL_ConvertAudio`, same converter/buffer-sizing/fallback machinery described below.

Root cause of the distortion this rate-picking exists to route around: the
device's ALSA default PCM is `plug` over `dmix`, and `dmix` is fixed at
48000 Hz with no quality `rate_converter` configured. Upstream flycast opens
at 44100 Hz with `allowed_changes=0`; `plug` accepts that request (it'll
happily open at any rate) but then `dmix` forces the real hardware rate, so
`alsa-lib` itself does a low-quality **linear** resample from 44.1→48 kHz on
every frame — audible as constant harsh distortion. This is the identical
class of bug already fixed for mupen64plus (see
`workspace/all/other/mupen64plus/README.md` for that precedent).

The fix: request the sink-preferred rate directly (by default 48000 Hz,
matching `dmix`'s real rate exactly, so `alsa-lib` does no resampling of its
own — or, when the current sink actually prefers 44.1 kHz natively, e.g. a
USB/Bluetooth sink that lists it, skip resampling entirely) and, when the
target isn't 44100, resample the Dreamcast's native 44.1 kHz audio stream to
`target_rate` *ourselves*, in SDL, via `SDL_BuildAudioCVT`/`SDL_ConvertAudio`
— a much higher-quality resampler than alsa-lib's linear one. This is exactly
the `needs_resampling`/`audioCvt` code path flycast already had, just
previously only reachable as a *fallback* (when 44.1 kHz failed to open,
which on this hardware it never does). The resampling machinery itself, and
its buffer sizing (`SAMPLE_COUNT`-based, driven by the device's fixed
per-callback frame count rather than by sample rate), needed no changes.
Native 44.1 kHz remains a hard fallback if the `target_rate` open or
converter build ever fails (that final fallback block is untouched by
`NX_AUDIO_RATE` — it always retries at a literal 44100).

The log lines that report which path was actually taken
(`"SDL2: Using resampling to %d Hz"` / `"SDL2: Using native 44.1 KHz output
(sink-preferred)"` / `"SDL2: SDL_OpenAudioDevice (%d Hz) failed: %s"`, plus
the unconditional-fallback block's own `"SDL2: Using native 44.1 KHz
output"` / `"SDL2: SDL_OpenAudioDevice failed: %s"`) use `NOTICE_LOG`, not
`INFO_LOG` — `INFO_LOG` calls are dead-code-eliminated entirely in Release
builds (`MAX_LOGLEVEL` in `core/log/Log.h` resolves to `LWARNING` when
`NDEBUG` is defined, and `LINFO` calls are stripped by the compiler as
unreachable), so they'd never actually appear in `$LOGS_PATH/DC.txt`.
`NOTICE_LOG` survives Release builds and matches the codebase's own
convention for this kind of message (e.g. `core/emulator.cpp`'s `"Forcing
real BIOS"`/`"Forcing HLE BIOS"`).

## RetroAchievements

Credentials are synced from NxRedux's own RA login on every launch — there is
no in-app flycast RA login:

```sh
RA_USER=$(sed -n 's/^raUsername=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
RA_TOKEN=$(sed -n 's/^raToken=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
```

If both are present, `emu.cfg`'s `[achievements] UserName`/`Token` are
rewritten (so logging into RA elsewhere in NxRedux takes effect on the next DC
launch automatically). If either is missing, `[achievements] Enabled` is
instead forced to `no` — with no on-device flycast login path, a stale or
absent credential shouldn't silently sit at `Enabled = yes` with nothing able
to authenticate it. `Enabled` is otherwise a separate in-app/overlay toggle
(see `overlay_settings.json` below), untouched by the sync except in that
no-credentials branch.

Note the sed patterns match `=.*` (no trailing space before `.*`), not
`= .*` — the shipped cfg's blank values (`UserName =`, nothing after) have no
space to match against `= .*`. This was a real bug caught by Task 6's
mandated sed sanity check (all three dialects: BSD sed, GNU sed, BusyBox sed)
and fixed before shipping; the brief's original pattern would have silently
left RA credentials empty on every fresh install.

**HardcoreMode:** flycast disables save states outright while RA Hardcore
Mode is active — upstream behavior, not something this pak adds or can route
around. The overlay's Save/Load actions will fail silently in that mode.
Accepted as v1 behavior.

## Netplay (GGPO + DCNet)

Flycast v2.6 ships two unrelated online mechanisms; both are compiled into
our binaries already (GGPO builds unconditionally under `if(NOT LIBRETRO)`,
`CMakeLists.txt`; DCNet is `core/network/dcnet.cpp`). No patch changes were
needed for either — flycast's own netplay code is used entirely as upstream
built it.

**Entry point (2026-07-29 rewrite):** GGPO netplay is no longer a persistent
overlay toggle. It's a per-launch choice: press `Y` on a netplay-capable ROM
in the game list (Phase 1 ships the capability marker only in DC.pak), or
use the "Launch with Netplay" context-menu item — both do the same thing,
the menu item just being the discoverable path. A plain `A` press on the
same ROM is always an ordinary single-player launch: GGPO is never armed by
accident and never outlives a quit. (Root Search moved from `Y` to `START`
to make room for this.) Everything interactive — role, hotspot/WiFi, peer
discovery, save sync — now lives in a new standalone wizard, `netplay.elf`
(`workspace/all/netplay-wizard/`), that `DC.pak/launch.sh` runs before
flycast starts. Design:
`docs/superpowers/specs/2026-07-29-netplay-prelaunch-wizard-design.md`.

### The wizard flow

```
netplay.elf --game <name> [--serve-dir <dir>] [--fetch-to <dir>]
            [--fetch-files <pat,pat>] [--session-file <path>]
netplay.elf --cleanup [--session-file <path>]
```

Screens: Host/Join → connection mode (Hotspot/WiFi) → network setup →
rendezvous → handshake → optional save sync → "Go" (writes
`/tmp/netplay_session`, exits 0). `B` backs out one screen at a time;
backing out after network setup has already started tears that setup back
down first — hotspot stopped, previous WiFi restored, rsyncd stopped —
before the wizard exits. There is no path from a cancelled wizard into a
fallback single-player launch anywhere, including inside `launch.sh`. Exit
codes: `0` proceed with netplay, `1` user cancelled, `2` error (shown on the
wizard's own screen for anything that happens once the UI is up; a CLI
usage error — e.g. a missing `--game` — returns 2 *before* `GFX_init`, with
only a stderr message and no screen, though this is practically
unreachable since `launch.sh` always passes `--game`); `launch.sh` treats
anything non-zero as "return to the game list, never start flycast".

Ports, all distinct from minarch's own in-game netplay (55435-55438,
56400/56421) so the two can coexist during the migration described in
`DEV_TODO.md`:

- **TCP 55440** — wizard control channel (`HELLO`/`REJECT`/`SYNC-READY`/
  `SYNC-DONE`/`START`, line-based).
- **UDP 55441** — discovery broadcast, magic `NXWZ` (`0x4E58575A`), sent
  once a second while a host waits. A client only ever lists hosts
  broadcasting the *identical* game name, so wrong-game pairing is
  impossible by construction rather than a stall to diagnose after the
  fact. The `HELLO` handshake re-checks the game name and protocol version
  anyway, but only the CLIENT ever sees a mismatch: a rejected client shows
  the named reason and falls back to its host list (WiFi) or exits the
  wizard (hotspot has no other host to fall back to). The HOST shows no
  error at all — it sends `REJECT` and simply keeps showing "Waiting for
  player...", by design (`wizard_net.c`: "rejecting is not an error here —
  the host keeps waiting for the peer it is actually paired with").
- **TCP 18731** — the save-sync rsync daemon below. Deliberately not
  Device Sync's 18730, so the two features' daemons never collide.

### Save sync: rsync daemon, replacing tar + a per-platform HTTP server

The host starts a read-only rsync daemon for the length of the handshake
only (config written to `/tmp/netplay_rsyncd.conf`: one module named `sync`
mapped to `--serve-dir`, `read only = true`, `hosts allow = <client ip>`,
`list = false`, port 18731) and sends `SYNC-READY`; the client pulls each
`--fetch-files` pattern with `rsync rsync://<host>:18731/sync/<pattern>`
into a staging directory inside `--fetch-to`, showing a progress bar parsed
from `--info=progress2`. Only once the WHOLE requested set has landed does
the client rename it into `--fetch-to` proper (same filesystem, so each
commit is an atomic `rename()`) — a failure partway removes the staging
directory instead of leaving a partial card in place, which is what the old
five-condition tar guard existed to prevent. The daemon is stopped the
moment `SYNC-DONE` comes back, so it lives seconds, not the length of the
match. Filenames are treated as data end to end — whitelisted to
`[A-Za-z0-9._-]+` on both the wire protocol and the rsyncd module, never
shell-interpolated — which is what now does the anti-traversal /
anti-shell-metacharacter job the old tar guard did.

### Virtual config — nothing persists to `emu.cfg`

`launch.sh` starts flycast with every netplay value as a **virtual**
`-config`, `GGPO`/`ActAsServer`/`server`/`EnableUPnP`, plus (now
unconditionally) `input:device2`:

```sh
-config network:GGPO=yes -config network:ActAsServer=$NX_AS_SERVER \
-config network:server=$NETPLAY_PEER_IP -config network:EnableUPnP=no \
-config input:device2=0
```

flycast's `get_entry()` prefers a virtual value over the one in the file,
and `ConfigFile::save()` never writes a virtual value back (`cl.cpp` →
`cfgSetVirtual`, `ini.cpp`) — the same mechanism the old flow's B-skip
already relied on for one value, now used for the whole netplay path.
`emu.cfg` is never touched by a netplay run. `GGPODelay` and `DCNet`
remain real, persisted overlay keys — ordinary gameplay settings a player
wants to keep between sessions — while `GGPO` and `ActAsServer` were
removed as overlay items entirely (`overlay_settings.json`'s `Netplay`
section now ships only `GGPODelay` + `DCNet`): there is no in-game toggle
for netplay any more.

**Migration guard** (idempotent, runs on every DC launch before the netplay
gate): if `[network] GGPO` is `yes`/`True`, set it to `no`; if
`[input] device2` is `0`, set it to `10`. This is what makes a plain launch
on a card that ran the old overlay-toggle flow immediately sane again — no
90 s peer wait on what the player intends as a single-player session.
Known, accepted ambiguity: a user who hand-set `device2 = 0` in flycast's
own Controls UI for local two-pad play has it reverted the one time the
old value is still on disk; the new flow never writes `device2` back to
`emu.cfg` at all, so it cannot recur after that. A stale `server =` (or
`EnableUPnP = no`) left over from an old session is harmless and
deliberately not swept — flycast only reads it on the GGPO path, where the
wizard's virtual value overrides it anyway.

### Cleanup and self-heal

`launch.sh` calls `netplay.elf --cleanup` after `wait $EMU_PID`, gated on
the session file's presence: stop any stray rsyncd, tear down a hotspot AP
and restore the WiFi network that was active before it (persisted as
`NETPLAY_PREV_SSID` in the session file, since a fresh `--cleanup` process
can't inherit `wifi_direct.c`'s in-memory previous-network state across
processes), then remove the session file. Bounded to a 19 s budget
(`WIZ_TEARDOWN_BUDGET_MS`, `wizard.c`) even against a slow supplicant
reconnect. The wizard also runs this same teardown defensively at its own
startup whenever a stale session file exists — a pak killed mid-session
(an OSD/power quit bypasses `launch.sh`'s own exit block entirely) is
healed the next time netplay is attempted, rather than left as an orphaned
AP indefinitely.

### What this replaced, and why (superseded 2026-07-29)

The previous flow was a shell implementation of everything above, built
2026-07-28/29 and largely never pair-tested. Every piece below maps to
something in the wizard that is both simpler and already proven in-repo.
Full mechanics and the reasoning behind them (still useful for
archaeology) live in `docs/superpowers/specs/2026-07-28-netplay-state-sync-design.md`,
`2026-07-28-netplay-discovery-feedback-design.md`, and
`2026-07-29-netplay-cancel-discovery-design.md`.

- **Ping-sweep discovery** (`nx_find_peer`: sweep the local /24, dedup by
  MAC, probe `ip neigh` concurrently, bounded by a 90 s deadline) → UDP
  broadcast reaches the peer directly. The ARP-storm bug class this fought
  (a router answering ARP for every unused address turned 22 neighbours
  into 259 across one sweep on a home LAN) is structurally gone, not
  merely patched around.
- **Tar over a platform-divergent HTTP server** (busybox `httpd` on
  tg5040, `python3 -u -m http.server` on tg5050 because its busybox 1.35
  has no `httpd` applet) plus five hand-rolled tar-slip guards → the rsync
  daemon above, one implementation on both platforms.
- **`show2.elf`'s FIFO-driven status screen** (`TEXT:`/`PROGRESS:` over
  `/tmp/show2.fifo`, racing its own init against launch.sh's writes) → the
  wizard's own real screens — a host/join menu, a live host list, a
  progress bar, named errors instead of a message that may or may not
  have been queued in time. `show2.elf` is no longer part of netplay at
  all; it remains only the first-boot install splash.
- **Pressing B mid-discovery to fall through to a single-player launch for
  that run only** → `B` now backs the wizard out entirely (tearing down
  anything already set up) and returns to the game list; there is no
  fallback path into a peerless GGPO launch any more.
- **The `js0` hexdump B-cancel reader** (`dd bs=8 count=1 <&3 | hexdump`,
  one child process per poll) → ordinary SDL input handling inside the
  wizard, the same as every other NxRedux screen.

### flycast internals unaffected by the rewrite (still true)

None of this changed — it's how flycast itself behaves, independent of
what drives it:

- The handshake exchanges MD5s of **BIOS + game + flash/VMU state**
  (`ggpo.cpp:537`) and fails with "Peer verification failed" on mismatch —
  which is why the host still plays on its real VMU/flash ("host brings
  the memory card") and the client plays on a synced, isolated copy rather
  than its own. Only `dc_boot.bin` and the CHD must still be
  byte-identical on both cards by hand — BIOS and game images are never
  synced.
- An empty `server =` makes flycast target loopback on `localPort ^ 1`
  (`ggpo.cpp:579-597,808-811`) and wait forever on "Starting Network",
  which has no timeout — the modal's Cancel button is the only way out
  (`gui.cpp:1286`). The wizard always resolves a real peer IP before
  starting flycast, so this should now only be reachable through a truly
  exotic failure.
- GGPO **always drives Dreamcast port B as player 2** — the remote player
  on the host, the *local* player on the client (`ggpo.cpp:500`/`:569` set
  the local player to `localPlayerNum + 1`, and `ggpo.cpp:643-650` writes
  `inputState[player]` straight to the maple port index) — and flycast
  leaves port B empty by default (`option.cpp:197-201`, enum
  `maple_cfg.h:6-18`), so both machines need a pad there or the second
  device's own inputs reach no maple device at all (MvC2's VS mode
  greyed out, the second device's Start doing nothing). **Both peers must
  carry the identical value** or the emulated machines differ and desync
  — which is why `launch.sh` sets it as a virtual value on both sides
  rather than leaving it to a hand edit. A bare controller on port B does
  not perturb the GGPO verification digest — `vmuDigest()`
  (`maple_cfg.cpp:364`) only hashes devices whose `getData()` is
  non-null.
- **UPnP is forced off** because `ggpo.cpp:801-804` runs
  `miniupnp.Init()` + `AddPortMapping(19713/UDP)` **before** the
  `ActAsServer` branch, so both roles would otherwise punch a router
  mapping with an 86400 s lease (`miniupnp.cpp`). `EnableUPnP = no` is one
  of the virtual `-config` values above on every netplay launch now,
  rather than a value `sed`-written into `emu.cfg`; there is still no
  restore-when-off branch, since the value is only ever read on the GGPO
  path.
- **Pairing key is the ROM filename with its extension stripped**
  (`EMU_OVERLAY_GAME`), not CHD content — two byte-identical CHDs stored
  under different filenames will never pair. The wizard's discovery-list
  filtering and `HELLO` mismatch check are both keyed on this same
  string, so keep the game file named identically on both cards.
- **Leave flycast's `PerGameVmu` off.** With it on, the port-A1 VMU file
  is written as `<gameId>_vmu_save_A1.bin` (`oslib.cpp:52-66`) — a name
  the wizard's `--fetch-files` glob (`vmu_save_*.bin`) does not match, any
  more than it matched the old tar glob. The sync then silently ships an
  incomplete card, with "Peer verification failed" the only symptom.
- A shared `<rom basename>.state.net` savestate is still honored by
  flycast when present (auto-loaded at start, its MD5 replaces the
  VMU/flash hash — `emulator.cpp:674`, `ggpo.cpp:541`), independent of the
  wizard's own sync.
- GGPO forces the SH4 clock to stock 200 MHz and, under threaded
  rendering, disables framebuffer emulation (`emulator.cpp:864,983`) —
  automatic.
- GGPO disconnects a peer after 3 s of silence (`ggpo.cpp:566`) — opening
  the overlay menu (pause) or save/load state mid-session will drop or
  desync the match. Both sides launch the same game and sync from boot.
- `GGPOAnalogAxes` must match on both peers (default 0 on both;
  digital-only fighters like MvC2 don't need it).

**DCNet** (`DCNet = yes`, overlay: "DCNet Online") — emulates the DC modem
(or BBA with `EmulateBBA = yes`) and tunnels PPP/Ethernet traffic to the
public `dcnet.flyca.st` relay, which routes to fan-run revival servers. This
is for games with *native* online modes (PSO, ChuChu Rocket, Quake III,
Alien Front Online, …), needs internet (not just LAN), needs no peer
config, and does nothing for games without an online menu (MvC2). Off by
default; independent of GGPO; entirely unaffected by the wizard rewrite.

Existing installs are already `.initialized`, so they never receive a
re-seeded `[network]` block from a fresh `default*.cfg` — that's fine: the
migration guard above is what an upgraded card actually needs (it runs on
every launch, not just first-run), and since the netplay values are all
virtual now, nothing about `emu.cfg` needs to change on an upgraded card at
all. Existing installs still routinely have no `[input]` section (flycast
drops keys left at default when it rewrites the file), but that no longer
matters either, since `device2` is virtual-only on the netplay path.

## Pre-launch options (Emulator Options / Emulator Settings)

Flycast's `emu.cfg` keys can be set **before** a game starts, without opening
the in-game overlay, through two entry points that share one schema. **Per-game
overrides** come from the game-list context menu's "Emulator Options" item —
shown only when a pak beside the ROM's `launch.sh` also ships an `options.sh`
probe. **Global defaults** come from Tools → Emulator Settings, which runs
`options.elf --pick`. Both edit the same shared schema,
`overlay_settings.json` (the very file the in-game Options screen reads, above),
so a key is described in exactly one place. A per-game edit is written to its own
file at
`$SHARED_USERDATA_PATH/DC-flycast/config/<device>/games/<key>.cfg`, where `<key>`
comes from `nx_rom_base()` — a folder-named `.m3u` game collapses to the folder
name. At launch, `launch.sh` turns any present per-game file into virtual
`-config` arguments (`$GAME_ARGS`) placed **before** `$NETPLAY_ARGS`, i.e. the
same last-`-config`-wins mechanism the netplay path above uses. Setting
`EMU_OVERLAY_HIDE_OPTIONS=1` hides the in-game Options menu for that launch.
Design: `docs/superpowers/specs/2026-07-29-dc-prelaunch-options-design.md`.

### Override precedence and the argv contract

- **Override precedence is argv order, not sections.** `launch.sh` runs
  `./flycast $GAME_ARGS $NETPLAY_ARGS "$ROM"`, and flycast's last `-config` for a
  repeated `section:key` wins: `ParseCommandLine()` walks argv left-to-right
  (`core/cfg/cl.cpp`) and each `-config` lands in `cfgSetVirtual()` →
  `ConfigSection::set()`, a plain `std::map` assignment (`core/cfg/ini.cpp`).
  Swapping the two variables silently inverts netplay precedence, so `GAME_ARGS`
  stays first and `NETPLAY_ARGS` last.
- **Both arg variables are deliberately unquoted** — they are word-split argument
  lists whose values are ints/bools with no whitespace, and empty on a normal
  launch. A schema that ever grew a string-valued option would break this; it
  would need a different mechanism, not quoting.
- **Bools are written `True`/`False`, not `yes`/`no`.**
  `emu_ovl_cfg_format_value` speaks flycast's INI dialect and both spellings
  parse fine, so don't "fix" an override file that reads `rend.WideScreen = True`
  — and don't grep argv for `=yes`; the pair to expect there is
  `-config config:rend.WideScreen=True`. The single spaces around `=` in the
  override file ARE load-bearing: `launch.sh`'s awk splits on `-F' = '`, so a
  hand-edited `key=value` line is silently dropped.

### Debugging: argv is the only witness

- **Applied overrides leave NO trace in `$LOGS_PATH/DC.txt` — don't look for
  one.** flycast does log every virtual pair (`Virtual cfg <sec>:<key>=<val>`,
  `core/cfg/cl.cpp:51`) but that call is an `INFO_LOG`, and INFO level is
  compiled **out** of Release builds (`MAX_LOGLEVEL` = `LWARNING` under `NDEBUG`,
  `core/log/Log.h:58-63`; the paks are built `-DCMAKE_BUILD_TYPE=Release`).
  Verified against the shipped binaries: `strings` finds zero `Virtual cfg`
  occurrences in either `DC.pak/flycast`, while NOTICE-level log strings from the
  same builds are present. This is the same trap `flycast.patch:110-112` already
  documents for the audio path. **The witness is argv**, not the log:
  `tr '\0' ' ' < /proc/$(pidof flycast)/cmdline` (or `xargs -0 echo <` the same
  file) shows every `-config` pair and, crucially, their order — the same applies
  to netplay's six pairs. Read `/proc` directly rather than reaching for `ps w`:
  the device's busybox `ps` only honors `w` when built with `FEATURE_PS_WIDE`,
  and without it argv is truncated to the terminal width, silently hiding the
  very `-config` pairs you're looking for.
- **Never run `DC.pak/launch.sh` (or `options.sh`) from a bare adb shell to see
  what path it computes.** Both source `nx_paths.sh`, which builds every path
  from `SHARED_USERDATA_PATH` and branches on `DEVICE` — and those are exported by
  `MinUI.pak/launch.sh` (`:23`, `:52-56`), not by the pak itself. Outside
  nxredux's environment they're empty/unset, so the pak resolves
  `$DEVICE_CONFIG_DIR` to a bogus
  **`/DC-flycast/config/tg5040-smart-pro/…`** on the rootfs (empty prefix,
  `DEVICE` falling through to the smart-pro `else`), happily `mkdir`s it and seeds
  a junk `emu.cfg` there. `SDCARD_PATH` being empty also breaks
  `LD_LIBRARY_PATH`, `EMU_OVERLAY_JSON` and `FLYCAST_BIOS_PATH`, so the run is not
  a faithful launch either. Only the trailing `<key>.cfg` basename would be
  trustworthy from such a trace. If you genuinely need to trace the pak, export
  the environment first — `skeleton/SYSTEM/tg5040/dbg/launch.sh:3-27` is the
  ready-made export list (note it leaves `DEVICE` unset on Smart Pro, where
  `nx_paths.sh`'s `else` happens to land correctly).

### File format and lifecycle edge cases

- **A malformed or unreadable override file is skipped silently** — the game
  launches with globals only, no error anywhere, and (per the debugging note
  above) nothing in the log either way. If an override "doesn't apply", dump
  argv: pair absent → `launch.sh` never parsed the file (wrong filename, or a
  line not matching `key = value`); pair present but no visible effect → flycast
  took the value and something else is wrong. The editor treats a malformed file
  as empty and rewrites it cleanly on the next save.
- **The override key can collide by design.** `nx_rom_base()` maps a folder-m3u
  game to the folder name, so a flat ROM that happens to share that name resolves
  to the same override file. Inherent to the mandated one-file-per-game scheme,
  accepted.
- **Renaming or deleting a ROM orphans its override file** — harmless, and
  deliberately not cleaned up (explicit non-goal). It will silently re-attach if
  a ROM with the old name ever reappears.

### The editor binary (`options.elf`)

- **`options.elf` is a SYSTEM bin on `PATH`, invoked bare** — same shape as
  `netplay.elf`. Pushing a rebuilt copy needs no reboot (it is not
  nextui/minarch), but the editor must not be running at the time.
- **`options.sh`'s stdout must stay uncaptured.** Only stderr is redirected (to
  `$LOGS_PATH/emu-options.txt`); the Emulator Settings tool captures
  `options.elf --pick`'s stdout **directly** and `exec`s the result, so wrapping
  `options.sh` in `$(…)` anywhere would swallow the editor's own output stream.
  This is the same class of bug the tg5050 fancontrol lesson taught: a
  `system("… &")` daemon (the fan-control daemon respawned by `InitSettings()`)
  inherited the picker's `$()` pipe write end and hung the capture, because the
  pipe stays open as long as any process holds its write end. `options.c` now
  parks stdout with `FD_CLOEXEC` (`stdout_park`) for exactly this reason, so a
  daemon a child spawns can never keep the capture pipe open.
- **Editor exit 1 means two different things** — cancel in `--pick` mode,
  config-load failure in the edit modes; a plain usage error exits 2. Callers
  must not conflate the two exit-1 cases (they currently don't: `options.sh`
  ignores the edit-mode code, the tool treats empty stdout as cancel).
- **Emulator Settings exits silently (rc=0) when no pak on the card ships an
  `options.sh`** — an empty Tools app, not an error. Expected on any platform
  other than tg5040/tg5050, where `options.elf` isn't even built.
- The schema's `options_hint` still reads "Restart game to apply changes" —
  correct for the in-game overlay, vacuous in the pre-launch editor (nothing is
  running yet). Cosmetic; left alone so both consumers share one schema file.

## Runtime libraries

**Nothing is shipped in the pak.** Verified on the Brick (tg5040) via
`LD_TRACE_LOADED_OBJECTS=1` (the device's own `ldd` shim) run with
`launch.sh`'s exact `LD_LIBRARY_PATH` — every `NEEDED` entry the binary has
resolved against the firmware's own copies, zero `not found`:

| Library | Resolves from (Brick / tg5040) |
|---|---|
| `libSDL2-2.0.so.0`, `libSDL2_ttf-2.0.so.0`, `libSDL2_image-2.0.so.0` | `/usr/trimui/lib` |
| `libGLESv2.so.2`, `libcurl.so.4`, `libssl.so.1.1`, `libcrypto.so.1.1` | `/usr/lib64` |
| `libzip.so.5` | `$SDCARD_PATH/.system/lib` |

The firmware's own `libcurl.so.4` is itself SSL/HTTP2-enabled (pulls in
`libnghttp2`, `libssl.so.1.1`, `libcrypto.so.1.1` transitively) — RetroAchievements
HTTPS, boxart downloads, and the update checker all work out of the box.
None of the cross-built `curl-prebuilt-aarch64-{tg5040,tg5050}/` or
`openssl-prebuilt-aarch64-tg5050/` libraries from the Build section above are
shipped — they exist solely to satisfy the **cross-compiler's** link-time
`find_package(CURL REQUIRED)`, and stay gitignored dev-only artifacts.

**tg5050's equivalent library-resolution check is done too.** A later full
on-device session on the Smart Pro S (tg5050) re-ran the same
`LD_TRACE_LOADED_OBJECTS=1` check against `launch.sh`'s exact
`LD_LIBRARY_PATH`: every `NEEDED` entry resolved against that firmware's own
copies as well, zero `not found`. Both physical target devices are now fully
verified for runtime library resolution.

## Overlay options (`overlay_settings.json`)

`skeleton/BASE/Emus/shared/flycast/overlay_settings.json` drives the
in-game Options screen (same schema `workspace/all/common/emu_overlay_cfg.c`
already reads for N64.pak):

| Section | Key | Type | Values | Notes |
|---|---|---|---|---|
| Video (`[config]`) | `rend.WideScreen` | bool | — | 16:9 instead of native 4:3 |
| | `rend.WidescreenGameHacks` | bool | — | game-specific widescreen hacks; needs Widescreen on |
| | `rend.Resolution` | cycle | 480 / 640 / 720 | 480 is native |
| | `pvr.AutoSkipFrame` | cycle | Off / Normal / Maximum | **default `1` (Normal)** — a deliberate handheld-performance deviation from flycast's own compiled default (`0`, off); safe because the default cfg always seeds this key explicitly, so flycast never falls back to its own default here |
| | `rend.vsync` | bool | — | sync presentation to the display |
| RetroAchievements (`[achievements]`) | `Enabled` | bool | — | requires a NxRedux RA login (see above) |
| | `HardcoreMode` | bool | — | disables save states while active (flycast behavior, not overridable here) |

Options apply on the **next launch** (`options_hint: "Restart game to apply
changes"` in the JSON), not live — same convention N64.pak uses. Any other
`emu.cfg` key not listed here — e.g. `Dynarec.Enabled`,
`Dreamcast.AutoLoadState`/`AutoSaveState` (separate from the overlay's own
slot-based save/load, both `no` by default), `Dreamcast.Language`/`Region`/
`Cable`, `rend.ThreadedRendering`, `rend.PerStripSorting`,
`rend.DelayFrameSwapping`, `ta.skip`, `aica.DSPEnabled`/`NoBatch`,
`[window] fullscreen`/`width`/`height` (see `DC.pak/default-brick.cfg` for
the full set) — is hand-editable directly in
`$SHARED_USERDATA_PATH/DC-flycast/config/<model>/flycast/emu.cfg`; it's just
not exposed in the overlay's Options screen. (`UseReios` is excluded from
this list since `launch.sh` manages it automatically every launch — see
"BIOS" above; hand-editing it will be overwritten on the next launch.)

## Controller mapping — SHIPPED

A curated `SDL_Xbox 360 Controller.cfg` mapping file ships in both platforms'
pak trees — `skeleton/SYSTEM/tg5040/paks/Emus/DC.pak/SDL_Xbox 360 Controller.cfg`
and `skeleton/SYSTEM/tg5050/paks/Emus/DC.pak/SDL_Xbox 360 Controller.cfg`
(byte-identical; kept per-platform because everything else in each pak
directory is per-platform too) — and `launch.sh` installs it on first launch:

```sh
if [ ! -f "$DEVICE_CONFIG_DIR/flycast/mappings/SDL_Xbox 360 Controller.cfg" ]; then
    cp "$PAK_DIR/SDL_Xbox 360 Controller.cfg" "$DEVICE_CONFIG_DIR/flycast/mappings/" 2>/dev/null || true
fi
```

Deliberately **not** gated by the same `.initialized` marker that seeds
`emu.cfg`: that marker only tracks the config seed, so an install upgraded
from an older pak version (already `.initialized`, but never given a
mapping) would otherwise never receive one. Install-if-absent runs on every
launch instead, and never overwrites a mapping the user has since customized
via flycast's own Controls UI.

The joystick enumerates to SDL as **`Xbox 360 Controller`**
(GUID `0300a3845e0400008e02000014010000`, VID `045e` PID `028e` — the Brick's
built-in gamepad spoofing an Xbox 360 controller), so the installed filename
matches exactly what `GamepadDevice::save_mapping()` would itself write
(`make_mapping_filename()` = `api_name() + "_" + name()`).

**Curation:**
- **Face buttons are label-accurate, not positional** — the physical pad's
  A/B and X/Y pairs are swapped relative to flycast's own positional default,
  per user preference, so the label printed on the physical button matches
  the DC input it triggers rather than matching physical slot position.
- D-pad → DC d-pad, left stick → DC analog stick, L2/R2 axes → DC analog
  triggers — straightforward, no surprises.
- **L1/R1 → `btn_z`/`btn_c`** — a deliberate deviation from the task brief's
  original "leave unbound" plan. User-verified as harmless for console
  (non-arcade) games, since `btn_z`/`btn_c` are VMU/arcade-panel buttons the
  console control scheme doesn't use. Noted here as a known deviation, not a
  bug.
- **Deliberately no binds** for `btn_menu`, `btn_escape`, `btn_jump_state`,
  `btn_quick_save`, or `btn_fforward` — those would fight the overlay's own
  MENU/Save/Load/Quit flow (see "Patch: what `flycast.patch` does" above),
  which owns those actions instead.

**Warning:** a player who remaps controls via flycast's own native Controls
UI could rebind `btn_escape` to a physical button. Unlike the overlay's Quit
action (which auto-saves to hidden slot 9 before exiting — see "Save-state
model" above), `EMU_BTN_ESCAPE` calls bare `dc_exit()` directly
(`core/input/gamepad_device.cpp` — this path is untouched by `flycast.patch`).
A rebind that adds a `btn_escape` bind would let a player quit **without**
the slot-9 auto-save, breaking Game Switcher resume for that session.
