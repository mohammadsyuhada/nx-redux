# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

---

## Trimui Brick Pro (tg4040)

**Status:** ported and building on branch `brick-pro-support` (2026-07-25). Never run on
hardware — device ordered 2026-07-25.

Ported from upstream NextUI [PR #766](https://github.com/LoveRetro/NextUI/pull/766) plus five
follow-up commits that fix bugs in it: `9dffb9e8` (L4/R4 remapping), `d47fb074` (`MAX_LIGHTS` 5),
`ff202893` (rumble voltage cap), `a20481b7` (mute-buzz voltage), `bade2a41` (minput R-group
layout regression).

### Build and flash

```bash
make all                       # produces releases/NextUI-<date>[-branch]-brickpro.zip
```

Copy the `-brickpro` zip to the SD card as `MinUI.zip` and boot, or for iterating on a single
binary use `make deploy DEVICE=brickpro` (pushes `MinUI.zip` over adb and reboots).

### Already verified — do not re-derive

Confirmed by reading the stock rootfs out of
`sd_recovery_tg4040_brick_pro_v1.1.1_20260717.img` (ext4 at sector 126432; recipe in `OSD.md`):

- `TRIMUI_MODEL` is exactly `Trimui Brick Pro` → `DEVICE=brickpro`
- LED zones are `f1 f2 m lr rear`; brightness nodes `max_scale`, `max_scale_f1f2`,
  `max_scale_lr`, `max_scale_rear` (see the table in `INPUT_MAPPING.md`)
- `/usr/trimui/bin/trimui_inputd` exists, so the tg5040 boot path works unchanged, and its
  turbo interface is the same `/tmp/trimui_inputd/turbo_*` flag files
- the Smart Pro's analog-pad GPIO poke (PD14/PD18) is **commented out** in the Brick Pro's own
  `runtrimui.sh` — its sticks need no GPIO setup
- `trimui_osdd` is a distinct build from the Brick's, which is why the assembled
  `osd-brickpro/` overlay (built from `skeleton/SYSTEM/osd/device/brickpro/` at
  package time) exists

### 1. On-device verification (Brick Pro)

- [ ] **Boots and identifies correctly** — UI is 1024×768 at 3× scale with 7 main rows.
      Confirm `DEVICE=brickpro` (not `smartpro`); a mis-detect shows up as a 1280×720 layout.
- [ ] **SDL joystick indices** — *the main unverified assumption.* Open
      Settings → Input Tester and press everything. Expected: 9/10 = stick clicks (L3/R3),
      11/12 = FN1/FN2 (shown as L4/R4), 13/14 = volume, 15 = HOME, 8 = MENU.
      Wrong indices look like dead or swapped buttons, **not** a crash.
- [ ] **Analog sticks** — both nubs move the on-screen indicators; `L3+R3` enters calibration.
- [ ] **Hall-stick calibration** — check whether `/dev/ttyAS5` / `/dev/ttyAS7`
      (`settings_input.c:68-71`) exist on this model; calibration is a no-op if they don't.
- [ ] **LEDs, all five zones** — Settings → LED Control shows F1 key / F2 key / Top bar /
      Joysticks / L/R triggers. Verify each zone lights the part it names (in particular that
      `lr` is the *joysticks* here and `rear` is the *triggers* — the opposite of the Brick).
- [ ] **LED brightness coupling** — F1 and F2 track each other (shared `max_scale_f1f2`);
      the other three are independent.
- [ ] **Per-zone effect lists** — the code picks by node name (`lr` gets the extended LR
      effects, `rear` gets the standard set). If an effect renders wrong or does nothing,
      adjust the selection in `settings_led.c` (`led_page_create`).
- [ ] **OSD** — long-press `MENU` opens it. Check the background/tile layout at 1024×768, that
      toasts land on-screen, and that the battery widget works (stock layout implies it does).
- [ ] **Rumble** — capped at 2.5 V; confirm it isn't unpleasantly strong at max.
- [ ] **Mute toggle buzz** — the FN-switch mute pulse uses 900000 µV on this model.
- [ ] **Backlight** — brightness ladder uses the Brick curve; check the low end isn't black.
- [ ] **HOME button** — maps to `BTN_HOME`, currently inert (matching Smart Pro S). Decide
      whether it should *do* something here; if so, note that it arrives as a gamepad button
      (index 15), not `KEY_HOMEPAGE`, so keymon's tg5050 Home path does not apply.

### 2. Regression checks (Brick / Smart Pro / Smart Pro S)

Shared code moved, so these need a pass on at least one older device:

- [ ] **Input Tester shoulder rendering** — L1/L2 and R2/R1 pills must look exactly as before.
      This is precisely what upstream broke and had to fix in `bade2a41`.
- [ ] **`pak.cfg` bind round-trip** — bind a shortcut in a game, restart minarch, confirm it
      survived. `BTN_ID_L4`/`BTN_ID_R4` were inserted mid-enum and `LOCAL_BUTTON_COUNT` went
      16 → 18; bindings persist by *name*, so this should hold, but it is the one change that
      could silently corrupt existing configs.
- [ ] **LED page** — Brick still shows 4 zones, Smart Pro/S still 3, and existing
      `ledsettings*.txt` files still parse after the `MAX_LIGHTS` 4 → 5 bump.
- [ ] **OSD still starts** — the boot sync is now skipped when no `osd-$DEVICE` overlay
      exists and the daemon only starts if present; confirm Brick and Smart Pro are unaffected.
- [ ] **OSD assets after the dedup refactor** — the assembled trees were verified
      byte-identical to `66c76db8`, so this is a spot-check, not a proof: on Brick
      and Smart Pro S confirm the tile grid, `bg.png` and toast positions look
      unchanged.

### 3. Deliberately deferred

- **PortMaster device entry** — its detection keys off `/proc/device-tree/model`, which isn't
  recoverable from the firmware image. Brick Pro currently resolves to `trimui-smart-pro`,
  exactly as the Brick does today (no regression). To fix: read
  `cat /proc/device-tree/model` on the device and add an entry in
  `workspace/all/portmaster/portmaster.c` (`patch_device_info`) alongside the tg5050 one.
- **Display calibration / white point** — upstream's `displaycal.h` does not exist in this
  fork at all, so upstream's Brick Pro calibration commits (`64160e99`, `45406e12`) were out of
  scope. Porting white-point correction is its own piece of work.
- **Hardcoded platform paths in OSD widgets** — four of the 18 files in
  `skeleton/SYSTEM/osd/device/smartpros/widgets/` (`slider_backlight`,
  `slider_volume_global`, `toggle_mute`, `toggle_wifi`, each `set.sh`) differ
  from `common/` only by a hardcoded `/mnt/SDCARD/.system/tg50X0` path:
  `toggle_wifi` via a `SYSTEM_PATH=` variable (one changed line), the other
  three via `LD_LIBRARY_PATH` and `OSDCTL` (two changed lines each, no
  `SYSTEM_PATH` variable involved). Deriving that path instead — the widget
  already runs from a known location — would delete all four overrides. Left
  out of the dedup refactor because it changes shipped bytes and so forfeits
  that change's byte-equality proof; it needs its own on-device check of the
  backlight, volume, mute and wifi widgets.
- **Music widget tile is the wrong size on 1024×768** —
  `skeleton/SYSTEM/osd/common/widgets/app_music/skin/block4x2.png` is 540×260
  and `block4x2_sel.png` is 544×264, both byte-identical to the 1280×720
  `res/1280x720/` versions; the 1024×768 grid tiles are 556×268 and 560×272
  respectively. So Brick and Brick Pro draw the music
  widget's 4×2 tile 16 px too narrow and 8 px too short. Pre-existing well
  before the dedup refactor — the reorg only made it visible by putting the two
  variants side by side. Fixing it needs a 1024×768 asset that does not exist
  anywhere in the repo; until one is produced the file correctly stays in
  `common/`, since there is only one variant to ship.

### Gotchas

- The OSD sync is **hash-gated**. If you replace anything under `skeleton/SYSTEM/osd/`
  and the device doesn't pick it up, delete `/usr/trimui/osd/.nx_osd_stamp` and reboot.
- OSD assets are layered (`common/`, `res/<WxH>/`, `device/<dev>/`) and assembled
  by `scripts/assemble-osd.sh` at package time — edit the layer, not a device tree.
- `make deploy` now takes `DEVICE=` (e.g. `make deploy DEVICE=brickpro`). Passing
  only `PLATFORM=tg5040` deploys `brick`.
- Don't push an `.elf` over a running copy — stop the pak first. Only `nextui`/`minarch`
  need a reboot after pushing; other paks just need to not be running.
- Never `killall nextui` on device: the `kill -9` path powers the unit off.
