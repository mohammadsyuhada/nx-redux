# Building & Deploying

Build system, desktop setup, and per-component iteration. Device facts are in
[DEVICES.md](DEVICES.md); on-device testing in [TESTING.md](TESTING.md).

## Build targets & platforms

The root `Makefile` runs on the **host** (macOS/Linux), not inside Docker, and
picks a build path from the target platform:

- **Device builds** (`tg5040`, `tg5050`) compile inside the LoveRetro Docker
  toolchain images and produce flashable release archives.
- **Desktop builds** (`desktop`) compile natively against Homebrew libraries
  for fast UI/debug iteration.

| Platform | Device(s) | Toolchain |
|---|---|---|
| `tg5040` | Trimui Smart Pro / Brick / Brick Pro | `ghcr.io/loveretro/tg5040-toolchain:latest` |
| `tg5050` | Trimui Smart Pro S | `ghcr.io/loveretro/tg5050-toolchain:latest` |
| `desktop` | Native host debug build | Homebrew GCC + SDL |

`make all` builds both device platforms (`PLATFORMS = tg5040 tg5050`) and
packages one release zip **per device variant** into `releases/`:

| Zip suffix | Platform | Overlays / bg | OSD |
|---|---|---|---|
| `-brick` | tg5040 | 768p / 1024 | 1024x768 |
| `-brickpro` | tg5040 | 768p / 1024 | 1024x768 |
| `-smartpro` | tg5040 | 720p / 1280 | 1280x720 |
| `-smartpros` | tg5050 | 720p / 1280 | 1280x720 |

Host requirements for device builds: Docker and `adb`. On the first build for
a platform, its toolchain repo is cloned into `toolchains/` and the Docker
image pulled automatically. Apple Silicon / arm64 hosts are the least painful;
x86_64 hosts have historically hit cross-architecture dependency issues.

```bash
# firmware/UI only (fast — reuses prebuilt cores)
make all

# include emulator cores (slow; first build or core changes only)
make all COMPILE_CORES=true
```

## Deploy a full build

```bash
make deploy DEVICE=brickpro   # brick | brickpro | smartpro | smartpros
make deploy PLATFORM=tg5040   # resolves to that platform's first device
```

`make deploy` pushes `build/BASE/MinUI-<device>.zip` to
`/mnt/SDCARD/MinUI.zip` and reboots — a full OTA-style update. Payloads are
per-device: pushing the wrong device's zip leaves that unit without an OSD
overlay, since each zip carries only its own.

## Quick build (single component via Docker)

For fast iteration, build one component and push just that binary:

```bash
docker run --rm -v $(pwd)/workspace:/root/workspace \
  ghcr.io/loveretro/tg5040-toolchain:latest \
  /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/<component> && make PLATFORM=tg5040'
```

Then push per the destination rules:

- **bin/daemon binaries** (`nextui`, `minarch`, `audiomon`, `screenshot`,
  `screenrecorder`, `options`, `netplay`, …) → `/mnt/SDCARD/.system/bin/`
  and **need a `reboot`** to take effect.
- **Bundled Tools paks** → `/mnt/SDCARD/.system/paks/Tools/<Name>.pak/`
  (flattened layout — no platform subdir). No reboot; just relaunch the pak.
  Several pak names contain spaces (`Music Player.pak`, `Artwork
  Manager.pak`, …) — quote the whole remote path, including for `adb shell
  md5sum`.
- **PortMaster is not bundled** — the Xtras `portmaster` catalog entry
  installs it into the SD user layer, so `portmaster.elf` pushes go to
  `/mnt/SDCARD/Tools/PortMaster.pak/` (only present after an Xtras install).

The full deploy rules (why the reboot, how to verify the push actually landed,
adb pitfalls) are in [TESTING.md](TESTING.md).

Component source lives in `workspace/all/<component>` (shared) and
`workspace/<platform>/` (platform-specific: `libmsettings`, `keymon`,
`install/`, cores Makefiles). Output lands in
`workspace/all/<component>/build/<platform>/`.

## Build gotchas

- **libmsettings first.** On a fresh tree, build
  `workspace/<platform>/libmsettings` in-container before anything that links
  `-lmsettings` (nearly everything). It is per-platform, not in
  `workspace/all/`.
- **musicplayer's `opus_obj/` is not platform-namespaced** (unlike
  `build/$(PLATFORM)/`). Building musicplayer for tg5040 and tg5050
  concurrently on the same workspace mount races and fails at link. Build its
  platforms sequentially. Other paks are namespaced and safe.
- **Don't run two full `make` docker builds concurrently** on the same
  workspace mount — observed transient linker failures from resource
  exhaustion. Per-platform component jobs are fine (except the opus_obj case).
- `workspace/hash.txt` (git short hash) must exist for in-container builds.
- `make clean` in a `workspace/all/<component>` dir typically wipes **all**
  platforms' `build/` — don't clean between the two platform builds of one
  component.
- **`common/api.c` is statically compiled into every app binary.** A change
  to shared rendering/input code (e.g. `GFX_blitButton`) shows up only in
  binaries that were rebuilt — shipping a UI change means rebuilding every
  pak that draws it, not just nextui.
- Shared UI components live in `workspace/all/common/ui/`, one component per
  file, listed **only** in `common/ui/ui.mk` (`$(UI_COMPONENT_SRCS)`); app
  Makefiles include the fragment. New UI components are added to ui.mk, never
  to individual app Makefiles. There is no umbrella header — include exactly
  the `ui_*.h` you use.
- Patched vendored projects (mupen64plus/GLideN64, flycast) are cloned at
  **pinned commits** by the platform Makefiles and patched from
  `workspace/all/other/`. Never regenerate a multi-file vendored patch with a
  plain `git diff` — untracked new files vanish from it; splice per-file
  sections instead. See `workspace/all/other/mupen64plus/README.md`.

## Desktop development setup

Prerequisites:

```bash
brew install gcc sdl2 sdl2_image sdl2_ttf sqlite libsamplerate clang-format
```

One-time setup:

```bash
# 1. The build expects `gcc` to be Homebrew GCC, not Apple Clang. No sudo —
#    this shims a symlink under /var/tmp/nxredux/bin, not /usr/local/bin:
scripts/desktop/setup-macos-toolchain.sh
/var/tmp/nxredux/bin/gcc --version   # must say "Homebrew GCC"

# 2. Fake SD card root at /var/tmp/nxredux/sdcard:
./workspace/desktop/prepare_fake_sd_root.sh

# 3. compile_commands.json for clangd (gitignored, per-clone):
make compile-commands
```

Build and run:

```bash
cd workspace/desktop/libmsettings
make build CROSS_COMPILE=/var/tmp/nxredux/bin/ PREFIX=/opt/homebrew PREFIX_LOCAL=/var/tmp/nxredux

cd workspace/all/nextui
make PLATFORM=desktop CROSS_COMPILE=/var/tmp/nxredux/bin/ PREFIX=/opt/homebrew PREFIX_LOCAL=/var/tmp/nxredux UNAME_S=Darwin
NXREDUX_SDCARD=/var/tmp/nxredux/sdcard DYLD_LIBRARY_PATH=/opt/homebrew/lib:/var/tmp/nxredux/lib ./build/desktop/nextui.elf
```

Runtime path roots (desktop only — device builds keep compile-time literals):
`NXREDUX_SDCARD` picks the "SD card" root directory; if unset it defaults to
`~/NXRedux`. `NXREDUX_SYSTEM_ROOT` independently overrides just the
`.system` root (defaults to `<sdcard>/.system`). Use the fake-SD workflow's
`/var/tmp/nxredux/sdcard` (from `prepare_fake_sd_root.sh` above) for a
populated dev card, or leave both unset to exercise the real default-root
path under `$HOME/NXRedux`.

## IDE setup (clangd)

The project uses **clangd** (VS Code: the llvm-vs-code-extensions.vscode-clangd
extension; the Microsoft C/C++ IntelliSense is disabled in
`.vscode/settings.json`). Run `make compile-commands` after cloning.

`make compile-commands` also assembles a two-part include shim so device
translation units parse on macOS: `build/clangd/include/` (rcheevos symlinks +
docker-extracted sysroot headers, self-healing re-extract) plus the committed
stubs in `scripts/clangd/include/` (glibc-only APIs via `#include_next`; the
stubs show bogus errors if opened directly — expected). Known non-parsing
files: `common/generic_*` (textually included into each platform's
`platform.c`), `emu_overlay_sdl.c`, mediaplayer's ffplay tree.

For unused-include sweeps, drive clangd over LSP (its `--check` mode doesn't
emit unused-include diagnostics); expect false positives on umbrella headers
(`common/sdl.h` carries IWYU export pragmas) and always verify removals with
full docker builds on **both** platforms.

## Formatting

```bash
make format                 # clang-format -i over tracked .c/.h
./scripts/install-hooks.sh  # pre-commit formatting hook
```

## Syncing with upstream

```bash
git fetch upstream
git rebase upstream/main
git push --force-with-lease
```
