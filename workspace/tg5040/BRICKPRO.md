# Trimui Brick Pro (DEVICE=brickpro)

Bring-up reference for the Trimui Brick Pro. Distilled from the port
(`c0da09c7`, PR #53) and the on-hardware verification session of **2026-08-02**
(first unit with redux installed). Keep this as the durable "do not re-derive"
record; the live to-do list of anything still unverified lives in
`DEV_CHECKLIST.md`.

The Brick Pro is a **tg5040 build** — `PLATFORM=tg5040`, `DEVICE=brickpro`. It
runs the same tg5040 binaries as the plain Brick; the differences below are all
gated at runtime on `is_brickpro` (or `getenv("DEVICE")` in shared code —
never a tg5040-only global, since the source is shared with tg5050; see the
memory note on shared-code device detection).

## Build and flash

```bash
make all                          # produces releases/NextUI-<date>[-branch]-brickpro.zip
make deploy DEVICE=brickpro       # push MinUI.zip over adb + reboot (single-binary iterate)
```

Copy the `-brickpro` zip to the SD card as `MinUI.zip` and boot. `make deploy`
without `DEVICE=` deploys `brick`, not the Pro.

## How it differs from the plain Brick

Same 1024×768 panel, but it is a **physically larger** panel, so it uses the
Smart Pro's **2× layout, not the Brick's 3×**. This was confirmed on hardware
(a 3× layout was too large / clipped rows).

| Trait | Plain Brick | Brick Pro |
|---|---|---|
| UI scale (`FIXED_SCALE`) | 3× | **2×** |
| Main menu rows (`MAIN_ROW_COUNT`) | 7 | **11** (10 visible) |
| Analog sticks | none | **two** (I2C hall sticks) |
| Stick clicks L3/R3 (`JOY_L3/R3`) | 9 / 10 | 9 / 10 |
| Function keys L4/R4 (`JOY_L4/R4`) | — | **11 / 12** |
| `BTN_FN1/FN2` (music F1/F2 etc.) | L3/R3 | **L4/R4** |
| HOME button | none | **gamepad btn 15** (`KEY_HOMEPAGE`/172) |
| Volume +/- (`JOY_PLUS/MINUS`) | 14 / 13 | 14 / 13 |
| LED zones (`MAX_LIGHTS`) | 4 | **5** |
| LED brightness nodes | `max_scale`, `max_scale_lr` | + `max_scale_f1f2`, `max_scale_rear` |

Paks that act on the function keys **must** use `BTN_FN1`/`BTN_FN2`, not a
hardcoded L3/R3 — otherwise the Brick Pro's F1/F2 land on the wrong physical
key (this was the music-player L3/R3→L4/R4 bug fixed 2026-08-02).

## Kernel-level hardware facts (verified, do not re-derive)

Confirmed by reading the stock rootfs out of
`sd_recovery_tg4040_brick_pro_v1.1.1_20260717.img` (ext4 at sector 126432;
recipe in `../../OSD.md`) **and** live over adb on the unit, stock firmware
v1.1.1 / kernel 4.9.191 sun50iw10:

- `TRIMUI_MODEL` / MainUI model string is exactly `Trimui Brick Pro` →
  `DEVICE=brickpro`. `strings /usr/trimui/bin/MainUI | grep ^Trimui` returns
  that single line.
- Panel is 1024×768@60 (disp sysfs + fbset). A mis-detect would show a
  1280×720 layout instead.
- `overlay` is in `/proc/filesystems` (kernel 4.9 → the tg5040 tmpfs-staging
  OSD mount branch is the right one; its overlayfs rejects exFAT as a lower
  layer, so the OSD is staged through tmpfs, not mounted from SD as on tg5050).
- **LED**: `/sys/class/led_anim/` exposes `effect_{f1,f2,m,lr,rear}` plus
  `max_scale`, `max_scale_f1f2`, `max_scale_lr`, `max_scale_rear`. **Zone
  meaning is the opposite of the Brick's:** here `lr` is the **joysticks** and
  `rear` is the **triggers**. F1 and F2 share brightness (`max_scale_f1f2`);
  the other three are independent. There are also standalone `effect_l` /
  `effect_r` nodes (23 addressable RGB LEDs in `/sys/class/leds/`), so the two
  LR sides are individually addressable — unused, a possible future refinement.
- `trimui_inputd` and `keymon` run; `trimui_osdd` runs from
  `/usr/trimui/osd/trimui_osdd` (the overlay mount point). Turbo interface is
  the same `/tmp/trimui_inputd/turbo_*` flag files as the Brick.
- PD14/PD18 analog-pad GPIO poke (needed on the Smart Pro) is **commented out**
  in the Brick Pro's own `runtrimui.sh` — its sticks need no GPIO setup.
- busybox v1.27.2 — same as the Brick (tar guard / applet findings carry over).
- **`TRIMUI Player1` key bitmap decodes to the expected SDL order**: BTN
  304-318 (A,B,X,Y,TL,TR,SELECT,START,MODE,THUMBL,THUMBR → SDL 0-10), then
  KEY_F1(59), KEY_F2(60), VOLDOWN(114), VOLUP(115), KEY_HOMEPAGE(172) → SDL
  11-15. So **8=MENU, 9/10=L3/R3, 11/12=FN1/FN2, 13/14=volume, 15=HOME**.
  `ABS=3003f` → both sticks, analog triggers, and the dpad hat are all on the
  one device. HOME is `KEY_HOMEPAGE` emitted *by the gamepad device* (SDL
  joystick button 15), so keymon's tg5050 keyboard-device Home path does not
  apply here.
- `/dev/ttyAS*` — **none exist.** The Brick's/Smart Pro's serial hall-stick
  path does not apply; sticks are read over I2C (see below).

## Analog-stick calibration (I2C backend) — SHIPPED

`L3+R3` in Settings → Input Tester runs `cal_run_i2c` (dispatched on
`getenv("DEVICE")=="brickpro"` in `settings_input.c`). Verified on hardware
2026-08-02: both sticks rotate/center, `joypad.config` / `joypad_right.config`
written with plausible per-unit spans (center ~2009/2037, range ~1043–3110),
`/tmp/joypad_testmode` cleaned up on exit, live reload via
`/tmp/trimui_inputd/cal_update`.

Protocol (reverse-engineered from the unstripped stock `trimui_inputd`,
hardware-confirmed): bus `/dev/i2c-3`, two ADC chips — **0x29 = LEFT stick,
0x28 = RIGHT stick**. Read = `I2C_RDWR` write reg **0xB0**, read **4 bytes** =
X then Y, **big-endian** u16 each, 12-bit (0–4095). `/tmp/joypad_testmode`
present → inputd quiesces (stops feeding uinput and releases the bus) so the
calibrator is sole reader. Triggers (`z_min`/`z_max`) are left at inputd
defaults — trigger calibration was assessed and dropped (stock parity already
achieved; see the memory note).

## HOME button → OSD

The Brick Pro's dedicated HOME button arrives as `KEY_HOMEPAGE` (172). A short
press toggles the OSD (`toggle_osd()` in `workspace/tg5040/keymon/keymon.c`);
MENU long-press still opens it too. Verified responsive on hardware.

## OSD

Long-press MENU (or press HOME) opens the OSD at 1024×768. The battery widget
was **removed** from `skeleton/SYSTEM/osd/device/brickpro/osdlayout.json`
(2026-08-02). OSD assets are layered (`common/`, `res/<WxH>/`, `device/<dev>/`)
and assembled by `scripts/assemble-osd.sh` at package time — edit the layer,
not a device tree.

**Known cosmetic gap:** the Brick Pro's stock OSD `bg.png` has a teal accent
(`0,255,163`) mirrored at x=41 / x=982, y≈686–711 that the shipped Brick
background (plain black there) lacks — 138 px total, judged negligible. Restore
`device/brickpro/bg.png` if the accent ever matters.

## PortMaster

Brick Pro is detected as `trimui-brick-pro` via a marker override in
`workspace/all/portmaster/portmaster.c` (+ `device_info.txt` / `hardware.py`).
Without it PortMaster collapsed the Pro onto plain `trimui-brick` (shared
1024×768 resolution ambiguity), hiding analog-stick ports. Verified on device
2026-08-02.

## Gotchas

- OSD is overlay-mounted read-only at boot via a **tmpfs staging copy** on
  tg5040 (kernel 4.9 overlayfs rejects exFAT as a lower layer). If the OSD
  looks stale/dead, check `/proc/mounts` for `/usr/trimui/osd` and
  `/tmp/nx_osd_mount_failed`.
- Don't push an `.elf` over a running copy — stop the pak first. Only
  `nextui`/`minarch` need a reboot after pushing; other paks just need to not
  be running.
- **Never** `killall nextui` on device: the `kill -9` path powers the unit off.
- Rumble is capped at 2.5 V (`MAX_VOLTAGE` in `platform.c`, `/sys/class/motor/voltage`);
  the FN-switch mute pulse uses 900000 µV (`keymon.c`, double 100 ms pulse on gpio227);
  backlight uses the Brick brightness curve (`scaleBrightness`, UI level 0 → raw 1 via the
  `/dev/disp` `DISP_LCD_SET_BRIGHTNESS` ioctl). All three verified on hardware 2026-08-02:
  2.5 V is comfortable at max, the mute buzz fires, and raw 1 stays visible (not black).
