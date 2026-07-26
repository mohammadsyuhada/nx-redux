# Mupen64Plus Standalone (N64 Emulator)

Standalone mupen64plus built from upstream sources with custom overlay menu integration.
This directory holds everything platform-independent: the patches and this document.
The core/ui-console/audio source checkouts and build outputs stay per-platform under
`workspace/<platform>/other/mupen64plus/` (they build in-tree, per toolchain); the
platform Makefile's `$(REQUIRES_MUPEN64PLUS)/mupen64plus-core` rule clones them at
pinned commits and applies the patches from here. The GLideN64 checkout is shared and
lives in this directory (cloned by the tg5040 Makefile rule).

## Components

| Directory (per platform) | Description |
|---|---|
| `mupen64plus-core/` | Emulator core library (`libmupen64plus.so.2`) |
| `mupen64plus-ui-console/` | Console frontend binary (`mupen64plus`) |
| `mupen64plus-audio-sdl/` | Audio plugin (`mupen64plus-audio-sdl.so`) — patched for 48 kHz output |
| `GLideN64-standalone/` | Video plugin (`mupen64plus-video-GLideN64.so`) — **built once, shared across platforms**; checkout lives HERE (`workspace/all/other/mupen64plus/`), built with the tg5040 toolchain image |

Pinned upstream commits live in `workspace/<platform>/Makefile` (`MUPEN64PLUS_*_COMMIT`,
`GLIDEN64_COMMIT`) — keep the two platforms' pins in sync.

## Source Modifications

### mupen64plus-ui-console (`mupen64plus-ui-console.patch`)

**`src/osal_dynamiclib_unix.c`** — Change `dlopen()` to use `RTLD_GLOBAL` so GLES/EGL
symbols from the core library are visible to plugins loaded later:

```c
*pLibHandle = dlopen(pccLibraryPath, RTLD_NOW | RTLD_GLOBAL);
```

### mupen64plus-audio-sdl (`mupen64plus-audio-sdl.patch`)

**`src/main.c` / `src/sdl_backend.c`** — Fixes for distorted/rough audio on the
Trimui devices (verified on the Brick with Mario Kart 64, 2026-07):

- **`OUTPUT_FREQUENCY` config parameter** (default 48000) overriding the hardcoded
  44100 Hz output rate. The devices' `/etc/asound.conf` routes ALSA `default`
  through a dmix slave locked at 48000 Hz, and with no `rate_converter` configured
  alsa-lib converts 44100→48000 with its built-in linear interpolator — audibly
  distorted. Outputting 48000 directly means a single sinc resample (game rate →
  48 kHz) and no ALSA-side conversion. Set to 0 to restore upstream auto-selection
  (11025/22050/44100), e.g. for a Bluetooth sink that prefers 44100.
- **Dynamic rate control** — the audio callback nudges the resample ratio (max
  ±0.5%, inaudible) to steer the buffer level toward `PRIMARY_BUFFER_TARGET`.
  With `AUDIO_SYNC=False` nothing else couples the emulated AI clock to the DAC
  clock, so drift used to pin the buffer at full (dropped chunks = crackle every
  ~15 s) or drain it (silence gaps).
- **Partial fill on underrun** — a short buffer now plays what's available with a
  zero-filled tail instead of a whole callback (43 ms) of silence.
- **SCHED_FIFO for the audio callback thread** (set from the callback itself; needs
  root) — the thread shares cpu2-3 with GPU workers, and starvation caused SDL
  catch-up callbacks that drained the buffer in bursts.
- **Prime-to-target before unpausing** — after start/device re-init/starvation the
  audio stays paused until the buffer reaches target, replacing a burst of
  machine-gun underruns with one ~250 ms silent prime.
- **Underrun logging** — periodic (5 s) and session-total counts, so audio health
  is diagnosable from `$LOGS_PATH/N64.txt`.

Buffer sizing in the default cfgs goes with this: `PRIMARY_BUFFER_SIZE = 24576`,
`PRIMARY_BUFFER_TARGET = 12288` (256 ms operating level — the emulator's audio
production is bursty and a thinner cushion measurably underruns; the original
un-regulated buffer sat at ~370 ms).

Measured on the Brick (4-minute MK64 race, counters from the log): stock chain
62 underruns + 16 dropped chunks → patched chain 1 underrun, 0 drops.

### GLideN64-standalone (`GLideN64-standalone.patch`)

**`toolchain-aarch64.cmake`** — Cross-compilation toolchain for Docker builds.

**`src/overlay/OverlayGL.cpp`** — OpenGL ES render backend for the in-game overlay menu.

**`src/DisplayWindow.cpp`** — Overlay integration: init, menu button detection, overlay
loop, save/load state handling via `CoreDoCommand`. Menu navigation reads the d-pad hat
merged with analog stick axes 0/1 (`read_dpad_state()`) — on the Brick the FN switch
reroutes the d-pad to the stick axes for analog steering, which would otherwise leave
the menu unnavigable. Also the NextUI resume handshake:
on the first rendered frame after overlay init, `emu_ovl_consume_resume_slot()` reads
and unlinks `/tmp/resume_slot.txt` (written by nextui on every launch; slot 0-7 only
on a game-switcher resume) and auto-loads that slot via `M64CMD_STATE_SET_SLOT` +
`M64CMD_STATE_LOAD` — the standalone equivalent of minarch's `State_resume()`.

**`src/CMakeLists.txt`** — Added overlay source files from `workspace/all/common/`.
Note: `include_directories(${OVERLAY_COMMON_DIR})` must come AFTER `include_directories(. inc)`
to avoid `config.h`/`Config.h` name collision on case-insensitive filesystems.

**`src/GLideNHQ/lib/{libpng.a,libz.a,libzstd.a}`** — upstream bundles x86-64 static
libs; they must be replaced with aarch64 builds BEFORE building the plugin. The patch
records them as binary diffs without content, so recreate them after applying it:

```sh
# libzstd.a comes straight from the toolchain sysroot:
SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc   # inside docker
cp $SYSROOT/usr/lib/libzstd.a GLideN64-standalone/src/GLideNHQ/lib/libzstd.a

# libz.a must be BUILT, not copied: the sysroot's static zlib predates 1.2.9 and
# lacks adler32_z/crc32_z, which libpng 1.6.x references — linking it produces a
# .so that fails dlopen at runtime with "undefined symbol: adler32_z".
# Download zlib 1.3.1 (github.com/madler/zlib releases), then inside docker:
CC=aarch64-nextui-linux-gnu-gcc CFLAGS="-O3 -fPIC" ./configure --static
make libz.a
cp libz.a GLideN64-standalone/src/GLideNHQ/lib/libz.a

# libpng must match the 1.6.x headers in src/GLideNHQ/inc (sysroot only has 1.2):
# download libpng 1.6.43, then inside docker:
./configure --host=aarch64-nextui-linux-gnu --disable-shared --enable-static \
  CC=aarch64-nextui-linux-gnu-gcc CFLAGS="-O3 -fPIC --sysroot=$SYSROOT" \
  CPPFLAGS="--sysroot=$SYSROOT" LDFLAGS="--sysroot=$SYSROOT"
make libpng16.la
cp .libs/libpng16.a GLideN64-standalone/src/GLideNHQ/lib/libpng.a

# Sanity check before deploying — must print nothing:
aarch64-nextui-linux-gnu-nm -D --undefined-only \
  src/build/plugin/Release/mupen64plus-video-GLideN64.so | grep " U adler32"
```

(`libdxtn.a` is a Windows COFF lib upstream; it is not referenced by this build and
can stay as-is.)

## Build (TG5040)

All builds run inside Docker using `ghcr.io/loveretro/tg5040-toolchain:latest`.

### 1. mupen64plus-core

**Important:** If switching build configurations (e.g., first build without dynarec, then with),
you must delete stale generated headers before rebuilding. The Makefile's `make clean` requires
cross-toolchain variables, so clean manually inside Docker first:

```sh
rm -rf _obj libmupen64plus.so* ../../src/asm_defines/asm_defines_gas.h ../../src/asm_defines/asm_defines_nasm.h
```

```sh
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c '
source ~/.bashrc
export PKG_CONFIG_PATH=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
cd /root/workspace/tg5040/other/mupen64plus/mupen64plus-core/projects/unix
make -j$(nproc) all \
  CROSS_COMPILE=aarch64-nextui-linux-gnu- HOST_CPU=aarch64 \
  USE_GLES=1 NEON=1 PIE=1 VULKAN=0 \
  PKG_CONFIG=pkg-config \
  OPTFLAGS="-O3"
'
```

Output: `mupen64plus-core/projects/unix/libmupen64plus.so.2.0.0`

### 2. mupen64plus-ui-console

```sh
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c '
source ~/.bashrc
export PKG_CONFIG_PATH=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
SDL_C="$(pkg-config --cflags sdl2)"
SDL_L="$(pkg-config --libs sdl2)"
cd /root/workspace/tg5040/other/mupen64plus/mupen64plus-ui-console/projects/unix
make -j$(nproc) all \
  CROSS_COMPILE=aarch64-nextui-linux-gnu- HOST_CPU=aarch64 PIE=1 \
  PKG_CONFIG=pkg-config \
  SDL_CFLAGS="$SDL_C" SDL_LDLIBS="$SDL_L" \
  APIDIR=/root/workspace/tg5040/other/mupen64plus/mupen64plus-core/src/api \
  COREDIR="./" PLUGINDIR="./" \
  OPTFLAGS="-O3"
'
```

Output: `mupen64plus-ui-console/projects/unix/mupen64plus`

### 3. mupen64plus-audio-sdl

Built from source to enable the `src-sinc-fastest` resampler (requires libsamplerate,
available in the toolchain — the stock binary only includes the `trivial` resampler)
and patched with `mupen64plus-audio-sdl.patch` for 48 kHz output (see Source
Modifications above).

```sh
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c '
source ~/.bashrc
export PKG_CONFIG_PATH=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
SDL_C="$(pkg-config --cflags sdl2)"
SDL_L="$(pkg-config --libs sdl2)"
cd /root/workspace/tg5040/other/mupen64plus/mupen64plus-audio-sdl/projects/unix
make -j$(nproc) all \
  CROSS_COMPILE=aarch64-nextui-linux-gnu- HOST_CPU=aarch64 PIE=1 \
  PKG_CONFIG=pkg-config \
  SDL_CFLAGS="$SDL_C" SDL_LDLIBS="$SDL_L -lpthread" \
  APIDIR=/root/workspace/tg5040/other/mupen64plus/mupen64plus-core/src/api \
  OPTFLAGS="-O3"
'
```

Output: `mupen64plus-audio-sdl/projects/unix/mupen64plus-audio-sdl.so`

**Note:** The device must have `libsamplerate.so.0` available at runtime. The launch script
includes `$SDCARD_PATH/.system/tg5040/lib` in `LD_LIBRARY_PATH` for this.

### 4. GLideN64 video plugin (shared across platforms)

```sh
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c '
source ~/.bashrc
cd /root/workspace/all/other/mupen64plus/GLideN64-standalone/src
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../../toolchain-aarch64.cmake \
  -DMUPENPLUSAPI=ON -DEGL=ON -DMESA=ON \
  -DNEON_OPT=ON -DCRC_ARMV8=ON ..
make -j$(nproc) mupen64plus-video-GLideN64
'
```

Output: `GLideN64-standalone/src/build/plugin/Release/mupen64plus-video-GLideN64.so`

## TG5050 build differences

Everything above applies with `tg5050` substituted for `tg5040` (toolchain image and
checkout paths), except:

- **GLideN64 is not built on tg5050** — the .so is shared; deploy the tg5040 build.
- **libpng headers workaround** — the tg5050 toolchain has broken libpng header
  symlinks (`png.h -> libpng16/png.h` where `libpng16/` doesn't exist). Provide them
  once:

```sh
mkdir -p workspace/tg5050/other/mupen64plus/libpng-headers
cd workspace/tg5050/other/mupen64plus/libpng-headers
curl -sL https://github.com/glennrp/libpng/archive/refs/tags/v1.6.37.tar.gz -o libpng.tar.gz
tar xf libpng.tar.gz
cp libpng-1.6.37/scripts/pnglibconf.h.prebuilt libpng-1.6.37/pnglibconf.h
```

Then add to the mupen64plus-core make invocation:

```sh
PNG_HEADERS=/root/workspace/tg5050/other/mupen64plus/libpng-headers/libpng-1.6.37
  ...
  LIBPNG_CFLAGS="-I${PNG_HEADERS}" LIBPNG_LDLIBS="-lpng16 -lz" \
```

## Deployment

Copy built binaries to the pak/shared directories (per platform for the pak, once for
the shared dir):

```
skeleton/EXTRAS/Emus/<tg5040|tg5050>/N64.pak/
├── mupen64plus                    ← ui-console binary (per-platform build)
├── libmupen64plus.so.2            ← core library (per-platform build)
├── mupen64plus-audio-sdl.so       ← audio plugin (built from source, libsamplerate)
├── mupen64plus-input-sdl.so       ← stock input plugin
├── mupen64plus-rsp-hle.so         ← stock RSP plugin
├── launch.sh
└── default*.cfg

skeleton/EXTRAS/Emus/shared/mupen64plus/
├── mupen64plus-video-GLideN64.so  ← video plugin (single shared build)
├── overlay_settings.json          ← overlay menu config
├── mupen64plus.ini                ← ROM database
├── InputAutoCfg.ini               ← input auto-config
├── mupencheat.txt                 ← cheat codes
└── libpng16.so.16                 ← libpng runtime
```

**Note:** `Emus/shared/` exists per SD card — pushing a new GLideN64 build to one
device does not update the other.

## Key Build Flags

| Flag | Purpose |
|---|---|
| `USE_GLES=1` | Use OpenGL ES instead of desktop GL |
| `NEON=1` | Enable ARM NEON SIMD optimizations |
| `PIE=1` | Position-independent executable |
| `VULKAN=0` | Disable Vulkan (not available on target) |
| `HOST_CPU=aarch64` | Target architecture (enables NEW_DYNAREC) |
| `COREDIR="./"` | Search for core library relative to CWD |
| `PLUGINDIR="./"` | Search for plugins relative to CWD |
| `PKG_CONFIG=pkg-config` | Override cross-prefix pkg-config lookup |
