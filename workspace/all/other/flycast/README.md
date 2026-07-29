# flycast (Sega Dreamcast) — DC.pak

Standalone flycast pinned at v2.6 (`392a429e`), patched with the shared NextUI
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
files) carries the NextUI in-game overlay integration plus one compiler
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
adb push skeleton/EXTRAS/Emus/tg5040/DC.pak /mnt/SDCARD/Emus/tg5040/
adb push skeleton/EXTRAS/Emus/shared/flycast /mnt/SDCARD/Emus/shared/
adb shell chmod +x /mnt/SDCARD/Emus/tg5040/DC.pak/launch.sh /mnt/SDCARD/Emus/tg5040/DC.pak/flycast
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
- **Slot 8** — the launcher's fresh-start sentinel. NextUI writes `8` to
  `/tmp/resume_slot.txt` on a non-resume ("A START") launch; the overlay's
  one-shot resume check ignores it — never saved to or loaded from
  automatically.
- **Slot 9** — hidden auto-save-on-quit (`EMU_OVL_AUTO_SLOT`). Choosing Quit
  from the overlay auto-saves to slot 9 plus a Game Switcher screenshot,
  before calling `dc_exit()`. NextUI writes `9` on a Game Switcher RESUME
  launch, and the overlay's one-shot resume check loads it. Net effect:
  **every quit is Game-Switcher-resumable** without the player needing to
  remember to save first — the same standalone-resume convention already
  shipped for N64.pak (mupen64plus + GLideN64), consuming the identical
  `/tmp/resume_slot.txt` handshake nextui writes on every launch.

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

Credentials are synced from NextUI's own RA login on every launch — there is
no in-app flycast RA login:

```sh
RA_USER=$(sed -n 's/^raUsername=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
RA_TOKEN=$(sed -n 's/^raToken=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
```

If both are present, `emu.cfg`'s `[achievements] UserName`/`Token` are
rewritten (so logging into RA elsewhere in NextUI takes effect on the next DC
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
needed — the overlay's `Netplay` section (`overlay_settings.json`) plus the
seeded `[network]` block in the `default-*.cfg` files are the entire
integration.

**GGPO** — rollback netplay between two flycast instances running the same
game (Dreamcast console games included — `ggpo.cpp` has an explicit console
path). Peers sync inputs over **UDP 19713** and re-simulate on rollback.
Config lives in `emu.cfg` `[network]`:

```ini
[network]
GGPO = yes          # overlay: "GGPO Netplay"
ActAsServer = yes   # host/P1 on one device, no (join/P2) on the other
GGPODelay = 0       # overlay: "Input Delay" — 2-3 trades latency for fewer rollback hitches
server = 192.168.x.x  # auto-written by launch.sh discovery; manual fallback only
EnableUPnP = no     # forced by launch.sh on every GGPO launch (see below)
```

Constraints, all verified in source:
- The handshake exchanges MD5s of **BIOS + game + flash/VMU state**
  (`ggpo.cpp:537`) and fails with "Peer verification failed" on mismatch.
  launch.sh automates the state match ("host brings the memory card"):
  the host plays on its **real** VMU/flash and serves a tar of exactly
  the hashed files (`vmu_save_*.bin` + `dc_nvmem.bin`, hashed per
  `maple_cfg.cpp:381` / `nvmem.cpp:267`) over a small HTTP server on
  **TCP 19714** — busybox `httpd` where the firmware provides it
  (tg5040), `python3 -u -m http.server` on tg5050 (its busybox 1.35
  lacks the httpd applet), fail-open if neither exists; the client
  pulls it into an isolated `netplay-data/` dir
  (`XDG_DATA_HOME` override) and plays on the borrowed copy, so the
  client's real saves are never touched and whoever hosts contributes
  their own unlocks. The host snapshots its card to `netplay-backup/`
  (one-deep) before each session. Only `dc_boot.bin` and the CHD must
  still be byte-identical on both cards by hand — BIOS and game images
  are never synced.
- IP setup is automatic too, and **symmetric**. GGPO is peer-to-peer, so
  each side has to know the other's address (`ggpo.cpp:579-597`): an
  empty `server =` makes flycast target loopback on `localPort ^ 1` —
  `127.0.0.1:19712` from the host, `127.0.0.1:19713` from the client
  (`ggpo.cpp:584-590`, `:808-811`) — and wait
  forever on "Starting Network", which has no timeout — the modal's
  Cancel button is the only way out (`gui.cpp:1286`). So **both** devices
  serve a two-line `netplay-info` on **TCP 19714** (line 1 the ROM
  filename with its extension stripped, line 2 the role, `host` or
  `client`), and **both** scan the
  LAN for a peer serving the same ROM in the *opposite* role: the ROM
  line prevents cross-game pairing, the role line prevents two hosts (or
  two clients) from pairing. Each side tries the stored `[network]
  server =` first, but **only when the device holds a /24 lease and that
  address falls inside it**. Two things follow. After moving to a
  different network the stored address belongs to the old one and can
  never answer, so it is skipped instead of burning ~3 s on a dead
  probe. And on a **non-/24 lease** the subnet is never parsed at all —
  the address is read with `sed -n 's|.*inet \([0-9.]*\)/24 .*|\1|p'`,
  which matches nothing on a /16 or /23 — so there the stored address is
  skipped *unconditionally*, not merely when the octets differ. A repeat
  session on the same /24 with an unchanged DHCP lease still pairs
  almost instantly. Otherwise it ping-sweeps its own /24 and probes the
  `ip neigh` neighbors **concurrently** (one background probe per
  candidate, results collected through a hits dir), so a pass costs a
  fixed ~7 s — a 3 s settle after the sweep plus a 4 s collect, extended
  only by the slowest straggler probe, each of which is bounded by the
  fetch watchdog at 3 s — rather than ~1 s per reachable host. Scan time
  no longer scales with how busy the LAN is.

  Two details here are load-bearing, and the first real pair test failed
  because neither was in place. Candidates are **deduplicated by MAC**
  before probing: the sweep itself leaves an ARP entry for every address
  it touched, and a router that answers ARP for unused addresses turns
  that into hundreds of phantom neighbours sharing a handful of MACs
  (measured on a home LAN: 22 entries before a sweep, 259 after, ~19 real
  MACs). Probing all of them swamps a 4-core handheld and nothing
  finishes inside the collect window. And `nx_fetch` names each download
  with `mktemp` rather than the pid: in POSIX sh `$$` keeps the *parent*
  shell's pid inside a subshell, so a pid-derived name is literally the
  same file in every concurrent probe — one probe's cleanup deletes
  another's payload, and a dead host can `cat` a live host's answer and
  win the selection. Concurrency is what made both of these reachable;
  neither could bite while probes ran serially. All of it is bounded by a
  90 s wall-clock deadline. Whoever resolves a peer writes its IP back
  to `server =`, which stays a manual fallback
  for LANs where discovery fails — with one caveat on a non-/24 lease,
  where the fast path never probes it: a hand-set `server =` is then
  reached only if that host happens to sit in the `ip neigh` table, and
  nothing sweeps to populate that table off a /24. flycast still reads
  the hand-set value and can connect on it (launch.sh leaves it alone
  when discovery finds nothing), but the client will not have pulled the
  save tar — the GGPO connection and the state sync part ways there.
  **Launch order does not matter** —
  either device may start first, with any gap up to the deadline. On top
  of this shared discovery, the host additionally serves the VMU/flash
  tar and the client additionally pulls it. Every failure path fails open
  into a normal launch so GGPO surfaces errors visibly.
- A discovery long enough to be worth covering is no longer a silent
  black screen. (A fast one still is, and correctly so — see the end of
  this bullet.) While the netplay
  block runs, launch.sh drives the already-shipped `show2.elf` in daemon
  mode — resolved bare via `PATH`, exactly like the scripts' other helper
  binaries, so nothing new was built. The opening "starting" message is
  the daemon's own `--text=` argument (the FIFO does not exist yet at
  that point); from there on launch.sh pushes `TEXT:` lines over
  `/tmp/show2.fifo` at each stage: preparing saves (host, `TEXT:` only —
  it carries no `PROGRESS:`, so no percentage is left standing in front
  of the long wait that follows), looking for the peer — the host names
  the `<subnet>.x` it is scanning, or "the network" when it holds no /24
  lease to name — then peer found at `<ip>`, which on
  the client is a **single** combined `host found at <ip> - copying
  saves...` message rather than two updates (plain ASCII hyphen — a
  tester matches these against the screen), or "starting anyway" when
  nothing turned up. `PROGRESS:` rides along only where it means
  something: `-1` (the marquee again) for the open-ended search, a real
  percentage once a peer is resolved or the search has given up.
  Sent is not the same as seen. Which message is on screen at any moment
  is not something the design pins down: each write is *queued* from its
  own background subshell polling for the FIFO on an independent 1 s
  cadence, so any of the messages racing show2's init may end up the
  survivor; and the terminal messages are followed immediately by a
  `killall -9` that destroys the daemon, leaving them a frame at most. It
  follows that the screen will ordinarily disappear still showing a
  search message. The client's "copying saves" is the one update that is
  reliably readable, because the tar fetch follows it — ~1-2 s for a
  ~384 KB card over LAN (derived), the 30 s in `nx_fetch` being only the
  watchdog ceiling for a host that stalls. Read the screen as evidence
  that discovery is running, not as a progress transcript. And note the
  corollary: on the repeat-session fast path the stored-`server =` probe
  resolves in about a second, so the whole block can finish about as fast
  as show2 comes up — expect a brief flash, or nothing at all. Either is
  the feature working: there was barely a wait to cover. The screen
  takes exactly **one** input — B skips netplay (next bullet), which is
  why a second line sits under the status text — and is otherwise
  informational: nothing else on it is interactive and it has no other
  effect on what discovery does. It runs with
  `--image=/dev/null` (there is no netplay artwork — the repo's existing
  "no logo" idiom) and `--progress=-1`, show2's indeterminate marquee,
  because the wait has no knowable length. `/dev/null` is accepted at
  runtime: `IMG_Load` logs "not a regular file or pipe" and rendering
  carries on (measured, Brick, 2026-07-29). show2 paints nothing until
  its SDL2/DRM init finishes, and only creates its FIFO after that
  (`mkfifo` is the first thing `runDaemonMode()` does after unlinking any
  stale node) — **measured at ≤1 s** on tg5040 over three cold runs, so
  only the first moment of a netplay launch is dark. Teardown must use
  `killall -9`: show2 handles SIGINT only (`show2.cpp:623`) and `SDL_Init`
  traps SIGTERM into an `SDL_QUIT` event its daemon loop never pumps, so
  a plain `killall` leaves it running (measured alive 6 s later) and
  holding the display against flycast. Everything
  degrades silently: no `show2.elf` on `PATH`, or a daemon that died,
  means no status screen and an otherwise-normal launch — exactly the
  old silent behavior — and every FIFO write happens inside a background
  subshell so a half-dead daemon can never stall the launch. Verified on
  hardware at the function level only (start / update / stop / no-op when
  absent, Brick 2026-07-29; the deployed tg5050 binary separately shown
  to start, create its FIFO in ~1 s and take `TEXT:`/`PROGRESS:`
  updates). No one has yet watched it driven by a real game launch on
  either device.
- **Press B during discovery to skip netplay — for that run only.** The
  status screen's second line reads `B = skip netplay`, and pressing B
  stops the search and launches the game single-player. The window is
  the whole netplay block: `nx_cancel_arm` runs immediately after the
  screen is started, before any network work, and the flag survives
  until `nx_cancel_disarm` runs on the far side of the block, just
  before flycast is started. It is checked at every phase boundary —
  after the host has snapshotted and served its VMU tar, after the
  client has published its identity, on entry to each discovery pass
  and before each neighbour probe — and *inside* the network waits as
  well: `nx_fetch`'s own watchdog polls it once a second, so a press
  during the client's tar download (watchdog ceiling 30 s) is acted on
  within about a second instead of after the download finishes. "Skip"
  means **this run only**. launch.sh starts flycast with `-config
  network:GGPO=no`; flycast stores a `-config` value as a **virtual**
  setting — `get_entry()` prefers virtual values and `ConfigFile::save()`
  never writes them (`cl.cpp` → `cfgSetVirtual`, `ini.cpp`) — so
  `emu.cfg` is left untouched: `[network] GGPO` is still `yes`
  afterwards and the next launch attempts netplay again. A cancelled
  client also skips the `XDG_DATA_HOME` redirect, so it plays on its
  **own** VMU rather than the host's borrowed copy (with GGPO off there
  is nothing to isolate; its real saves were never at risk either way).
  The screen shows `Skipped - starting game...` for a beat first — the
  one-second pause on the cancel path exists only so that message can
  be painted at all, since `nx_ui` merely *queues* its write and
  `nx_ui_stop` kills the daemon two lines later; a press early enough to
  precede show2's FIFO still cancels, it just cannot be confirmed on
  screen. One residual: `[input] device2 = 0` is written unconditionally
  at the top of the block and no cancel path reverts it, so it stays
  until a launch with GGPO **off** puts it back to `10`.
- How the press is read, and what the design deliberately leaves
  behind. The hint line is show2's new **optional `--hint`** argument: a
  second line drawn under the status text in `renderSimple()` — which
  `renderProgress()` calls, so both display modes get it — and skipped
  entirely when the argument is absent, so the first-boot install
  splash, which is the same binary, renders exactly as before. B is js0
  event **number `00`**, measured on **both** units — five consecutive
  presses on each, every one type `01` number `00` with a non-zero
  value, the Smart Pro S reading identical to the Brick — which agrees
  with this pak's own `SDL_Xbox 360 Controller.cfg` (`bind0 =
  0:btn_b`). So the one shared constant is right for both and the
  byte-identical block needs no per-device handling. An earlier reading
  of `02` came from a block-buffered reader that misattributed delayed
  events and must not be restored.
  The reader opens `/dev/input/js0` **once** on fd 3 and then runs one
  short-lived `dd bs=8 count=1 <&3 | hexdump` per event, so hexdump
  exits — and therefore flushes — every time. Both obvious alternatives
  were tried on-device and fail: a persistent `dd | hexdump | while
  read` pipeline block-buffers (~4 KB, ~170 events) and recorded zero
  presses across a 45 s capture, and a `count=1` loop that re-opens the
  device replays joydev's init burst on every pass and spins. Nothing
  but a real press can trip it: the init burst is type 81/82 with value
  0, releases carry value 0, and stick motion is type 02. If js0 is
  unreadable the watcher exits at once and cancel is simply unavailable
  — every other path is unchanged. Killing the watcher does **not**
  reap its `dd`/`hexdump` children, and that leak is accepted on
  purpose. It clears itself: the `dd` has `count=1`, so the first
  controller event after the game starts ends it, hexdump then EOFs, and
  joydev gives each opener an independent event buffer so nothing is
  taken from flycast meanwhile. Both sweeps that would clean it up are
  wrong — matching `/proc/<pid>/cmdline` cannot work (the device is a
  redirection, never an argument; verified on-device to find none of the
  holders), and matching open descriptors does find them all but "kill
  whatever holds js0" would also hit `keymon.elf`, which opens input
  devices for the whole session, or the launcher, and killing the
  launcher powers this device off.
- What has actually been tested (2026-07-29) — bench level, both
  devices. The shipped helpers pass 12/12 checks on the Brick, run
  against the functions extracted verbatim from launch.sh: arm/disarm
  lifecycle, a stale flag cleared by `nx_cancel_arm`, the flag honoured,
  and `nx_fetch` returning empty 2 s into a 30 s timeout when the flag
  appears (the check that proves a press during the tar download is
  acted on promptly). The rebuilt show2 was watched on hardware
  rendering two lines, with the hint persisting across `TEXT:` updates
  and the marquee animating, freezing on a numeric `PROGRESS:` and
  re-arming on a later `PROGRESS:-1`; the no-`--hint` install-splash
  invocation was unaffected. Both rebuilt binaries are now **deployed**
  (Brick md5 `2678718c` → `4be3ac43`, Smart Pro S `fe7d510b` →
  `c943d4b7`, each md5-verified against the local build, and `show2.elf`
  resolves bare via `PATH` on both); the pre-change binary is kept
  on-device as `show2.elf.orig` beside it, so rolling the shared install
  splash back needs no computer. The deployed tg5050 copy was then run
  with `--hint` on the Smart Pro S — a platform that had never run show2
  at all — and starts, creates its FIFO in ~1 s, accepts `TEXT:` and
  `PROGRESS:` updates and stays alive. **Not** tested, all three needing
  a real session: an actual B press during a real game launch — that it
  cancels, lands in single-player and leaves `[network] GGPO = yes`
  afterwards — which cannot be driven over adb, since input cannot be
  injected; any two-device pair test with the new block on both units;
  and the cancelled-client-plays-on-its-own-saves path, which is
  code-verified and reasoned but never exercised in a session.
- Netplay launches also plug a controller into Dreamcast **port B**:
  launch.sh sets `[input] device2 = 0` (`MDT_SegaController`) while GGPO
  is on, automatically on both devices, and puts it back to `10`
  (`MDT_None`) when GGPO is off, so single-player sessions see the stock
  one-pad Dreamcast. GGPO **always drives port B as player 2** — the
  remote player on the host, the *local* player on the client
  (`ggpo.cpp:500`/`:569` set the local player to `localPlayerNum + 1`,
  and `ggpo.cpp:643-650` writes `inputState[player]` straight to the
  maple port index) — so both machines need a pad there. flycast leaves
  port B empty by default (`option.cpp:197-201`, enum
  `maple_cfg.h:6-18`), so those inputs reached no maple device at all —
  MvC2's VS mode stayed greyed out and the second device's Start did
  nothing (that device's *own* player is port B). **Both
  peers must carry the identical value** or the emulated machines differ
  and desync; that is why launch.sh owns the key on both sides rather
  than leaving it to a hand edit. A bare controller on port B does not
  perturb the GGPO verification digest — `vmuDigest()`
  (`maple_cfg.cpp:364`) only hashes devices whose `getData()` is
  non-null.
- Security posture: the rendezvous is deliberately unauthenticated
  (home-LAN scope). While netplay is on, the serve dir is readable by
  any device on the network for the session's duration — on the host
  that is the `netplay-info` identity file plus the VMU/flash tar, on
  the client only `netplay-info` (the python3 branch also
  directory-indexes the dir; the server writes no request log at all —
  busybox `httpd` runs without `-v` — and the stdout/stderr file it does
  write lives outside the serve dir, so nothing there is served). Note
  what that tar actually contains: `data/flycast` is shared by **all**
  Dreamcast games, so the host serves its **entire VMU set** (every
  `vmu_save_*.bin` plus `dc_nvmem.bin`), not just the saves of the game
  being played. Outbound, every netplay-on launch **ping-sweeps all 254
  addresses of the local /24 repeatedly and HTTP-GETs each neighbor**
  for up to 90 s; on a network that is not your own home LAN that
  traffic reads as a port scan. Inbound, the client
  extracts a synced tar only when all **five** guard conditions hold:
  the file is non-empty; `tar tf` lists at least one entry (a non-tar
  payload lists nothing, which would pass every negated test
  vacuously); every name is a bare allowlisted filename (no slashes,
  no `..`, no absolute paths); every `tar tvf` mode column is `-`; and
  no entry is a link (` -> `). The link test is not redundant with the
  mode column: busybox tar prints a HARDLINK with a leading `-`, so on
  this firmware the mode column alone would accept one. Verdicts on
  device (Brick, busybox 1.27.2): good tar ACCEPT; path-traversal,
  symlink, hardlink, non-tar junk and empty all REJECT. A hostile peer
  can at worst replace the client's netplay-data VMU copies, never
  write outside them.
- **UPnP is forced off** on every GGPO launch: launch.sh writes
  `[network] EnableUPnP = no`. flycast defaults it to **true**
  (`option.cpp:172`) and `ggpo.cpp:801-804` runs `miniupnp.Init()` +
  `AddPortMapping(19713/UDP)` *before* the `ActAsServer` branch, so
  **both** roles would otherwise punch a router mapping with an 86400 s
  lease (`miniupnp.cpp`). The `EnableUPnP = no` seeded in the
  `default*.cfg` files only reaches **fresh installs** (they're gated
  behind `.initialized`), so writing it from launch.sh is what closes it
  on already-installed devices. There is no restore-when-off branch —
  the value is only read on the GGPO path.
- **Pairing key gotcha:** peers match on the ROM **filename with its
  extension stripped** (`EMU_OVERLAY_GAME`), *not* on CHD content. Two
  byte-identical CHDs stored under different filenames will never pair,
  and the symptom is thin: the status screen sits for the full 90 s on
  whatever message survived show2's init window — "looking for the host"
  on the client, either "preparing saves" or "looking for player 2 …" on
  the host — then simply
  disappears, because the "starting anyway" update is queued and killed
  before it can be read. flycast then drops into its untimed "Starting
  Network". Nothing names the mismatch. Keep the game file named
  identically on both cards.
- **Leave flycast's `PerGameVmu` off.** With it on, the port-A1 VMU file
  is written as `<gameId>_vmu_save_A1.bin` (`oslib.cpp:52-66`) — a name
  the host's `tar cf … vmu_save_*.bin` glob misses and the client's
  allowlist (`vmu_save_[A-D][12].bin`) rejects. The sync then silently
  ships an incomplete card and the only symptom is "Peer verification
  failed" with no hint as to why.
- A shared `<rom basename>.state.net` savestate is still honored by
  flycast when present (auto-loaded at start, its MD5 replaces the
  VMU/flash hash — `emulator.cpp:674`, `ggpo.cpp:541`) but is no longer
  needed by the pak's own flow.
- GGPO forces the SH4 clock to stock 200 MHz and, under threaded rendering,
  disables framebuffer emulation (`emulator.cpp:864,983`) — automatic.
- GGPO disconnects a peer after 3 s of silence (`ggpo.cpp:566`) — opening
  the overlay menu (pause) or save/load state mid-session will drop or
  desync the match. Both sides launch the same game and sync from boot.
- `GGPOAnalogAxes` must match on both peers (default 0 on both; digital-only
  fighters like MvC2 don't need it).

**DCNet** (`DCNet = yes`, overlay: "DCNet Online") — emulates the DC modem
(or BBA with `EmulateBBA = yes`) and tunnels PPP/Ethernet traffic to the
public `dcnet.flyca.st` relay, which routes to fan-run revival servers. This
is for games with *native* online modes (PSO, ChuChu Rocket, Quake III,
Alien Front Online, …), needs internet (not just LAN), needs no peer
config, and does nothing for games without an online menu (MvC2). Off by
default; independent of GGPO.

Existing installs are already `.initialized`, so they never receive the
newly seeded `[network]` block — that's fine: the overlay toggles create
their keys through cfgdb on first save, and launch.sh creates/updates the
`server =` and `EnableUPnP` lines itself (the latter on every GGPO launch,
which is the only way an upgraded install ever gets it). The same is true of
`[input] device2`: flycast drops keys left at their default when it
rewrites the cfg, so a live file routinely has no `[input]` section at all
— launch.sh creates the section and the key when it needs them.

## Runtime libraries

**Nothing is shipped in the pak.** Verified on the Brick (tg5040) via
`LD_TRACE_LOADED_OBJECTS=1` (the device's own `ldd` shim) run with
`launch.sh`'s exact `LD_LIBRARY_PATH` — every `NEEDED` entry the binary has
resolved against the firmware's own copies, zero `not found`:

| Library | Resolves from (Brick / tg5040) |
|---|---|
| `libSDL2-2.0.so.0`, `libSDL2_ttf-2.0.so.0`, `libSDL2_image-2.0.so.0` | `/usr/trimui/lib` |
| `libGLESv2.so.2`, `libcurl.so.4`, `libssl.so.1.1`, `libcrypto.so.1.1` | `/usr/lib64` |
| `libzip.so.5` | `$SDCARD_PATH/.system/tg5040/lib` |

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

`skeleton/EXTRAS/Emus/shared/flycast/overlay_settings.json` drives the
in-game Options screen (same schema `workspace/all/common/emu_overlay_cfg.c`
already reads for N64.pak):

| Section | Key | Type | Values | Notes |
|---|---|---|---|---|
| Video (`[config]`) | `rend.WideScreen` | bool | — | 16:9 instead of native 4:3 |
| | `rend.WidescreenGameHacks` | bool | — | game-specific widescreen hacks; needs Widescreen on |
| | `rend.Resolution` | cycle | 480 / 640 / 720 | 480 is native |
| | `pvr.AutoSkipFrame` | cycle | Off / Normal / Maximum | **default `1` (Normal)** — a deliberate handheld-performance deviation from flycast's own compiled default (`0`, off); safe because the default cfg always seeds this key explicitly, so flycast never falls back to its own default here |
| | `rend.vsync` | bool | — | sync presentation to the display |
| RetroAchievements (`[achievements]`) | `Enabled` | bool | — | requires a NextUI RA login (see above) |
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
pak trees — `skeleton/EXTRAS/Emus/tg5040/DC.pak/SDL_Xbox 360 Controller.cfg`
and `skeleton/EXTRAS/Emus/tg5050/DC.pak/SDL_Xbox 360 Controller.cfg`
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
