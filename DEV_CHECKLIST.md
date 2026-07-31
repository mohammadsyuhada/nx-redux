# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## N64 pre-launch options + overlay Options removal (built 2026-07-30)

**Status:** Tasks 1-5 (N64 `options.sh` + shared `nx_paths.sh`, the per-game
`--set` override path in both N64.pak `launch.sh` scripts, and the outright
removal of the in-game overlay Options UI) are built, compile clean on both
platforms, and staged (not committed).
**Verified 2026-07-30 on tg5040 Brick** (automated over adb: `sh -n` on all
scripts, busybox awk `--set` translation byte-exact, seeding idempotence
(`.initialized` honored), live launch argv carrying
`--nosaveoptions --set Video-GLideN64[UseNativeResolutionFactor]=3`,
`mupen64plus.cfg` md5 byte-identical across a real play session, rebuilt
GLideN64 `.so` initializing the overlay at 1024×768; UI pass: context-menu
entry, per-game edit visibly applying with revert-deletes-file, "Clear All
Overrides" deleting a planted two-key override file end to end, Emulator
Settings listing both emulators with global edits applying, N64 and DC in-game
menus showing Continue / Save State / Load State / Quit only with
save/load/slot-screenshots/quit-autosave intact; fresh-card round-trip:
config dir wiped → "Emulator Options" alone seeded `mupen64plus.cfg`
byte-identical to `default-brick.cfg` + `.initialized`, an edit survived the
first game launch un-re-seeded, per-game `--set` never leaked into the global
cfg, and the second launch left the cfg byte-identical — see the Gotcha below
about the first launch's one-time rewrite; 44.1 kHz sink session via a
simulated `/tmp/nx_audio_sink` (`rates=44100`): argv carried
`--set Audio-SDL[OUTPUT_FREQUENCY]=44100`, cfg md5 byte-identical after the
session with `OUTPUT_FREQUENCY = 48000` untouched on disk — the audio-rate
drift bug is dead; a both-rates sink correctly stayed at native 48 kHz,
confirming `nx_pick_audio_rate`'s exact-match preference live). Real-BT
audio output itself was verified in the audio-routing round and is not
re-tested here. What remains below is the Smart Pro S pass.
This makes mupen64plus the second
pre-launch-options adopter after flycast — same schema-driven `options.elf`,
same nxredux context-menu probe and `Emulator Settings` picker, nothing N64 has
of its own beyond an `options.sh` and its config storage. Design:
`docs/superpowers/specs/2026-07-30-n64-prelaunch-options-design.md`.

Shape of the shipped feature: the game-list context menu's "Emulator Options"
item and Tools → Emulator Settings both key off the same capability marker —
does the pak ship an `options.sh`. For N64, per-game overrides go to
`$SHARED_USERDATA_PATH/N64-mupen64plus/config/<device>/games/<rom-base>.cfg`
(one key per changed value; `<rom-base>` collapses a folder-m3u game's discs to
the folder name, `nx_paths.sh`'s `nx_rom_base()`), while global defaults edit
the per-device `mupen64plus.cfg`. `launch.sh` reads the override with an awk
step emitting `--set Section[KEY]=VALUE`, applied under `--nosaveoptions` so
every `--set` is a session-virtual runtime override that ui-console never
persists back to `mupen64plus.cfg` — the mupen64plus analogue of flycast's
`cfgSetVirtual`. Config-dir resolution + first-run `mupen64plus.cfg` seeding
were split out of `launch.sh` into a sourced `nx_paths.sh` so options can be
edited before a game has ever launched without the two scripts drifting.
Alongside this, the overlay Options UI was deleted outright from
`workspace/all/common/emu_overlay.c`/`.h` (the old `EMU_OVERLAY_HIDE_OPTIONS`
env-var gate is gone entirely — there is nothing left to hide), and both flycast
binaries and the shared GLideN64 `.so` were rebuilt against the trimmed overlay.

### On-device verification

- [ ] **Smart Pro S (tg5050) pass** — deploy the staged tg5050 artifacts
      (`N64.pak` scripts, `DC.pak` launch.sh + flycast, shared GLideN64 `.so`)
      and repeat the Brick checks: context-menu + Emulator Settings wiring,
      per-game override apply/revert, `--set` virtuality hash, N64 + DC
      in-game menus (four entries, save/load intact).

### Gotchas

- **On a freshly-seeded config, the FIRST game launch rewrites
  `mupen64plus.cfg` once. This is normal, not the drift bug.** Why: the
  shipped `default-*.cfg` files predate ~35 settings newer GLideN64 knows
  (unused `hk*` hotkey bindings), so on the first launch the plugin adds them
  with empty defaults and saves the file — once. Verified harmless on the
  Brick 2026-07-30: existing values survive, per-game `--set` values do NOT
  get written in, and every launch after the first leaves the file
  byte-identical. Practical consequence, and the only reason this note
  exists: when running the md5 "cfg unchanged" checks (44.1 kHz item above,
  Smart Pro S pass), launch a game once BEFORE taking the "before" hash —
  otherwise this one-time rewrite looks like the drift bug came back.
  Deliberately not fixed (would mean regenerating all four `default-*.cfg`
  files and re-verifying seeding per device, to avoid one harmless write);
  fold the missing keys in whenever those defaults are next touched anyway.
  **When this section is verified and deleted** (after the Smart Pro S pass),
  this note moves to `workspace/all/other/mupen64plus/README.md` — it
  describes permanent GLideN64/default-cfg behavior, not a one-off bring-up
  fact. Move the correct patch-regen recipe there in the same edit (the
  GLideN64 patch's `.a` entries are content-less placeholders: regenerate
  WITHOUT `--binary`, round-trip with
  `git apply --check -R --exclude='src/GLideNHQ/lib/*'`).

---

## DC netplay pre-launch wizard (built 2026-07-29)

**Status:** Tasks 1-7 (nxredux Y-launch, the standalone `netplay.elf` wizard, and the
rewritten DC.pak `launch.sh`) are built, compile clean on both platforms, and staged
(not committed). This supersedes the shell-based overlay-toggle flow entirely — see
`workspace/all/other/flycast/README.md`'s Netplay chapter for what changed and why, and
`docs/superpowers/specs/2026-07-29-netplay-prelaunch-wizard-design.md` for the design.
**Verified 2026-07-29/30 on tg5040 Brick** (WiFi pairing + session in real use; solo
failure-path pass over adb: rc contracts — no `--game` rc=2, `--cleanup` no-op rc=0 —
first-screen cancel, hotspot AP up/`NxRedux-<code>`/teardown-on-cancel within budget,
kill-and-heal of an orphaned AP healed instantly, migration guard rewrote planted
`GGPO=yes`/`device2=0`, `emu.cfg` md5 stable across all cancels). What remains below is
the two-device pair matrix and its bundled checks.

Shape of the shipped feature: `Y` on a netplay-capable ROM (Phase 1: DC.pak only,
marked by a `netplay` file beside its `launch.sh`) writes `/tmp/netplay_launch` and
launches the pak normally; root Search moved from `Y` to `START`; a "Launch with
Netplay" context-menu item does the same thing as Y. `DC.pak/launch.sh` sees the flag,
runs `netplay.elf` (role → hotspot/WiFi → UDP 55441 broadcast discovery / TCP 55440
handshake → optional rsync save sync on TCP 18731 → `/tmp/netplay_session`), then
starts flycast with every netplay value as a **virtual** `-config` (`GGPO`,
`ActAsServer`, `server`, `EnableUPnP`, `device2`) — `emu.cfg` is never written on this
path. An idempotent migration guard reverts a leftover `GGPO=yes/True` and
`device2=0` from the old overlay flow on every launch. `--cleanup` (called after the
game exits, and defensively at the wizard's own startup) tears down any hotspot,
restores prior WiFi, stops a stray rsyncd, and removes the session file, bounded to a
19 s budget.

### On-device verification

- [ ] **Pair test, Brick ↔ Smart Pro S, a netplay-capable DC game (e.g. MvC2), all
      four quadrants** (host/client × hotspot/WiFi) — *WiFi pairing + a full session
      already proven in real use 2026-07-29/30; still open: the hotspot quadrants, the
      mismatch `REJECT`, the save-sync integrity checks, the post-session port sweep,
      and (bundled here) the full-session `emu.cfg` hash from the item below* — from
      the spec's hardware verification plan plus Task 4/5/7's device items:
      - pairing completes with no IP ever typed in any quadrant (UDP 55441 broadcast
        + TCP 55440 handshake, not the old ping sweep).
      - only hosts broadcasting the *identical* game name appear in the client's
        list; a deliberately mismatched game/protocol produces a named `REJECT`
        error on the CLIENT's screen only — it falls back to its host list
        (WiFi) or exits the wizard (hotspot, nothing else to connect to) — while
        the HOST shows no error at all: it silently sends `REJECT` and keeps
        showing "Waiting for player...", by design. Do not expect (or misreport
        the absence of) a host-side error here.
      - save sync: client's `netplay-data` copy of `vmu_save_*.bin`/`dc_nvmem.bin`
        md5-matches the host's real originals after the pull, at WiFi speed; a pull
        attempted from a third, non-paired IP is refused (rsync `hosts allow`); no
        `.netplay-staging` directory survives a successful sync; a deliberately
        failed pull leaves the client's existing saves byte-identical (no partial
        commit); the progress bar renders correctly at both devices' screen scales.
      - both devices clear flycast's untimed "Starting Network" modal and P2 inputs
        work (virtual `device2=0` on both sides).
      - client's own real saves are untouched after the session (it played entirely
        out of `netplay-data/`); host's `netplay-backup/` holds its pre-session copy.
      - after the session, `netstat` shows no listener on 55440/18731 on either
        device, and `pidof hostapd`/`pidof rsync` are clean.
- [ ] **`emu.cfg` untouched by a netplay run** — hash/diff it before and after a full
      hosted and a full joined session on both devices: no key changes at all (the
      migration guard's one-time rewrite aside), confirming the six netplay values
      really are virtual-only — including `DCNet`, which the netplay block forces to `no`
      virtually and must therefore leave at its overlay value on disk.

### Gotchas

- flycast's GGPO/DCNet internals are unaffected by the wizard rewrite and still hold:
  GGPO always drives Dreamcast **port B** as player 2, so `device2` must be a real
  pad on both sides or the second device's inputs reach nothing (port B is empty by
  default); UPnP is forced off because `ggpo.cpp:801-804` punches a router mapping
  **before** checking `ActAsServer`, so both roles would otherwise leak an 86400 s
  lease; `server =` semantics (empty → loopback deadlock, flycast's "Starting
  Network" modal has no timeout, Cancel is the only exit) are still exactly what a
  broken pairing looks like at the flycast layer, even though the wizard should now
  make that unreachable in normal use. **DCNet is likewise forced off** (virtually,
  `network:DCNet=no` last in `NETPLAY_ARGS`): it relays the emulated modem to an
  external cloud service, an input the GGPO lockstep never synchronizes, so leaving a
  per-game or global `DCNet = yes` in force on a session is a desync. It is on by
  default now, so this is the common case, not a corner one.
- **`PerGameVmu` must stay off.** With it on, flycast writes the per-game VMU file as
  `<gameId>_vmu_save_A1.bin`, which does not match the wizard's `--fetch-files` glob
  (`vmu_save_*.bin`) any more than it matched the old tar glob — the sync silently
  ships an incomplete card, with "Peer verification failed" the only symptom.
- **Switcher-resume desync.** A netplay session must be started as a fresh launch,
  never a switcher RESUME on a netplay-capable ROM — GGPO's boot-time sync desyncs
  against a mid-session state load. This used to mean "must be a plain A START"; it
  is now "must be a plain **Y** START" (or the "Launch with Netplay" context item) —
  an ordinary switcher resume on a netplay-capable game is exactly what the Y branch
  exists to bypass, so don't use it to start netplay.
- **`show2.elf` must still be killed with `killall -9`, never plain `killall`**, if
  anything ever drives it again — SIGTERM is swallowed (SDL traps it into an
  `SDL_QUIT` its daemon loop never pumps). This no longer applies to netplay at all
  (the wizard has its own screens and owns its own process lifecycle); `show2.elf`
  remains only the first-boot install splash.
- **Migration guard fires at most once per card**, by construction: it reverts
  `GGPO=yes/True → no` and `device2=0 → 10` on every launch, but since the new flow
  never writes either key back to `emu.cfg`, there's nothing left for it to revert
  after the first post-upgrade launch. A user who deliberately sets `device2 = 0` in
  flycast's own Controls UI for local two-pad play will have it reverted the one time
  the old value is still on disk — same accepted ambiguity as the original overlay
  flow, just incapable of recurring afterward.
- A stale `server =` (and `EnableUPnP = no`) left in `emu.cfg` by an old, pre-wizard
  session is harmless and deliberately not swept — flycast only reads it on the GGPO
  path, where the wizard's virtual value overrides it anyway.
- **`wizard_wifi.c`'s picker/scan loops don't poll `app_quit`** — up to 120 s
  (`WIZ_PICKER_TIMEOUT_MS`). This is what makes a SIGTERM-then-SIGKILL while sitting
  in the WiFi/hotspot picker a real window for an orphaned AP, i.e. exactly the
  scenario the kill-and-heal check above exists to catch. Tracked as a follow-up in
  `DEV_TODO.md` (make the loops poll `app_quit`); not fixed in this build.

---

## Upstream-port + fix round (built 2026-07-27)

**Status:** ten DEV_TODO items implemented 2026-07-27, committed as `1ccc1030`. All
changed elfs + the rebuilt GLideN64 `.so` + N64 launch.sh are pushed to both cards
(Brick and Smart Pro S, md5-verified 2026-07-27).

Full deploy: `make all`, flash zip. Quick iterate: push the single rebuilt `.elf` (reboot required
for nextui/minarch pushes — see Gotchas at the bottom of this file).

### On-device verification

- [ ] **SRAM read unification** (`ma_saves.c`, upstream #667) — save in-game with
      Save Format = SRM (compressed), switch back to the default (uncompressed), relaunch:
      the in-game save must be intact, and after the next in-game save the `.srm` should be
      raw (`head -c8` no longer `#RZIPv1#`). Regression: raw `.srm` still loads, and a
      RetroArch-imported compressed `.srm` loads under the default setting.
- [ ] **Resampler leak fix** (`api.c`, upstream #697) — play any PAL game (or set
      Core Sync = Native) for ~10 min; `VmRSS` in `/proc/<minarch pid>/status` must stay
      flat (before the fix it grew ~11 MB/min).
- [ ] **RETRO_ENVIRONMENT_SHUTDOWN** (`ma_environment.c`, upstream #699) — Doom
      (PRBOOM.pak): in-game menu → Quit must exit cleanly back to nextui. FBNeo: launch a
      known-bad ROM, any button on the error screen must exit. Check the switcher isn't
      left pointing at garbage for the PRBOOM quit (core dies before the menu's autosave —
      quit here goes through the env callback, not ITEM_QUIT, so no slot-9 autosave fires;
      confirm RESUME behaves sanely, i.e. falls back to previous state or START).
- [ ] **Rewind re-init fix** (upstream #728 + early-out) — enable rewind, play: rewind
      works; changing a rewind option mid-game still takes effect (buffer size change →
      re-init happens); in-game "Restore Defaults" no longer hitches for seconds with a
      big rewind buffer; game launch with rewind enabled allocates once (single
      "Rewind:" init in the log, if logging shows it).

### Follow-ups discovered while implementing

- The `keepAwakeUSB` config key is camelCase, matching its immediate neighbours
  (`disableSleep`, `sshOnBoot`) rather than the older lowercase style the DEV_TODO entry
  suggested — deliberate.
- CFG setters were NOT given per-setter early-returns: `CFG_sync()` now compares content
  before writing, which subsumes the I/O benefit (a redundant set costs a read+compare,
  never a write).
- Core-requested SHUTDOWN (env cmd 7) deliberately does NOT trigger the slot-9 autosave —
  it fires mid-`retro_run` where a state save is unsafe, and the quitting core (Doom quit
  menu / failed init) rarely has a moment worth resuming. Revisit only if PRBOOM quit
  verification above shows a bad switcher experience.

---

## Game Switcher resumable filter + standalone-emulator resume

**Status:** merged to `main` 2026-07-26 (`13a4d3c4`, PR #54). Verified on Brick and
Smart Pro S (filter, setting, N64 resume handshake, fresh-launch cold boot). Only the
Brick Pro pass remains.

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

- [ ] **Brick Pro (pending hardware)** — switcher filter + setting, and N64 resume if the
      pak gains a tg4040 build.

### Standalone emulators still without resume

The repo ships exactly three standalone (non-minarch) emulator paks; everything else in
`skeleton/SYSTEM/<plat>/paks/Emus/` launches `minarch.elf` and already resumes via `State_resume()`:

| Pak | Emulator | Resume status |
|---|---|---|
| N64 | mupen64plus + GLideN64 overlay | **Works** (this change) |
| DC | flycast + nx_overlay | **Works**, incl. auto-save-on-quit to hidden slot 9 (`/tmp/resume_slot.txt`, same handshake as N64). Merged `99985dec` (PR #56), verified on Brick + Smart Pro S. Still pending: full RA session test (a live achievement unlock, not just login/HTTPS), and Brick Pro once hardware arrives. Details: `workspace/all/other/flycast/README.md`. |
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

**Status:** ported 2026-07-25, merged to `main` 2026-07-26 (`c0da09c7`, PR #53). Never run
on hardware — device ordered 2026-07-25.

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

Confirmed live over adb on the actual unit, stock firmware v1.1.1 / kernel 4.9.191
sun50iw10, 2026-07-29 (device arrived, no SD card, redux not installed):

- `strings /usr/trimui/bin/MainUI | grep ^Trimui` → exactly one line, `Trimui Brick Pro`
- panel live at 1024×768@60 (disp sysfs + fbset); `overlay` in `/proc/filesystems`
  (kernel 4.9 → the tg5040 tmpfs-staging OSD mount branch is the right one here too)
- `/sys/class/led_anim/` has `effect_{f1,f2,m,lr,rear}` + `max_scale`,
  `max_scale_f1f2`, `max_scale_lr`, `max_scale_rear` exactly as tabled — plus
  standalone `effect_l` / `effect_r` (and matching rgb_hex/cycles/duration nodes),
  so the two `lr` sides are individually addressable (23 RGB LEDs in
  `/sys/class/leds/`); possible future refinement, not needed for bring-up
- `trimui_inputd` and `keymon` running; `trimui_osdd` runs from
  `/usr/trimui/osd/trimui_osdd` (the overlay mount point); turbo interface is the
  same `/tmp/trimui_inputd/turbo_*` flag files (per its own `help` file)
- PD14/PD18 analog-pad GPIO poke confirmed commented out in the LIVE
  `runtrimui.sh` (not just the recovery image)
- busybox v1.27.2 — same as the Brick (tar guard / applet findings carry over)
- **`TRIMUI Player1` key bitmap decodes to exactly the expected SDL order**:
  BTN range 304-318 (A,B,X,Y,TL,TR,SELECT,START,MODE,THUMBL,THUMBR → SDL 0-10),
  then low keys KEY_F1(59), KEY_F2(60), VOLDOWN(114), VOLUP(115),
  KEY_HOMEPAGE(172) → SDL 11-15. That is 8=MENU, 9/10=L3/R3, 11/12=FN1/FN2,
  13/14=volume, 15=HOME — kernel-level evidence for the Input Tester item below
  (still confirm in SDL once redux is installed). ABS=3003f → both sticks,
  analog triggers, dpad hat all present on the one device. HOME is
  KEY_HOMEPAGE(172) *emitted by the gamepad device*, so SDL sees it as joystick
  button 15 and keymon's tg5050 keyboard-device Home path indeed does not apply.
- `/dev/ttyAS*` — NONE exist (see resolved calibration item below)

### 1. On-device verification (Brick Pro)

- [ ] **Boots and identifies correctly** — UI is 1024×768 at 3× scale with 7 main rows.
      Confirm `DEVICE=brickpro` (not `smartpro`); a mis-detect shows up as a 1280×720 layout.
- [ ] **SDL joystick indices** — *the main unverified assumption.* Open
      Settings → Input Tester and press everything. Expected: 9/10 = stick clicks (L3/R3),
      11/12 = FN1/FN2 (shown as L4/R4), 13/14 = volume, 15 = HOME, 8 = MENU.
      Wrong indices look like dead or swapped buttons, **not** a crash.
- [ ] **Analog sticks** — both nubs move the on-screen indicators. Note: `L3+R3`
      calibration is known-inert on this model until the I2C implementation lands
      (protocol + design in `DEV_TODO.md`, "Trimui Brick Pro: joystick calibration").
- [ ] **DC pre-launch options smoke (60 s)** — open "Emulator Options" once on a DC
      game: proves seeding into `config/tg5040-brickpro` from `default-brickpro.cfg`.
      Everything else on that feature is transitively covered — Brick Pro runs the same
      tg5040 binaries at the same 1024×768/3× geometry verified on the Brick, and has no
      fan daemon (the tg5050 picker-hang class does not apply).
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

Three items were scoped out of the port and are tracked in `DEV_TODO.md`: the PortMaster
device entry, display calibration / white point, and the wrongly-sized 1024×768 music
widget tile. None of them block bring-up.

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

**Status:** fixed and merged to `main` 2026-07-27 (`99985dec`, PR #56 — task 11 fix round).
tg5050 (Smart Pro S) fully verified (native `taskset`, PS.pak probe, DC.pak + N64.pak
pinning — evidence in `.superpowers/sdd/2026-07-26-flycast-dc-pak/n64-tg5050-report.md`).
Only the Brick N64.pak re-verify remains.

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
      (`skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/launch.sh:100,108,127`) were written and
      shipped blind, against a `taskset` that always silently failed; they were never
      exercised for real until this fix, the same way DC.pak's pinning was
      evidence-gated by direct measurement (task-11 report) before shipping.
      **Known gap, measured on Smart Pro S 2026-07-27 (reproduced twice, incl. a real
      user session):** the "pin the busiest `mupen64plus`-named thread" heuristic only
      pins ONE of the (at least) two non-main threads named `mupen64plus`; the other is
      left on the unrestricted 0-7 mask. Measured impact is small: its load is bursty
      init/loading work (~3.6% during boot, ~0% in live gameplay), and since NxRedux only
      brings cpu0-1/4(/5) online (8-core silicon run as effective 4-core by boot policy),
      "unrestricted" still lands it on the contended cores. The fix (pin unmatched threads
      to LITTLE by default) is written up in `DEV_TODO.md` and should land with this
      re-verify, so it ships measured rather than blind.
