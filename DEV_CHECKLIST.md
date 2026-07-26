# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

---

## Game Switcher resumable filter + standalone-emulator resume

**Status:** built and staged 2026-07-26 (branch `game-switcher-list-resumable-games-only`).
Switcher filter + setting verified on Brick (tg5040) and Smart Pro S (tg5050). N64 resume
handshake verified on Brick only.

Two related changes:

1. **Switcher filter** — the Game Switcher lists only games with a resumable save state
   by default; Settings → System → "Game Switcher games" toggles between "Resumable only"
   and "All recent games" (config key `switcherresumableonly`). Non-resumable entries in
   "All" mode show `A START` instead of `A RESUME`.
2. **Standalone resume handshake** — nextui writes the slot to `/tmp/resume_slot.txt` on
   every launch; minarch consumes it in `State_resume()`. The N64 pak (mupen64plus +
   GLideN64 overlay) now consumes it too: `emu_ovl_consume_resume_slot()`
   (`emu_overlay.c`) is called once from `DisplayWindow::swapBuffers` after overlay init
   and auto-loads slots 0-7 via `M64CMD_STATE_SET_SLOT` + `M64CMD_STATE_LOAD`. Slots 8/9
   (fresh launch / minarch auto-resume) are ignored by design.

### On-device verification

- [ ] **N64 resume on Smart Pro S** — blocked on two things: the rebuilt
      `mupen64plus-video-GLideN64.so` must be pushed to that SD card's
      `Emus/shared/mupen64plus/` (each card has its own copy; the Brick's was updated
      2026-07-26, hash `ba02da6f`), and no N64 games are currently installed there.
      Test: save in-game via overlay, quit, resume from switcher → lands in the save.
      The plugin is platform-shared, so behavior should match the Brick.
- [ ] **N64 fresh launch still cold-boots** on Smart Pro S after the .so push (nextui
      writes slot 8 on non-resume launches; the hook must ignore it).
- [ ] **Brick Pro (pending hardware)** — switcher filter + setting, and N64 resume if the
      pak gains a tg4040 build.

### Standalone emulators still without resume

The repo ships exactly two standalone (non-minarch) emulator paks; everything else in
`skeleton/EXTRAS/Emus/` launches `minarch.elf` and already resumes via `State_resume()`:

| Pak | Emulator | Resume status |
|---|---|---|
| N64 | mupen64plus + GLideN64 overlay | **Works** (this change) |
| NDS | DraStic (closed-source binary) | **No resume, by design.** No overlay integration and no `.minui/` slot files, so NDS games are hidden by the resumable-only filter and show `A START` in "All" mode — honest behavior. Baking resume in would need DraStic's own savestate CLI/auto-load hooks, if any exist; the emu_overlay approach is not available without source. |

User-installed paks that are not part of this repo (e.g. a community PSP/PPSSPP pak) are
in the same position as NDS unless they write `.minui/<EMU>/<rom>.txt` slot files — if one
does, it must also consume `/tmp/resume_slot.txt` or the switcher's RESUME promise will be
cosmetic (exactly the bug fixed for N64).

### Gotchas

- The GLideN64 build needs three aarch64 static libs recreated after applying the patch
  (bundled ones are x86-64; the patch records them content-lessly). Recipe in
  `workspace/all/other/mupen64plus/README.md` — note especially that `libz.a` must be
  a self-built zlib ≥1.2.9, NOT the sysroot's (sysroot zlib lacks `adler32_z` → the .so
  builds fine but fails `dlopen` at runtime with a blank screen).
- mupen64plus patches + docs live in `workspace/all/other/mupen64plus/` (deduped
  2026-07-26; per-platform dirs keep only the gitignored source checkouts). The GLideN64
  patch is regenerated against the pinned `GLIDEN64_COMMIT` in the platform Makefile —
  regenerate against that pin, not upstream master.

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
