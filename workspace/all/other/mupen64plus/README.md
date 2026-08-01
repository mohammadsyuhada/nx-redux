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
  (11025/22050/44100), e.g. for a Bluetooth sink that prefers 44100. `launch.sh` may
  override this per-sink via `--set Audio-SDL[OUTPUT_FREQUENCY]=<rate>`, using
  audiomon's published `/tmp/nx_audio_sink` to pick the rate; the cfg default stays
  48000. This `--set` override is read once, at launch, before the emulator starts —
  a sink change mid-game (Bluetooth hotplug, USB DAC plug/unplug) does not re-trigger
  it, so the audio plugin keeps outputting at whatever rate it launched with.
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
the menu unnavigable. Also the NxRedux resume handshake:
on the first rendered frame after overlay init, `emu_ovl_consume_resume_slot()` reads
and unlinks `/tmp/resume_slot.txt` (written by nextui on every launch; slot 0-7 only
on a game-switcher resume) and auto-loads that slot via `M64CMD_STATE_SET_SLOT` +
`M64CMD_STATE_LOAD` — the standalone equivalent of minarch's `State_resume()`.
And the quit side of the handshake (parity with DC.pak / minarch): overlay Quit
auto-saves to the hidden auto-resume slot (9) + writes the slot-9 switcher screenshot
and repoints the `.minui` txt, then issues `M64CMD_STOP` **3 frames later** —
`M64CMD_STATE_SAVE` is a queued job serviced at the next interrupt on the emu thread
(`gen_interrupt`, `device/r4300/interrupt.c`), so stopping in the same breath could
tear the loop down before the state is written.

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

**Regenerating `GLideN64-standalone.patch`:** the `.a` entries above are deliberately
content-less placeholders, so regenerate the patch WITHOUT `--binary`, and round-trip
check it with the lib dir excluded (a plain `-R` check would fail on the placeholders):

```sh
git apply --check -R --exclude='src/GLideNHQ/lib/*' GLideN64-standalone.patch
```

## Build (TG5040)

All builds run inside Docker using `ghcr.io/loveretro/tg5040-toolchain:latest`.

### 1. mupen64plus-core

**Important:** If switching build configurations (e.g., first build without dynarec, then with),
you must delete stale generated headers before rebuilding. The Makefile's `make clean` requires
cross-toolchain variables, so clean manually inside Docker first:

```sh
rm -rf _obj libmupen64plus.so* ../../src/asm_defines/asm_defines_gas.h ../../src/asm_defines/asm_defines_nasm.h
```

The core ships **netplay-enabled** (`NETPLAY=1`), so SDL2_net must be present in the
toolchain before the core links. Build SDL2_net and the core in a **single** docker run
(SDL2_net installs into the ephemeral container and links statically — nothing new ships
at runtime). See "SDL2_net (netplay)" below for the pitfall and verification.

The image has no `curl`, so fetch the SDL2_net tarball on the host first and mount it in:

```sh
curl -sL https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz \
  -o /tmp/SDL2_net-2.2.0.tar.gz   # sha256 4e4a891988316271974ff4e9585ed1ef729a123d22c08bd473129179dc857feb
```

```sh
docker run --rm -v $(pwd)/workspace:/root/workspace -v /tmp/SDL2_net-2.2.0.tar.gz:/tmp/SDL2_net-2.2.0.tar.gz \
  ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c '
source ~/.bashrc
export CROSS_COMPILE=aarch64-nextui-linux-gnu-
export PKG_CONFIG_PATH=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc

# --- SDL2_net 2.2.0, static + PIC, installed into the toolchain sdl2 prefix ---
cd /tmp && tar xzf SDL2_net-2.2.0.tar.gz && cd SDL2_net-2.2.0
PREFIX="$(pkg-config --variable=prefix sdl2)"
./configure --host="$(basename ${CROSS_COMPILE%-})" --prefix="$PREFIX" \
    --disable-shared --enable-static --with-pic CC="${CROSS_COMPILE}gcc"
make -j$(nproc) && make install
pkg-config --modversion SDL2_net   # MUST print 2.2.0

# --- core with NETPLAY=1 ---
cd /root/workspace/tg5040/other/mupen64plus/mupen64plus-core/projects/unix
SDL_C="$(pkg-config --cflags sdl2 SDL2_net)"
SDL_L="$(pkg-config --libs sdl2 SDL2_net)"
make -j$(nproc) all \
  CROSS_COMPILE=aarch64-nextui-linux-gnu- HOST_CPU=aarch64 \
  USE_GLES=1 NEON=1 PIE=1 VULKAN=0 NETPLAY=1 \
  PKG_CONFIG=pkg-config \
  SDL_CFLAGS="$SDL_C" SDL_LDLIBS="$SDL_L" \
  OPTFLAGS="-O3"
'
```

Output: `mupen64plus-core/projects/unix/libmupen64plus.so.2.0.0`

#### SDL2_net (netplay)

The core enables the mupen64plus v2 netplay API with `NETPLAY=1`, which pulls in
`src/main/netplay.c` (`-DM64P_NETPLAY`) and requires SDL2_net at link time. We build
SDL2_net 2.2.0 **static + PIC** and link it **statically** into the `.so`, so the shipped
core gains **no new runtime `NEEDED` dependency** (verify with the `readelf` check below).

**Pitfall — `SDL_CFLAGS`/`SDL_LDLIBS` override the Makefile's SDL2_net append.** When
`NETPLAY=1`, `projects/unix/Makefile` (~lines 357-365) would normally append
`pkg-config --cflags/--libs SDL2_net` on its own — but **only inside** the
`ifeq ($(origin SDL_CFLAGS) $(origin SDL_LDLIBS), undefined undefined)` block. Passing
`SDL_CFLAGS`/`SDL_LDLIBS` on the make command line (as this build does, matching the
ui-console/audio recipes) makes that whole block `defined`, so the internal SDL2_net
append **never runs**. The SDL2_net flags must therefore be folded into the command-line
values — hence `pkg-config --cflags sdl2 SDL2_net` and `pkg-config --libs sdl2 SDL2_net`
above (not just `sdl2`). (`-DM64P_NETPLAY` and compiling `netplay.c` come from a
separate `ifeq ($(NETPLAY),1)` block that is unaffected, so the build still succeeds
without the flags — it just fails to *link* SDL2_net. Always fold both.)

**Verify netplay compiled in** (expect ≥ 5; these are the `Netplay:` DebugMessage
strings from `netplay.c` — the working builds print `9`):

```sh
strings libmupen64plus.so.2.0.0 | grep -c "Netplay:"
```

**Verify static link** — `readelf -d` must show **no** `libSDL2_net` NEEDED entry, and
the NEEDED set must match the previously shipped core:

```sh
aarch64-nextui-linux-gnu-readelf -d libmupen64plus.so.2.0.0 | grep NEEDED   # no libSDL2_net
```

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
includes `$SDCARD_PATH/.system/lib` in `LD_LIBRARY_PATH` for this.

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

- **SDL2_net links statically as on tg5040 — but force the archive.** Unlike the tg5040
  image, the tg5050 sysroot **pre-ships a shared** `libSDL2_net-2.0.so.0`. A plain
  `-lSDL2_net` (what `pkg-config --libs SDL2_net` emits) then links that shared lib and
  adds a `libSDL2_net` runtime `NEEDED` we do not ship. After computing `SDL_L`, swap the
  `-lSDL2_net` token for the explicit static archive path so the linker cannot pick the
  `.so`:

```sh
SDL_L="$(pkg-config --libs sdl2 SDL2_net)"
SDL_L="${SDL_L/-lSDL2_net/${PREFIX}/lib/libSDL2_net.a}"   # PREFIX = pkg-config --variable=prefix sdl2
```

  Then verify with `readelf -d ... | grep NEEDED` — no `libSDL2_net` entry, and the NEEDED
  set matches the previously shipped tg5050 core (which keeps `libpng16.so.16`, not
  `libpng12.so.0`, from the LIBPNG override above).

## Deployment

Copy built binaries to the pak/shared directories (per platform for the pak, once for
the shared dir):

```
skeleton/SYSTEM/<tg5040|tg5050>/paks/Emus/N64.pak/
├── mupen64plus                    ← ui-console binary (per-platform build)
├── libmupen64plus.so.2            ← core library (per-platform build)
├── mupen64plus-audio-sdl.so       ← audio plugin (built from source, libsamplerate)
├── mupen64plus-input-sdl.so       ← stock input plugin
├── mupen64plus-rsp-hle.so         ← stock RSP plugin
├── launch.sh
└── default*.cfg

skeleton/BASE/Emus/shared/mupen64plus/
├── mupen64plus-video-GLideN64.so  ← video plugin (single shared build)
├── overlay_settings.json          ← overlay menu config
├── mupen64plus.ini                ← ROM database
├── InputAutoCfg.ini               ← input auto-config
├── mupencheat.txt                 ← cheat codes
└── libpng16.so.16                 ← libpng runtime
```

**Note:** `Emus/shared/` exists per SD card — pushing a new GLideN64 build to one
device does not update the other.

## Pre-launch options (Emulator Options / Emulator Settings)

mupen64plus is a pre-launch-options adopter (same schema-driven `options.elf` as
flycast): the game-list context menu's "Emulator Options" and Tools → Emulator
Settings both key off the pak shipping an `options.sh`. Per-game overrides land in
`$SHARED_USERDATA_PATH/N64-mupen64plus/config/<device>/games/<rom-base>.cfg` (one key
per changed value; `nx_rom_base()` in the sourced `nx_paths.sh` collapses a folder-m3u
game's discs to the folder name), while global edits go to the per-device
`mupen64plus.cfg`. `launch.sh` translates the override file to
`--set Section[KEY]=VALUE` args via awk and runs under `--nosaveoptions`, making every
`--set` a session-virtual runtime override that ui-console never persists back —
the mupen64plus analogue of flycast's `cfgSetVirtual`. Config-dir resolution and
first-run `mupen64plus.cfg` seeding live in `nx_paths.sh` (sourced by both scripts)
so options can be edited before a game has ever launched without drift.

**First-launch one-time cfg rewrite (normal, not the drift bug):** on a
freshly-seeded config, the FIRST game launch rewrites `mupen64plus.cfg` once — the
shipped `default-*.cfg` files predate ~35 settings newer GLideN64 knows (unused `hk*`
hotkey bindings), so the plugin adds them with empty defaults and saves the file,
once. Verified harmless (Brick, 2026-07-30): existing values survive, per-game
`--set` values do NOT get written in, and every launch after the first leaves the
file byte-identical. Practical consequence: when doing md5 "cfg unchanged" checks,
launch a game once BEFORE taking the "before" hash, or this one-time rewrite looks
like the audio-rate drift bug returning. Deliberately not fixed — fold the missing
keys in whenever the four `default-*.cfg` files are next regenerated anyway.

## Netplay

Multiplayer online play for N64 (**up to 4 players**), built on mupen64plus's own
upstream netplay client (the mupen64plus v2 core netplay API, `src/main/netplay.c`) —
**not** a libretro/minarch path. It follows the same standalone-emulator pattern as
DC/flycast: the pre-launch wizard (`netplay.elf`) handles rendezvous, and a small
on-host relay server does the per-frame input and save/settings sync. Nothing here
changes single-player launches — a plain A-launch takes none of these code paths.

Players are numbered by **join order**: the host is player 1, and joiners become
players 2, 3, 4 in the order they connect. Each player number is the game's N64
controller port.

### How it fits together

- **Core** — built with `NETPLAY=1` (pulls in `netplay.c` under `-DM64P_NETPLAY`, links
  SDL2_net; see the "SDL2_net (netplay)" build notes above). Once connected, the core's
  own netplay does the work: save transfer, settings sync, server-buffered UDP input,
  and CP0-based desync detection.
- **`mupen64plus-ui-console`** — patched to expose the netplay API on the CLI:
  `--netplay <host> <port>` and `--netplay-player <1-4>`. The frontend issues the
  `M64CMD_NETPLAY_*` commands (version check against `NETPLAY_API_VERSION`, init, control
  player, close) at the RMG-equivalent point in the startup sequence, with a connect-retry
  loop so launch-order jitter between the devices is harmless.
- **Wizard (`netplay.elf`)** — shared across N64/DC/minarch. A `--max-players N` flag
  (clamped 2–4, **default 2**) controls the lobby size. N64.pak's `launch.sh` passes
  `--max-players 4`; DC.pak and every minarch pak pass **nothing** and so stay at the
  default 2, where the wizard's behavior is byte-identical to the original 1:1 flow. With
  `N > 2` the host gets a "start when ready" screen — a live `Players connected: X / up to
  N` count and an A-to-start prompt (enabled once at least one joiner is present) — so the
  host presses A to begin the session once everyone has joined. With `N == 2` the host
  auto-starts on the first joiner (no A press), exactly as before. Player numbers are
  assigned at handshake by join order and travel to each joiner in the host's HELLO reply;
  the total `N` travels in the `START` message (the host only knows it when it presses
  Start).
- **`m64p-server.elf`** — a minimal, dependency-free C relay server
  (`workspace/all/n64-netplay-server/`, shipped inside `N64.pak`). Plain POSIX sockets,
  single process, implements the [mupen64plus v2 core netplay wiki protocol](https://www.mupen64plus.org/wiki/index.php?title=Mupen64Plus_v2.0_Core_Netplay):
  TCP player registration + settings/save relay, UDP input buffering/relay, and
  `UDP_SYNC_DATA` desync logging. All wire data is big-endian per the wiki spec.
- **`launch.sh`** — the DC-style inline wizard block (see the `# --- netplay pre-launch
  wizard ---` section). On `Y` / "Launch with Netplay" it runs `netplay.elf --max-players
  4` for rendezvous only (no save-sync args), then reads the role/peer/player/mode handoff
  and appends the right `--netplay` args to the emulator. It also builds the per-session
  controller-to-port config directory described below.

### Controller-to-port mapping (why each device drives its own player)

The netplay seat is the N64 controller port, but a device's **local pad must stay on
`[Input-SDL-Control1]`** — do **not** move it to the assigned port. The core reads a
client's local input through `getKeys(netplay_get_controller(seat))`
(`input_plugin_compat.c:71-79`), and `netplay_get_controller` resolves to the device's
*local controller 0* — i.e. `Control1` — for whatever seat this device controls. So every
device drives its own player from `Control1`, and the core routes that input to the
device's assigned seat. (This is the opposite of the intuitive "put player 2's pad on
port 2": doing that makes the joiner's seat read `getKeys(0)` = `Control1`, which — if the
pad were moved off `Control1` — reads an unplugged controller and sends **zero input**.
That exact inversion was the first on-device bug: player 2 was uncontrollable even though
the connection and registration were fine.)

The only thing that must change for multiplayer is **presence**: the shipped default cfg
plugs only `Control1`, so ports 2–4 are seen as **empty** and those seats are dead — the
game accepts no input for them even though netplay is routing input to them. (This is about
functional controllers, **not** the game's menu: many N64 titles hardcode their player-mode
menu — Mario Kart 64 always lists 1P/2P/3P/4P even with only `Control1` plugged — so plugging
does not change what the menu shows, only which seats actually respond.)
mupen64plus-input-sdl names controllers `[Input-SDL-Control1..4]` (1-indexed = ports 1–4);
`Control1` is `plugged = True` with a button map, `Control2/3/4` are `plugged = False`,
`device = -1`, empty maps.

`launch.sh` fixes presence per session without touching the on-disk cfg. It reads
`NETPLAY_NUM_PLAYERS=N` from `/tmp/netplay_session`, copies the device config dir to a
disposable `/tmp` directory, and rewrites its `[Input-SDL-Control1..4]` sections via
`nx_netplay_map.awk` (shipped in the pak):

- **`Control1` keeps `device = 0`** and its full button/axis template — the local pad, on
  every device regardless of player number.
- Ports `1..N` are set `plugged = True` (so the game presents N controllers); ports `> N`
  stay `plugged = False`. Ports `2..N` keep `device = -1` (no local joystick — their input
  arrives from the network); the core routes this device's `Control1` input to its seat.

`NETPLAY_PLAYER=P` drives only `--netplay-player P` (which seat this device registers as),
**not** the controller config. Because the button/axis values contain spaces and quotes
(e.g. `DPad R = "hat(0 Right)"`), the mapping is a whole-section rewrite, never a per-key
`--set`. The emulator launches with `--configdir <session dir>` (replacing the normal
device config dir); the session dir is removed in teardown. Per-game overrides still apply
via `--set` on top, and `--nosaveoptions` keeps everything session-virtual.

### Session flow

1. **Y-launch** on an N64 ROM → `launch.sh` runs `netplay.elf --max-players 4 --game
   "<rom>"` for rendezvous. Joiners connect; the host presses A once the lobby is full to
   start (for a 2-player wizard the host auto-starts on the first joiner). The wizard
   writes the handoff to `/tmp/netplay_session`, including `NETPLAY_PLAYER` (this device's
   1–4 number) and `NETPLAY_NUM_PLAYERS` (the total N). A non-zero exit bails cleanly to
   the game list (the emulator never starts peerless).
2. **Host** (player 1) starts `m64p-server.elf --port 55445 --players N` in the background
   (N from the session, not hardcoded), then launches mupen64plus with `--netplay
   127.0.0.1 55445 --netplay-player 1`. Player 1 is the protocol's source of truth for
   **both saves and the determinism-critical core settings** (CountPerOp, DisableExtraMem,
   emumode/dynarec, etc.), and it plays on the device's **real** save files.
3. **Joiners** (players 2..N) each launch with `--netplay <host-ip> 55445 --netplay-player
   <k>` plus a session-only save redirect
   `--set Core[SaveSRAMPath]=$USERDATA_DIR/netplay-data/mupen64plus/save`. They receive the
   host's saves and settings in-memory through the protocol; any in-game writes land in the
   staging dir (`netplay-data/mupen64plus/save/`), so a joiner's **real** saves are never
   touched. (`--nosaveoptions` is already in effect, so the `--set` never persists to
   `mupen64plus.cfg`.)
4. The server won't release the registration table (the start gate) until all N seats are
   registered, so no game starts before every device is connected.
5. On exit the host kills the server and `netplay.elf --cleanup` runs (crash-safe, same as
   DC/minarch), restoring WiFi/hotspot state.

### Port and logs

- **Port 55445** — one TCP and one UDP socket to the same number.
- `$LOGS_PATH/netplay-wizard.txt` — the pre-launch wizard's output (rendezvous + cleanup).
- `$LOGS_PATH/n64-netplay-server.txt` — the relay server's output on the host: buffer-size
  adjustments and any `desync`/`DESYNC` lines.

### Performance tuning and where to host (on-device learnings)

N64 is the heaviest emulator here, and netplay adds sync work on top. The tunings below are
all **netplay-only** (single-player untouched). Together they make **2-player** smooth and
solid on the Brick (tg5040, 4× Cortex-A53) — as host *or* joiner (≤~0.5 s felt latency on a
clean network). **3-player is a different story: it is GPU-bound on the Brick and cannot be
tuned into 60 fps** — see "Known limits". The Smart Pro (tg5050, big.LITTLE) has ample
GPU/CPU headroom and needs none of this, but the server pinning + buffer target are applied
on both for consistency.

- **Where to host (input latency).** In this client-server model the host's own input
  round-trips through its **local** server (`127.0.0.1`, ~0 latency) while a joiner's input
  travels over **WiFi** to the host — so **the host always has the snappiest input** and
  joiners pay the network latency. For **2 players**, hosting on the Brick gives the Brick
  the local-input advantage (the faster Smart Pro tolerates being the remote joiner well).
  For **3+ players**, host on the **strongest** device (the Smart Pro): the host renders the
  split-screen *and* relays to every joiner, so the weakest device is the worst host. Either
  way, joiner input latency is dominated by WiFi quality (see "Networking" below).
- **CPU pinning (tg5040 `launch.sh`).** N64's dynarec is effectively single-threaded, so
  one saturated core stalls emulation even while others idle. The launch pins the main
  CPU/dynarec thread to **cpu0 exclusively**, **every** other mupen64plus worker thread
  (GLideN64 video, etc.) to **cpu1**, and the SDL/audio helpers **and** the relay server to
  **cpu2-3**. (The old heuristic pinned only the single "busiest" thread by a 2-second
  snapshot and could leave a heavy worker floating onto cpu0 to steal dynarec cycles —
  tolerable in single-player, but under netplay it pushed the Brick behind the buffer.)
  The server is pinned because an unpinned relay landing on cpu0 stalls emulation and
  delays even the host's own (localhost) input.
- **Lighten the GPU during netplay (tg5040).** For netplay sessions only, `launch.sh`
  forces `--set Video-GLideN64[UseNativeResolutionFactor]=1` (down from the default 2×) **and**
  `--set Video-GLideN64[txHiresEnable]=False` (hi-res texture packs off — the single biggest
  win; the `.hts` packs are far heavier than native N64 textures). **Video settings are local
  and are never synced by the netplay protocol** (only core CPU/RSP timing is), so they
  **cannot desync** the session — they just cut Mali/GPU load, which is the real bottleneck
  on the Brick (measured under netplay: cpu0/cpu1 ~70 % with **cpu2/cpu3 idle** → GPU-bound,
  not CPU-bound). 2× proved too heavy even with hi-res off — the Brick fell **seconds** behind
  during busy races — so netplay forces 1×. These `--set`s are **last** on the command line,
  so they win over any per-game or global cfg value; single-player keeps its full resolution
  and hi-res (the `--set`s are session-only under `--nosaveoptions`). The trade is a softer
  image during netplay.
- **Input buffer depth (`--buffer-target 2`).** The server runs at the code default of 2.
  We briefly raised it to 4 to absorb dips while the Brick was still rendering 2×/hi-res, but
  once 1×+hi-res-off let the Brick hold 60 fps the extra depth only added noticeable input
  latency ("delayed even in the menu"), so it was returned to **2**. It remains the
  latency-vs-smoothness knob: raise it only if a device consistently dips below 60 fps
  (deeper buffer = more slack to ride out dips, at the cost of constant input latency).
- **Networking — the Bricks are 2.4 GHz-only.** The Brick and Brick Pro Wi-Fi radios only
  support **2.4 GHz** (the Smart Pro also does 5 GHz). On a shared/public AP the Bricks hit
  heavy 2.4 GHz airtime contention: RTT jitter of **5–75 ms** and a "queued" feel as two
  Bricks serialize on one radio (one catches up to the host, then the other). A **dedicated
  network fixes this** — the wizard's host-AP hotspot (the host device broadcasts its own AP,
  which the joiners connect to) dropped RTT to **~1–2 ms with near-zero jitter**. For netplay,
  prefer the host-AP hotspot or a private router on a clean channel (1/6/11), and keep both
  Bricks close to it; avoid shared "free" networks. Note the joiner delay is *network*
  latency, not the relay — the server (below) is not the bottleneck.

### Known limits and caveats

- **Buffered input-delay, not rollback.** Inputs are delayed and buffered; UDP loss is
  harmless (the server fabricates missing frames from each seat's last known input), but a
  device that drops frames slows **all** players. N64 is the heaviest emulator in the
  set, so per-game playability varies and some titles may simply be too heavy for netplay
  — judge on device. `--buffer-target N` on the server (N64's `launch.sh` uses the code
  default of 2) is the latency-vs-smoothness knob.
- **Player count: 2 verified great, 3+ is GPU-bound on the Brick.** The wizard's multi-join
  lobby and the server's `--players N` gate support 2–4 devices, and the path is hardware-
  tested to 3. **2-player is smooth and verified** (Brick as host *or* joiner, ≤~0.5 s felt
  latency). **3-player is limited by GPU, not the netplay path**: N64 split-screen renders one
  3D viewport *per seat*, so a 3-player game draws **3× the viewports**. The Brick's Mali
  cannot hold 60 fps on a 3-way split and drifts seconds behind — measured on device, it
  renders **~45 fps while the host feeds 60**, with its **cpu2/cpu3 idle** (ruling out CPU),
  and the relay ruled out too (server ~6 % CPU, ~68 s input ring, ~1–2 ms RTT on a clean
  link). Stripping GPU features further (framebuffer readbacks, etc.) did **not** recover it.
  So **3- and 4-player need a Smart-Pro-class GPU on every seat**; on the Brick, netplay tops
  out at a comfortable 2 players. 4-player remains hardware-pending.
- **N64 game menus show all player slots regardless of connected count.** e.g. Mario Kart 64
  always lists 1P/2P/3P/4P — even in single-player or a 2-device session. That menu is
  hardcoded in the ROM and is **not** driven by how many controllers are plugged (verified:
  single-player shows all four with only `Control1` plugged). The controller config still
  enables exactly the connected seats, so only those karts are controllable; picking a larger
  mode just leaves the extra karts uncontrolled. There is no emulator-side fix short of ROM
  patching — it is cosmetic and harmless.
- **All devices must run the same build.** Determinism relies on identical cores; the
  core enforces `NETPLAY_API_VERSION` and rejects a mismatch (`M64ERR_INCOMPATIBLE`).
- **Desyncs are logged, not corrected.** Each client reports CP0 registers every 600 VIs;
  the server logs a divergence but the cores keep running (upstream behavior). v1 adds no
  in-session UX.

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
