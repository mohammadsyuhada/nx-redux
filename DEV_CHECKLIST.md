# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

---

## Game Switcher resumable filter + standalone-emulator resume

**Status:** built and staged 2026-07-26 (branch `game-switcher-list-resumable-games-only`).
Switcher filter + setting verified on Brick (tg5040) and Smart Pro S (tg5050). N64 resume
handshake now verified on both Brick and Smart Pro S (tg5050 verified 2026-07-27, see
`.superpowers/sdd/2026-07-26-flycast-dc-pak/n64-tg5050-report.md`).

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

- [x] **N64 resume on Smart Pro S** — done, 2026-07-27. `.so` and `N64.pak/` were
      already in sync with the repo on this card (no push needed — see report).
      User-verified end-to-end on real hardware (Mario Kart 64): saved to slot 0 via
      overlay, quit, relaunched — log shows `Core Status: State loaded from:
      Mario Kart 64 (U) [!]-3A67D998.st0` immediately after `[Overlay] Initialized
      successfully`, with no menu button touched, confirming the
      `/tmp/resume_slot.txt` handshake fired automatically. Matches Brick's
      already-verified behavior. Full evidence:
      `.superpowers/sdd/2026-07-26-flycast-dc-pak/n64-tg5050-report.md`.
      Found (not fixed): N64 save-state/SRAM data is not actually shared across
      devices despite `N64.pak/launch.sh`'s comment claiming it is — only the
      `.minui/<rom>.txt` resume marker is genuinely shared; the real `.st0`/`.eep`/
      `.mpk` files land under the per-device `$HOME` (`$USERDATA_PATH`), not
      `$SHARED_USERDATA_PATH`. Not exercised by same-device testing but would
      break resume across a physically-moved SD card.
- [x] **N64 fresh launch still cold-boots** on Smart Pro S — done, 2026-07-27.
      Verified directly (`/tmp/resume_slot.txt`=8, fresh launch): file consumed
      and unlinked, zero state/resume log lines, confirming
      `emu_ovl_consume_resume_slot()`'s `slot >= EMU_OVL_MAX_SLOTS` guard holds.
- [ ] **Brick Pro (pending hardware)** — switcher filter + setting, and N64 resume if the
      pak gains a tg4040 build.

### Standalone emulators still without resume

The repo ships exactly three standalone (non-minarch) emulator paks; everything else in
`skeleton/EXTRAS/Emus/` launches `minarch.elf` and already resumes via `State_resume()`:

| Pak | Emulator | Resume status |
|---|---|---|
| N64 | mupen64plus + GLideN64 overlay | **Works** (this change) |
| DC | flycast + nx_overlay | **Works**, incl. auto-save-on-quit to hidden slot 9 (consumed via `/tmp/resume_slot.txt`) — same handshake as N64. Built on branch `flycast-dc-pak` (staged, not yet merged). User-verified on both physical target devices: Brick (tg5040) hardware 2026-07-26 (HLE boot, overlay menu, save/load slots, quit-to-slot-9, switcher resume) and Smart Pro S (tg5050) in a later full session (real-BIOS boot verified on both devices, needs only `dc_boot.bin`; controller mapping shipped; tg5050 fully verified). Honestly still pending: full RA session test (a live achievement unlock, not just login/HTTPS), and Brick Pro (tg4040) once that hardware arrives. See `workspace/all/other/flycast/README.md` for details. |
| NDS | DraStic (closed-source binary) | **No resume, by design.** No overlay integration and no `.minui/` slot files, so NDS games are hidden by the resumable-only filter and show `A START` in "All" mode — honest behavior. Baking resume in would need DraStic's own savestate CLI/auto-load hooks, if any exist; the emu_overlay approach is not available without source. |

User-installed paks that are not part of this repo (e.g. a community PSP/PPSSPP pak) are
in the same position as NDS unless they write `.minui/<EMU>/<rom>.txt` slot files — if one
does, it must also consume `/tmp/resume_slot.txt` or the switcher's RESUME promise will be
cosmetic (exactly the bug fixed for N64).

### Deferred: auto-save-on-quit parity (minarch + N64)

The DC pak's quit behavior — every quit auto-saves to hidden slot 9 (`AUTO_RESUME_SLOT`)
with a switcher screenshot and repoints `.minui/<EMU>/<rom>.txt` at it, so RESUME always
returns to the exact quit moment — should be ported to the other resume-capable emulators.
Requested 2026-07-26; not started.

- [ ] **minarch (all libretro cores)** — a plain Quit from the in-game menu should
      `State_autosave()`-style save to `AUTO_RESUME_SLOT` (9), write the slot-9 switcher
      screenshot, and update the `.minui` slot file, mirroring what `Menu_saveState()`
      already does for the MENU+SELECT switcher path (`ma_menu.c:1388`) but to slot 9
      instead of `menu.slot`. Today only Save&Quit / switcher-entry / sleep save anything;
      a bare quit leaves the switcher pointing at a stale (or no) state. Note the resume
      side already works: nextui copies the `.minui` txt slot into `/tmp/resume_slot.txt`
      and `State_resume()` loads it — slot 9 maps to the `.state.auto` filename
      (`State_getPath`, `ma_saves.c:189-204`).
- [ ] **N64 (mupen64plus + GLideN64 overlay)** — port the DC QUIT branch: on
      `EMU_OVL_ACTION_QUIT`, save state to `EMU_OVL_AUTO_SLOT` + `emu_ovl_save_slot_screenshot(9)`
      before `M64CMD_STOP` (hook site: `DisplayWindow::swapBuffers` in
      `GLideN64-standalone.patch`; DC reference implementation:
      `flycast.patch` → `core/nx_overlay.cpp` `handle_menu_close()` QUIT branch).
      The shared `emu_overlay.c` already accepts slot 9 (added on `flycast-dc-pak`), so
      this is a small patch change — but it requires a GLideN64 rebuild, which means the
      static-libs gotcha below (self-built zlib ≥1.2.9) applies, plus pushing the rebuilt
      `.so` to every SD card's `Emus/shared/mupen64plus/`.

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

## Backlog: audio output routing + quality options (not started)

Requested 2026-07-27 (GitHub comment on the music player + flycast audio investigation).
Today every app plays through ALSA `default` = `softvol → dmix(48 kHz) → internal codec`,
hardcoded in the firmware's `/etc/asound.conf`. USB-C DACs enumerate as ALSA card 1 but
nothing routes to them; BT audio plumbing exists on-device (`bluealsa`, `pulseaudio`,
alsa-lib plugin modules all shipped) but the default PCM never reaches it. The 3.5 mm
jack "works" only because the codec chip switches speaker→headphone amp in hardware.

- [ ] **Music player** (the GitHub request): settings for output device (system default
      vs. detected USB DAC — cards visible in `/proc/asound/cards`), sample-rate mode
      (fixed 48 kHz vs. follow-source-when-sink-accepts, fallback to resample), SRC
      resampler quality (`src-sinc-fastest`/`medium`/`best` — currently hardcoded
      fastest), and buffer size (currently fixed 2048). Direct `hw:` output bypasses
      `softvol`, so device volume keys won't apply — fine for USB DACs (own volume),
      must be documented.
- [ ] **Sink-aware output for emulators** (flycast/minarch/N64): same routing question.
      flycast currently always opens SDL default at 48 kHz (deliberate — see
      `workspace/all/other/flycast/README.md` audio section). A USB/BT-aware path would
      need per-sink rate choice (USB DACs often prefer source rate) and a volume story.
      The internal codec HAS hardware volume controls (`DAC Volume`, `HPOUT Gain` —
      verified via amixer on tg5050), so a hw-volume route exists if softvol is bypassed.
- [ ] **BT audio**: confirm whether the stock/NextUI stack can route game audio to BT at
      all (bluealsa PCM open from an app), before promising it anywhere.

**Per-sink rate policy (design decision, 2026-07-27):** the output rate must follow the
selected sink, not a global constant — internal codec via dmix = always 48 kHz (dmix is
fixed there; anything else hits alsa-lib's linear resampler, the exact bug fixed in
flycast); direct `hw:` to the internal codec or a USB DAC = open at source rate,
negotiated, falling back to the nearest supported rate with an in-app quality resample
(SRC/SDL), never ALSA `plug`; Bluetooth = match whatever rate the A2DP codec negotiated
via the bluealsa PCM. Crucially, the rate CANNOT be autodetected through the `default`
PCM — `plug` accepts any rate and converts silently — so the policy keys off the
user-selected output device: `default` → force 48 kHz, `hw:X`/bluealsa → negotiate.

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

---

## Thread-pinning `taskset` now actually works — re-verify everything that uses it

**Status:** fixed and staged 2026-07-27 (branch `flycast-dc-pak`, task 11 fix round).
tg5050 (Smart Pro S) is now fully verified: native `taskset`, PS.pak's affinity probe,
DC.pak's pinning, and N64.pak's pinning (2026-07-27, once a game was installed) all
confirmed on real hardware. tg5040 (Brick) N64.pak re-verification with pinning
actually active is still pending.

`skeleton/SYSTEM/shared/bin/taskset` — the binary every pak's `taskset` calls resolved
to via `PATH` — was a `-static` build that aborted with `FATAL: kernel too old` on the
Brick's real 4.9.191 kernel. Every call site wraps `taskset` in `2>/dev/null`, so this
failure was completely silent: **every existing thread-pinning call in the repo has
been a no-op on tg5040 this whole time**, not just for DC.pak. Fixed by dropping
`-static` from `workspace/all/taskset/Makefile` and shipping working, platform-specific,
dynamically-linked binaries at `skeleton/SYSTEM/{tg5040,tg5050}/bin/taskset` (which
shadow the old shared path via existing `PATH` ordering — no call-site changes needed).
The old shared binary was deleted this round, so **there is no fallback anymore** if a
platform's `taskset` turns out to be broken on some device.

- [ ] **N64.pak pinning on Brick, with pinning actually active** — re-verify audio/perf
      with real affinity applied. The masks and the thread-name heuristic
      (`skeleton/EXTRAS/Emus/tg5040/N64.pak/launch.sh:100,108,127`) were written and
      shipped blind, against a `taskset` that always silently failed; they were never
      exercised for real until this fix, the same way DC.pak's pinning was
      evidence-gated by direct measurement (task-11 report) before shipping.
      **Known gap, measured on Smart Pro S 2026-07-27 (reproduced twice, incl. a real
      user session):** the "pin the busiest `mupen64plus`-named thread" heuristic only
      pins ONE of the (at least) two non-main threads named `mupen64plus`; the other is
      left on the unrestricted 0-7 mask. Measured impact is small: its load is bursty
      init/loading work (~3.6% during boot, ~0% in live gameplay), and since NextUI only
      brings cpu0-1/4(/5) online (8-core silicon run as effective 4-core by boot policy),
      "unrestricted" still lands it on the contended cores. Fix when re-verifying
      (deliberately NOT fixed on `flycast-dc-pak` — evidence said low impact, and the
      change belongs with a measured Brick re-verify): pin all unmatched threads to
      LITTLE by default (else-branch in the scan loop; DC.pak's `pin_threads()` is the
      reference pattern).
- [x] **tg5050 `taskset` + PS.pak on Smart Pro S** — done. The tg5050 binary
      (`skeleton/SYSTEM/tg5050/bin/taskset`) runs natively on real Smart Pro S
      hardware (no "kernel too old" abort). PS.pak's `taskset -c 4,5` launch line
      and its `pin_threads` calls (`skeleton/SYSTEM/tg5050/paks/Emus/PS.pak/launch.sh`)
      were confirmed applying real affinity, not silently falling back to a bare
      launch.
- [x] **DC.pak on Smart Pro S** — done. Same taskset binary; DC.pak's dual-cluster
      pinning was confirmed exact on real Smart Pro S hardware.
- [x] **N64.pak on Smart Pro S** — done, 2026-07-27, once a game was installed.
      All three bare `taskset` calls (`skeleton/EXTRAS/Emus/tg5050/N64.pak/launch.sh:87,95,114`)
      confirmed applying real affinity on two independent sessions (my own launch +
      a real user session): main thread → cpu4, video thread → cpu5, `m64pwq`/`mali-*`
      helpers → cpu0-1, all correct. Known gap (measured, not fixed): a second,
      unnamed `mupen64plus` thread is never pinned — see the Brick bullet above,
      where the fix is recorded as prescribed follow-up. Full evidence + CPU% tables:
      `.superpowers/sdd/2026-07-26-flycast-dc-pak/n64-tg5050-report.md`.
