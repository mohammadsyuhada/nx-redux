# OSD source tree

This directory is a **build input, not shipped content.** Unlike the rest of
`skeleton/`, nothing here reaches a device at this path. `scripts/assemble-osd.sh`
composes these layers per device during `make package`, into the directory shape
each platform's `launch.sh` reads:

- **tg5040** — layered: `osd/` (common) plus `osd-$DEVICE/` (res + device)
- **tg5050** — flat: `osd/` (common + res + device)

## Layers

Later layers win.

| Layer | Holds |
|---|---|
| `common/` | What every device shares — widgets, icons, progress art, `key.wav` |
| `res/<WxH>/` | Anything with a pixel coordinate baked in — `block*.png` (124 px grid at 1024×768, 120 px at 1280×720) and the `show_*.sh` toast scripts, which differ only in their baked-in coordinates — 13 of 14 in `"x"` alone, `show_volume_msg.sh` in both `"x"` and `"y"` |
| `device/<dev>/` | `trimui_osdd` (closed-source, per-firmware, not interchangeable), `osdlayout.json`, `bg.png`, plus any file that device overrides |

`device/smartpros/` is the only layer that currently overrides `common/`. It is
the only device with a fan, and it carries 18 files across 10 widgets
(`slider_backlight`, `slider_volume_global`, `static_cpu_freq`, `toggle_bt`,
`toggle_led`, `toggle_mute`, `toggle_rumble`, `toggle_screenrecord`,
`toggle_screenshot`, `toggle_wifi`) that differ from tg5040's.

Those 18 differ along a **platform** axis rather than a device one — a hardcoded
`/mnt/SDCARD/.system/tg50X0` path — via `SYSTEM_PATH` in `toggle_wifi`, via
`LD_LIBRARY_PATH` and `OSDCTL` in the others — plus genuinely different BT, LED
and screen-record logic. They live here because tg5050 currently has exactly one
device, so a `platform/tg5050/` layer would hold identical content. If a second
tg5050 device is ever added, split them out into `platform/<plat>/` at that
point: `assemble-osd.sh` gains one optional layer between `common` and `res`,
and nothing else changes.

## Adding a device

Add `device/<dev>/` with the three model files, add the device to `DEVICES` in
the `Makefile` with its `osd_res`, and add `export DEVICE="<dev>"` to the
platform's `launch.sh`. If it is a new resolution, add `res/<WxH>/` too.

`regular.ttf` (16 MB CJK font) is deliberately absent — the firmware's copy is
used, since the boot-time copy only ever overwrites files, never deletes them.
