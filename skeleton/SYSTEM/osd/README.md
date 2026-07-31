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
| `res/<WxH>/` | Anything sized to the panel — `bg.png` (exactly 1024×768 / 1280×720), `block*.png` (124 px grid at 1024×768, 120 px at 1280×720), and the `show_*.sh` toast scripts, which differ only in their baked-in coordinates: 13 of 14 in `"x"` alone, `show_volume_msg.sh` in both `"x"` and `"y"` |
| `device/<dev>/` | `trimui_osdd` (closed-source, per-firmware, not interchangeable), `osdlayout.json`, plus any file that device overrides |

`device/smartpros/` is the only layer that overrides `common/`, down to
**three** widgets. Each is genuine platform divergence, not incidental drift —
if you are tempted to merge one into `common/` behind a platform conditional,
read this first:

- **`toggle_bt/set.sh`** — the two platforms need *opposite* behaviour on
  toggle-off. tg5040 must fully stop the BT stack, because on its xradio combo
  chip a live `bluetoothd` collapses WiFi throughput from ~330 KB/s to ~2 KB/s.
  tg5050 must *not*: it powers the adapter down and leaves `bluetoothd` running,
  because killing the daemon mid-call wedges whatever was talking to it.
- **`toggle_screenrecord/set.sh`** — different capture mechanisms entirely.
  tg5040 pipes `/dev/fb0` straight into `ffmpeg`; tg5050 runs
  `screenrecorder.elf`, which the foreground app feeds via the
  `/tmp/fb_mirror.raw` shm mirror.
- **`toggle_rumble/set.sh`** — the rumble motor sits on a different GPIO per
  platform (gpio227 on tg5040, gpio236 on tg5050). The two copies are identical
  but for that one path; keep them in sync.

Plus `stepper_fan_level/` — tg5050 is the only device with a fan, so that widget
has no `common/` counterpart to override.

Anything that genuinely differs between platforms — a whole *policy* or a single
hardware *constant* like `toggle_rumble`'s GPIO — is carried as a `device/<dev>/`
override, since there is no longer a build-time platform token to select it
inside `common/` (see Paths, below).

They live here rather than in a `platform/tg5050/` layer because tg5050 has
exactly one device, so the two would hold identical content. If a second tg5050
device is ever added, split them out at that point: `assemble-osd.sh` gains one
optional layer between `common` and `res`, and nothing else changes.

## Toggle icons

All four devices use the tg5050 icon set, which marks a toggle's active state
with a **solid white disc and a dark glyph**; the inactive state stays a faint
translucent disc. tg5040's stock icons distinguished the two states by glyph
alone, with no fill change, and NX Redux's own `toggle_screenrecord` icons were
already drawn solid — so adopting the tg5050 set made the row internally
consistent as well as clearer. The eight icons were promoted into `common/` and
their `device/smartpros/` overrides deleted.

`toggle_screenshot` remains the odd one out: its disc is fully transparent
rather than translucent, so it reads differently from every other toggle. Left
alone here; it needs an artwork decision, not a refactor.

## Paths

`.system` has no per-platform subdirectory. Every widget references the card's
system tree directly (`/mnt/SDCARD/.system/bin`, `/mnt/SDCARD/.system/lib`, …),
so `assemble-osd.sh` copies the layers verbatim — there is no build-time token
substitution.

Reading an environment variable at runtime is deliberately avoided because
`trimui_osdd` is closed-source and it is unverified whether it passes its
environment through to the widget scripts it spawns — if it does not, a runtime
variable would silently break these widgets on every device. Platform-divergent
behaviour is therefore baked in per device through the `device/<dev>/` layer
(see toggle_rumble, above), never via a conditional inside `common/`.

## Adding a device

Add `device/<dev>/` with `trimui_osdd` and `osdlayout.json`, add the device to `DEVICES` in
the `Makefile` with its `osd_res`, and add `export DEVICE="<dev>"` to the
platform's `launch.sh`. If it is a new resolution, add `res/<WxH>/` too.

Widget scripts reference `/mnt/SDCARD/.system/...` directly — there is no
platform level in the path. Anything that genuinely differs between platforms
(a GPIO number, a capture mechanism) belongs in the `device/<dev>/` layer, not
in a conditional inside `common/`.

`regular.ttf` (16 MB CJK font) is deliberately absent — the firmware's copy is
used, since the boot-time copy only ever overwrites files, never deletes them.
