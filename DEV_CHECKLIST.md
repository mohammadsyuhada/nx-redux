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
- [ ] **OSD stock restore** — overlay in `/proc/filesystems`; OSD overlay mount present after
      boot; Settings → Restore stock OSD round-trip (restore, verify rootfs matches
      `skeleton/SYSTEM/osd-stock/brickpro.manifest.md5`, reboot, NX OSD returns)
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
- [x] **OSD still starts** — the boot sync is now skipped when no `osd-$DEVICE` overlay
      exists and the daemon only starts if present. *Verified 2026-07-26 on Brick and
      Smart Pro S (sync fired, daemon running, scripts executable). Smart Pro:
      untestable — no hardware.*
- [x] **OSD assets after the dedup refactor** — the assembled trees were verified
      byte-identical to `66c76db8`, so this is a spot-check, not a proof.
      *Verified 2026-07-26 on Brick and Smart Pro S: tile grid, `bg.png` and toast
      positions unchanged.*
- [x] **Toggle icons on tg5040 — deliberate visual change, needs eyes.** Brick,
      Brick Pro and Smart Pro now use the tg5050 icon set: an active toggle draws
      a solid white disc with a dark glyph instead of the old faint translucent
      disc. Affects wifi, bluetooth, LED, rumble and mute. This is the one part
      of the OSD work that is *not* byte-identical to `66c76db8` — 8 files on
      3 devices, verified to be exactly those and nothing else. Confirm on a
      Brick that the active state reads correctly against that model's OSD
      background, and that `toggle_screenshot` (still a fully transparent disc)
      does not look broken next to them. *Verified 2026-07-26 on Brick. Smart
      Pro: untestable — no hardware; same assets as Brick, differing only in the
      1280×720 res layer. Brick Pro: pending hardware.*
- [x] **LED toggle on Smart Pro S — now runs tg5040's script.** `toggle_led`'s
      tg5050 override was merged into `common/`; the shared version writes a
      superset of nodes (`max_scale`, `_lr`, `_f1f2`, plus `_rear`, which does
      not exist on tg5050 and is swallowed by `2>/dev/null`). Confirm the LED
      toggle still turns all three zones on and off at the configured
      brightness. *Verified 2026-07-26 on Smart Pro S.*
- [x] **LED brightness now picks the running model's settings file.** All three
      `ledsettings*.txt` live in `.userdata/shared/`, which is shared across
      platforms (`sync.c:508-510` excludes all three precisely because they
      coexist), so a card moved between models carries several. The old code
      probed `_brickpro` → `_brick` → plain and took the first that existed —
      meaning a Smart Pro that had ever been in a Brick read the *Brick's*
      brightness, permanently, since Settings writes the plain file on both
      Smart Pro and Smart Pro S. It now selects by hardware instead: `_rear`
      node present → Brick Pro, else fb0 width 1024 → Brick, else the plain
      file. **This fixes a pre-existing bug on Smart Pro**, so verify there too,
      not just on Smart Pro S: set a distinctive brightness in Settings, toggle
      the LEDs off and on from the OSD, and confirm they come back at that
      brightness rather than another model's. *Verified 2026-07-26 on Smart
      Pro S — on-device probe confirmed the discriminator's inputs (no `_rear`
      node, fb0 width 1280 → plain file). Smart Pro: untestable — no hardware;
      the fix is code-identical there, selected by the same fb0-width branch.*
- [ ] **Brick Pro OSD background is now the Brick's.** `bg.png` moved into
      `res/<WxH>/` (it is exactly panel-sized, so it is resolution-locked art).
      The 1280×720 pair was byte-identical, so Smart Pro / Smart Pro S are
      unaffected. Brick Pro's stock version differed in 192 of 786,432 pixels:
      54 are ±1 alpha rounding on the panel corners (y≈56–80, invisible), and
      the other **138** are a teal accent (`0,255,163`) mirrored at x=41 and
      x=982, y≈686–711 — a 28 px fully-opaque core plus 110 px of anti-aliased
      edge. Brick's background is plain black there, so Brick Pro loses both
      accents. Judged negligible while the hardware is
      unavailable — look at it once a Brick Pro is in hand and restore
      `device/brickpro/bg.png` if the accents matter.
- [x] **CPU widget on Smart Pro S is now display-only.** `static_cpu_freq`'s
      `"launch"` was `set.sh` on tg5050 and empty on tg5040; it is now empty
      everywhere. `OSD.md` records that `set.sh` as a no-op stub, so tapping the
      widget should have done nothing anyway. *Verified 2026-07-26 on Smart
      Pro S — nothing visibly regressed.*

### 3. Deliberately deferred

- **PortMaster device entry** — its detection keys off `/proc/device-tree/model`, which isn't
  recoverable from the firmware image. Brick Pro currently resolves to `trimui-smart-pro`,
  exactly as the Brick does today (no regression). To fix: read
  `cat /proc/device-tree/model` on the device and add an entry in
  `workspace/all/portmaster/portmaster.c` (`patch_device_info`) alongside the tg5050 one.
- **Display calibration / white point** — upstream's `displaycal.h` does not exist in this
  fork at all, so upstream's Brick Pro calibration commits (`64160e99`, `45406e12`) were out of
  scope. Porting white-point correction is its own piece of work.
- **~~Hardcoded platform paths in OSD widgets~~** — done. The four overrides
  that differed from `common/` only by a `/mnt/SDCARD/.system/tg50X0` path are
  gone: the path is now written `__PLATFORM__` in `common/` and substituted per
  device by `assemble-osd.sh`. Assembled output is byte-identical, so this
  needs no hardware check of its own.
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

- OSD is overlay-mounted read-only at boot — from the SD directly on tg5050,
  via a tmpfs staging copy on tg5040 (its kernel 4.9 overlayfs rejects exFAT
  as a lower layer); no rootfs writes and no stamp on either. If the OSD looks
  stale or dead, check `/proc/mounts` for `/usr/trimui/osd` and
  `/tmp/nx_osd_mount_failed`.
- OSD assets are layered (`common/`, `res/<WxH>/`, `device/<dev>/`) and assembled
  by `scripts/assemble-osd.sh` at package time — edit the layer, not a device tree.
- `make deploy` now takes `DEVICE=` (e.g. `make deploy DEVICE=brickpro`). Passing
  only `PLATFORM=tg5040` deploys `brick`.
- Don't push an `.elf` over a running copy — stop the pak first. Only `nextui`/`minarch`
  need a reboot after pushing; other paks just need to not be running.
- Never `killall nextui` on device: the `kill -9` path powers the unit off.
