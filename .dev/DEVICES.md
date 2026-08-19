# Devices & Platforms

Per-device hardware and platform facts. Build/deploy recipes live in
[BUILD.md](BUILD.md), on-device test recipes in [TESTING.md](TESTING.md).

## Platform ↔ device map

One *platform* is one toolchain/kernel target; several *devices* can share it.
The Makefile's authoritative list is the `DEVICES` variable
(`device=platform,overlays,bg-width,osd-res`):

| Device | `DEVICE` | Platform | Panel | Kernel | SoC cores |
|---|---|---|---|---|---|
| Trimui Brick | `brick` | `tg5040` | 1024×768 | 4.9 | 4× A53 (homogeneous) |
| Trimui Brick Pro | `brickpro` | `tg5040` | 1024×768 | 4.9 | 4× A53 |
| Trimui Smart Pro | `smartpro` | `tg5040` | 1280×720 | 4.9 | 4× A53 |
| Trimui Smart Pro S | `smartpros` | `tg5050` | 1280×720 | 5.15 | 8× A55 big.LITTLE (boot policy runs 3: cpu0,1,4) |

The `DEVICE` env var is set at boot and distinguishes same-platform devices.
Model string: `TRIMUI_MODEL` (e.g. `Trimui Brick Pro`).

## UI scale and asset sheets

- `FIXED_SCALE` on tg5040 is **runtime**: `(is_brick ? 3 : 2)`
  (`workspace/tg5040/platform/platform.h`). The Brick renders the 1024×768
  panel at 3x; Smart Pro and Brick Pro use 2x. tg5050 is a compile-time 2.
- Because the scale is runtime, sprite-sheet glyphs must be drawn into **all
  four** `skeleton/SYSTEM/res/assets@{1,2,3,4}x.png` sheets, and all four must
  be deployed together. Pushing only `@2x` makes a new glyph invisible on the
  Brick (it loads `@3x`) — this exact mistake has burned a debugging session.
- Framebuffer format is RGB565 (`FIXED_BPP 2`) — no alpha channel on screen.
  Anti-aliasing therefore lives on *source* surfaces (PNG alpha, TTF blended
  alpha) and is consumed by the blit; drawn shapes need coverage-based AA
  (see `UI_fillRoundedRect` in `common/ui/ui_draw.c`).

## Display topology (critical for capture/screenshots)

The two platforms scan out completely differently:

- **tg5040**: Mali fbdev EGL renders through `/dev/fb0` — fb0 *is* the visible
  scanout. `dd` from fb0 is a valid screenshot (BGRA). Note fb0 holds a stale
  frame until the app's next input-driven redraw. `trimui_osdd` renders its
  overlay into high fb0 panes on this platform.
- **tg5050**: the UI renders via DRM/KMS (GL). `/dev/fb0` is **never scanned
  out** — reading it returns black (the only writer is a clear in
  `PLAT_quitVideo`). Any fbdev-based capture on tg5050 produces a black image.
  fb0 geometry ioctls are also inconsistent there (virtual_size says
  1280×1440 while the stride is 720-row 4-byte) — don't trust fb ioctls on
  tg5050. Capture must use the DRM scanout or the app-published GPU mirror
  (see [CAPTURE.md](CAPTURE.md)).
- tg5050's `sunxi-drm` also exposes a `card0-Writeback-1` connector, but it
  needs DRM master + atomic — unusable from a side daemon. `/dev/g2d` exists
  (possible future acceleration).

## CPU topology and default state

System default state is set by `MinUI.pak/launch.sh` and restored when an app
exits:

| Setting | tg5050 | tg5040 |
|---|---|---|
| Big core governor | cpu4: userspace | cpu0: userspace |
| Big core speed | 2160 MHz | 2000 MHz |
| Little core governor | cpu0: schedutil | N/A (homogeneous) |
| Little core max | 2000 MHz (hw caps at 1416) | N/A |
| Cores online | cpu0,1,4 (others offline) | all |
| MinUI loop restores | scaling_setspeed only | scaling_setspeed only |

Available frequencies:

- tg5050 big (cpu4): 408 672 840 1008 1200 1344 1488 1584 1680 1800 1992 2088 2160 MHz
- tg5050 little (cpu0): 408 672 792 936 1032 1128 1224 1320 1416 MHz
- tg5040 (cpu0): 408 600 816 1008 1200 1416 1608 1800 2000 MHz

Notes:

- On tg5050, cores in a cluster share one frequency domain (cpu4-7 together);
  bringing a core online joins it to the cluster's current frequency.
- The Smart Pro S is 8-core silicon deliberately run as 3-core by the boot
  policy — spare cores are a future performance lever (paks may online one,
  e.g. the screen-record toggle onlines cpu2).
- **Pak launch scripts own CPU state**: each `.pak`'s `launch.sh` is
  responsible for setting clocks on entry and cleaning up on exit. Some cap
  clocks hard (e.g. the Files pak caps big cores at 408 MHz) and a manual /
  killed run may leave the cap in place — always check `scaling_max_freq`
  before benchmarking on device (see [TESTING.md](TESTING.md)).

## Firmware requirements

NX Redux targets current stock firmware and does not bundle firmware libs
(e.g. `libmpg123.so.0` lives in `/usr/trimui/lib` and only ships from Brick
firmware 1.1.1 — older firmware kills anything that needs it, such as LÖVE
ports). The gate is system-level, not per-pak: `workspace/<plat>/install/update.sh`
compares `/etc/version` against `MIN_FW` (tg5040: **1.1.1**, tg5050: **1.0.1**)
and warns via the show2 splash without blocking.

- Probe firmware with `cat /etc/version` (`/usr/trimui/version` is empty).
- The Smart Pro S is a **separate 1.0.x firmware line** — never assume the
  tg5040 family's 1.1.1 numbering applies to it.
- Official firmware images: `github.com/trimui/firmware_*` releases.
  Recovery images are the source for stock OSD extraction (see
  [OSD.md](OSD.md)).

## Input daemons

- Every device's gamepad appears as `TRIMUI Player1` via a `trimui_inputd`
  uinput daemon. Event nodes and button codes are in
  [INPUT_MAPPING.md](INPUT_MAPPING.md).
- **tg5040 runs the device's own stock `/usr/trimui/bin/trimui_inputd` by
  absolute path.** A vendored inputd was shipped once (v1.2.0) and broke all
  input on the Smart Pro — the three tg5040 devices have different input
  wiring and the daemons are not interchangeable. Absolute-path launch also
  defends against stale vendored binaries left on updated cards (updates never
  delete files, and a bare-name PATH lookup would find them).
- **tg5050 keeps a vendored inputd** — the Smart Pro S Home button
  (`KEY_HOMEPAGE` 172) needs it for the instant OSD trigger.
- The same not-interchangeable rule applies to `trimui_osdd`: every model's
  build differs (different md5), which is why the OSD skeleton has a
  `device/<dev>/` layer per model.

## Userland/shell limitations

Busybox differs per platform; scripts must target the intersection:

- tg5040 busybox has **no `nohup`, no `setsid`, no `unzip` applet, no
  multiplexer**. tg5050 has `setsid` and `unzip`.
- Neither platform ships `rfkill` or `timeout` binaries.
- Busybox `printf` does **not** emit `\x` hex escapes — on-device binary
  injection via printf silently fails; build binary files on the host instead
  (see [TESTING.md](TESTING.md)).
- Statically-linked glibc binaries abort with `FATAL: kernel too old` on
  tg5040's 4.9 kernel — cross-compiled helpers must be dynamically linked.
- `/bin/bash` may exist only as a symlink PortMaster's launcher creates into
  its own bin — system scripts must be POSIX sh.
- The vendored Info-Zip `unzip` at `<SDCARD>/.tmp_update/<platform>/unzip`
  (the updater's own binary) is load-bearing on tg5040 for stock-OSD restore.

## Filesystem facts

- The SD card is exFAT (FUSE-mounted on tg5050) / vfat: **no ETXTBSY
  protection**, so `adb push` over a running binary corrupts the mapped
  executable (see [TESTING.md](TESTING.md) for the deploy rules).
- On **tg5050**, `/mnt/SDCARD` is a **symlink** to `/mnt/sdcard/mmcblk1p1`.
  Anything comparing paths against `/proc/mounts` or fd targets must
  `realpath()` first (this silently broke poweroff's SD-unmount protection
  before `poweroff_next.c` learned to resolve it).
- tg5040's kernel-4.9 overlayfs rejects the exFAT SD card as a lower layer
  outright; tg5050's 5.15 accepts it but its fuseblk lower layer lacks
  `d_type`, so merged-view directory *listings* can come back empty even
  though path lookups work. Details in [OSD.md](OSD.md).

## Power / PMIC

- tg5040-family devices have an AXP2202 PMIC on i2c-6 addr 0x34;
  `workspace/all/poweroff_next/poweroff_next.c` pokes it behind
  `-DHAS_AXP2202_POWEROFF` (tg5040 Makefile only). tg5050 has a different PMIC
  and ends shutdown in the plain `reboot(POWER_OFF)` syscall.
- Both platforms run `poweroff_next` from the launch loop on shutdown
  (`/tmp/poweroff` flag). SDL2 converts the shutdown SIGTERM into `SDL_QUIT`,
  which `PAD_poll` maps to `PWR_powerOff` — the graceful path for nextui and
  minarch both.
- USB gadget state sysfs (`/sys/class/udc/*/state`) **latches** at
  `configured` forever after the first host connection on tg5050 (clears
  correctly on tg5040). `axp2202-usb/online` tracks VBUS instantly on both;
  any "is USB connected" logic must AND both signals, and expect ~4 s of
  online=1 while still `not attached` on replug.

## Bluetooth / WiFi radio asymmetry

Do **not** harmonize the platforms' BT-off behavior:

- **tg5050** (separate WiFi/BT chips): BT off = `bluetoothctl power off`,
  keep `bluetoothd` alive (killing it wedges other apps' bluetoothctl calls).
- **tg5040** (xradio combo chip): BT off must fully stop the stack — a live
  `bluetoothd` collapses WiFi throughput from ~330 KB/s to ~2 KB/s.

Apps read live radio state (wlan0 `IFF_UP` sysfs flags / raw HCI ioctl), not
cached config; `generic_bt.c` shell-outs are deadline-killed so an
externally-stopped daemon can't hang callers. On tg5040 rfkill unblocking is
`echo 0 > /sys/class/rfkill/rfkill1/soft` (rfkill1 = phy0); `rfkill.elf` is
only on the launcher PATH, not adb's.
