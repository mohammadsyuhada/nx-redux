# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## Desktop: Tools paks bundled into packages; Wifi_ensureConnected behavior change (built 2026-09-01)

Task 11 of the desktop-tools-and-networking SDD: all 7 Tools paks (Settings,
Emulator Settings, RetroAchievements, Artwork Manager, Device Sync, Xtras,
Game Tracker) now ship inside both the macOS `.app` and the Linux AppImage,
pak-local (`paks/Tools/<Name>.pak/<binary>.elf`, matching device layout) with
`gametimectl.elf` also at `$SYS/bin` (nextui.c/launcher.c invoke it bare via
PATH at ROM start/stop — it's a stateless CLI, not a daemon, despite the
name). `PLAT_getNetworkStatus` on desktop now delegates to the already-live
`PLAT_wifiConnected()` reachability probe instead of hardcoding offline —
this feeds both the shared menu-bar wifi icon (`pwr.is_online`, `api.c`
`PWR_updateNetworkStatus`) and Device Sync's `STATE_NO_WIFI` gate (`sync.c`).
Verified live (macOS process/dylib-load + a Linux Xvfb/xdotool E2E,
`scripts/desktop/test-appimage-tools-e2e.sh`): Tools menu lists all 7 paks,
Settings shows only the desktop-retained sections, a pak (Artwork Manager)
opens cleanly, and the menu-bar wifi icon tracks real reachability. Desktop
build/packaging itself needs no further hardware verification — the items
below are about a *separate*, already-shipped device-side behavior change
this task's investigation surfaced (`Wifi_ensureConnected`, `wifi.c`) that
had no existing DEV_CHECKLIST coverage, plus desktop tools whose *content*
(network calls, real accounts) can't be exercised on desktop hardware.

**`Wifi_ensureConnected` — never auto-enables/auto-connects (both devices):**
- [ ] Wifi OFF, open Music Player → Podcasts (or Radio) → attempting to load
      a feed/stream shows "WiFi is off. Enable it in Settings." and wifi
      stays OFF (check Settings — it must not have flipped itself on).
- [ ] Wifi OFF, open Media Player → IPTV → same message, same
      does-not-auto-enable check.
- [ ] Wifi ON but not yet associated (no AP in range / wrong password):
      same screens instead show "WiFi is not connected. Connect in
      Settings." (`wifi.c` `Wifi_ensureConnected` picks the message off
      `PLAT_wifiEnabled()` — confirm it doesn't say "WiFi is off" once the
      radio is actually on).
- [ ] Wifi ON + connected: podcast/radio streaming and IPTV both work as
      before (no regression from the message-only change).

**Desktop tools needing real hardware/network verification** (all built +
packaged; none of this is testable from a desktop dev machine):
- [ ] Device Sync: an actual sync target (a real device or PC on the LAN)
      — Device Sync.pak's transfer paths are unexercised beyond the
      STATE_NO_WIFI gate fix above.
- [ ] RetroAchievements: real login + a live achievement unlock through
      ratools.elf on desktop (network calls only smoke-tested for
      reachability, not for actual RA account auth/session flow).
- [ ] Xtras: catalog installs will fail on desktop today (packages are
      device-arch binaries) — expected, not a bug; note this if anyone
      files it. Revisit if/when Xtras gains a desktop-arch catalog.

---

## Desktop: external game-controller support (built 2026-09-01)

Desktop build gained `SDL_GameController` support (macOS `.app` / Linux AppImage),
behind a `HAS_GAMECONTROLLER` macro defined only in `workspace/desktop/platform/platform.h`
— device builds compile the path out entirely (verified: tg5040 + tg5050 build green).
Keyboard stays active simultaneously. Face buttons map by physical position (Nintendo
layout: right face = A/confirm, bottom = B/back). Left stick = analog passthrough to
cores (`RETRO_DEVICE_ANALOG`) **and** digital d-pad (deadzone) for menus / non-analog
cores; right stick = analog only. Triggers (L2/R2) are digital past a threshold. Hot-plug
handled via `SDL_CONTROLLERDEVICEADDED/REMOVED`. No controller was available at build
time, so all controller behavior below is UNVERIFIED on real hardware.

- [ ] Plug an Xbox-style pad in **before** launch → launcher navigates with d-pad + left
      stick; right face button confirms, bottom face backs out (Nintendo-position mapping).
- [ ] Hot-plug: launch with no pad, connect one → it starts working without relaunch;
      disconnect → app stays alive, keyboard still works.
- [ ] Disconnect while holding a direction/button or with a stick off-center →
      no stuck input afterwards (CONTROLLERDEVICEREMOVED zeroes analog axes +
      PAD_reset()).
- [ ] In-game: face/d-pad/shoulders drive the emulated pad; keyboard still works at the
      same time (both input sources live).
- [ ] Analog passthrough: a core that reads analog (e.g. an N64/PSX core if present on
      desktop, else any `RETRO_DEVICE_ANALOG` core) responds to the right stick; left stick
      also moves the character AND navigates menus as a d-pad.
- [ ] Try a second controller type if available (PS4/5, Switch Pro, 8BitDo) — SDL's
      built-in DB should map it with no per-device config; note any pad that isn't recognized
      (would need a bundled `gamecontrollerdb.txt`, deferred).

---

## Boot: failed MinUI.zip extraction must not brick the boot loop (built 2026-08-01)

Found live on Smart Pro S (fresh install, 2026-08-01): a truncated MinUI.zip
(card pulled before the 230 MB copy flushed) made `.tmp_update/<plat>.sh`
extract nothing, then `rm -f MinUI.zip` unconditionally — every later boot had
no zip, no `.system`, no splash, and fell through to `poweroff`. Looks like a
dead device. Fixed in both `workspace/{tg5040,tg5050}/install/boot.sh`: the
zip is consumed only when unzip succeeds; on failure a show2 error line is
displayed for 10 s and the zip is kept so the next boot retries. The pakz
loop got the same success-gated consume — a corrupt pakz is renamed
`<name>.failed` (kept for diagnosis, but not re-matched by the `*.pakz` glob,
so no per-boot retry nag) and boot continues normally.

- [ ] Happy path: fresh install extracts and launches normally (both devices).
- [ ] Corrupt-zip path: truncate a MinUI.zip on card (`head -c 10M`), boot →
      "Install failed" splash shows ~10 s, MinUI.zip still on card, device
      powers off; replacing the zip and rebooting installs cleanly.
- [ ] Corrupt-pakz path: truncate a pakz on card, boot → "Package install
      failed" splash ~5 s, file renamed `.failed`, system boots normally and
      the next boot does NOT re-attempt it.

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
