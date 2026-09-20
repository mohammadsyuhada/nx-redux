# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## Launcher CPU policy: boot phase at full range, menu cap, idle cap, big core offline on tg5050 (built; Brick + Smart Pro S verified 2026-09-20)

Users migrating from NextUI reported a laggy menu that "went away" after
toggling Show menu animations off/on. The toggle is a no-op (same value, same
file; sole consumer is the pill glide in `ui_list.c`); what changed was the
launcher relaunch after the first-boot work (NextUI caches lack the `#fp=`
header -> full Roms rescan, cold SD page cache, `bt_init.sh`/`wifi_init.sh`/
daemons still starting). That window hurt because `CPU_SPEED_MENU` pinned
schedutil to the table's second step: 408-600 on tg5040, 408-672 on the tg5050
little cluster, versus upstream NextUI's "auto" (408-1800 / 408-1320).

**What shipped** (`workspace/all/nextui/cpu_policy.{c,h}`, host test
`scripts/tests/test-cpu-policy.sh`, wired in `nextui.c`; caps in each
`platform.c`; marker in both `MinUI.pak/launch.sh`):

- BOOT: full range (`PLAT_setCPUSpeedAuto`) from right after GFX_init on a
  fresh boot, so menu init and a first-boot ROM rescan run uncapped
  (`GFX_endStartupBoost` stops the 1 s boost thread undoing it). Ends when
  both 12 s have passed and launch.sh has touched `/tmp/nx_boot_done` (its
  bt/wifi/ssh jobs exited; `kill -0` waiter), 20 s fallback. Skipped when the
  marker exists (relaunch after a game/tool). tmpfs, so gone every boot.
- ACTIVE: `CPU_SPEED_MENU`. tg5040 (cpu0 = all cores): 408-1008. tg5050:
  `CPU_FREQ_BASE` is the **big-core** policy (cpu4), so this is 408-672 there
  (unchanged); the little cluster, where the UI runs, stays at launch.sh's
  408-1416.
- IDLE: `CPU_SPEED_MENU_IDLE` after 3 s without input: 408-600 (tg5040), big
  core parked 408/408 (tg5050). Any input restores the menu cap in the same
  poll. minarch's in-game menu keeps using `CPU_SPEED_MENU` only.
- **tg5050 big core offline while the launcher runs** (`PLAT_setBigCoreOnline`,
  weak no-op elsewhere): SET_AUTO onlines cpu4 for the boot phase, SET_MENU and
  SET_IDLE take it offline after driving its policy (the launcher is
  little-bound, and an offline core is power-gated where a parked one is not).
  `MinUI.pak/launch.sh` onlines cpu4 in the boot-default state (schedutil
  408/408) right before `eval $CMD`, because paks assume it is up: N64 pins its
  emulator to cpu4, DC/NDS/PS set its clocks, minarch drives policy4. The
  post-pak restore block leaves it online; the relaunch offlines it again at
  its first frame. minarch's menu (`CPU_SPEED_MENU`) does NOT offline anything.
- tg5050 Tools paks that used to cap cpu4 at 408 (Settings, Emulator Settings,
  Game Tracker, Files, Device Sync, Artwork Manager, Xtras, Music Player) plus
  RetroAchievements now take cpu4 offline themselves right after launch.sh
  hands it over online — same performance (a 408 MHz big core added nothing),
  cluster gated. Media Player keeps the big cluster (video decode). Verified:
  Settings round trip shows `online=0-1` inside Settings and after.
- Xtras catalog audit: PSP (upstream ben16w/minui-psp launch.sh) saves the
  clocks, sets its own on both clusters (Brick ondemand 1608-1800; tg5050
  little 1416 fixed + big performance 1992-2160) and restores on exit — needs
  cpu4 online, which the hand-over provides. Gen1recomp onlines every core,
  full range on each policy, `taskset -c 4-7` on tg5050. PortMaster (GUI and
  ports) onlines all tg5050 cores at full range / Brick performance 2 GHz —
  deliberate, left alone. Cheat Database pinned cpu0 to `performance` for a
  UI: now the Tools profile (tg5050 cpu4 offline; Brick 1008 cap). Brick
  Xtras.pak wrote a cpu4 line that does not exist there: now caps cpu0 1008.
- Brick hand-over: `MinUI.pak/launch.sh` now resets cpu0 to schedutil 408-1008
  before `eval $CMD` (mirrors its restore block), so a pak launched after the
  launcher went idle does not inherit the 600 MHz cap. tg5050 hands over cpu4
  online at schedutil 408/408 (its boot default); a pak that forgets its
  clocks lands on a 408 MHz big core, visible and fixable in that pak — paks
  own their clocks (DEVICES.md).
- The boot phase starts at the top of main (before InitSettings) when the
  marker is absent: on tg5050 GFX_init outlasts the 1 s boost, which showed as
  a 0.8 s dip to 408 before the first frame when it started after GFX_init.
- tg5040 Tools scripts that hard-capped 600000 (Settings, Emulator Settings,
  Game Tracker) now write 1008000.

**Brick measurements** (hot-deployed launcher, d-pad injection, frame time
logged after `GFX_flip` while the pill glides):

| | 600 MHz cap (old) | 1008 MHz cap (new) | ondemand @1008 |
|---|---|---|---|
| glide frame time | 29-33 ms (mode 30-31) | 21-24 ms (mode 22) | 21-24 ms after ramp |
| first frames after idle | same | same | 37-41 ms (ramp latency) |
| idle `scaling_cur_freq` | 600000 (= cap) | 1008000 (= cap) | 408000 / 600000 |

- Sweep (glide median/p90, boot settled): 408: 41/62 · 600: 31/33 · 816: 25/28
  · 1008: 23/24 · 1200: 21/24 · 1416: 20/23 · 1608: 20/22 · 1800: 19/21 ·
  2000: 18/22. Knee 816-1008; the ~18 ms floor is the flip/GPU.
- Boot window (navigating from ~2.5 s after first frame): 27 ms median / 40
  worst capped at 1008 vs 20 / 31 uncapped. The marker lands ~6 s after
  launcher start but stock init (`procd`) + SD I/O keep the system 30-40% busy
  for ~12-14 s — hence the 12 s minimum.
- **schedutil on the Brick's 4.9 kernel parks at `scaling_max_freq` when
  idle** (cores 4-5% busy, `cpuinfo_cur_freq` agrees; no `stats/time_in_state`,
  no schedutil tunables exposed). The cap is the idle OPP, which is why the
  IDLE state exists. Ondemand idles lower but stutters on the first press.
- Verified traces: fresh boot = 2000000 from launcher start (no dip) until
  policy-start + 12.1 s, then 600000 (no input); press -> 1008000 within
  50 ms; idle -> 600000 at +3 s. Relaunch via Settings round trip = GFX boost
  ~0.5 s, 1008000 at first frame, 600000 after 3 s idle; marker persists.
- Battery could not be measured: `axp2202-battery` reports Charging over the
  adb USB tether and exposes no `current_now`.

**Smart Pro S measurements** (same probe, `/dev/input/event4`, 3 cores online:
cpu0-1 little + cpu4 big): glide frame ~18 ms median for ANY big-core cap
(sweep 408..2160: 17-20 median) with the little cluster at its default 1416;
capping the little cluster instead: 408: 36 · 672: 22 · 792: 20 · 936: 19 ·
>=1224: 18-19 — the UI is little-bound, the big core is irrelevant to it. The
5.15 schedutil does scale down at idle (little sits at 936 under the ~15%
daemon load). Verified traces: fresh boot = cpu4 2160000 from launcher start
(no dip) until +12.3 s, then parked 408/408 (no input); marker at +10 s
(bt_init ~7 s); press -> 672000 within 50 ms; idle -> 408000 at +3 s; Settings
round trip = GFX boost ~0.75 s, 672000 at first frame, 408000 after 3 s idle;
cores 0-1,4 and GPU `simple_ondemand` unchanged throughout.

- [x] Brick: boot phase, marker, menu cap, idle cap, wake, relaunch (above).
- [x] Brick: Settings round trip caps at 1008000 at the first frame.
- [x] Smart Pro S: boot phase, marker, menu cap, idle park, wake, Settings
      round trip, big/little sweeps (above). Not yet: GPU governor back at
      `simple_ondemand` after a DC game (only Settings was round-tripped).
- [x] Smart Pro S, big core offline: fresh boot shows `online=0-1,4` through
      the boot phase and `0-1` from +12 s on; glide with cpu4 offline 16 ms
      median (p90 19-28, two runs of 12 presses) vs 18 with it parked; in
      Settings `online=0-1,4`, cpu4 schedutil (its GFX boost briefly at 2160);
      relaunch = `0-1,4` for the ~0.75 s boost then `0-1` at 672; post-relaunch
      glide 15 ms median. GPU `simple_ondemand` throughout.
- [ ] Smart Pro S: a real game round trip with cpu4 offline beforehand — a
      minarch pak (GB) and N64 (its `taskset -p 0x10` must succeed, i.e. cpu4
      is online by the time launch.sh runs it). Only Settings was exercised.
- [ ] Brick: core offlining in the menu was NOT done. All four A53s share one
      cluster and one frequency domain; an idle core sits in WFI / cpuidle and
      the cluster stays powered while any core is up, so the saving is far
      smaller than the tg5050 big-cluster gate and the launcher's loader
      threads and marquee do use the extra cores. Measure (probe + offline
      cpu2/3) before deciding; no pak on tg5040 touches core hotplug, so the
      launch.sh side would be new too.
- [x] Brick re-flashed with the final build + boot script (2026-09-20 09:30):
      2000000 from launcher start (no dip), marker +6 s, 600000 at +12.5 s
      (idle), press -> 1008000 in 50 ms, hand-over -> Settings at 1008000,
      relaunch 1008000 -> 600000 after 3 s.
- [x] Brick core offlining in the menu: measured, **decided against**. Glide
      frame (menu cap, probe): 4 cores 22-23 ms median / 26-27 p90; 2 cores
      24-27 / 36-37; 1 core 49-76 / 60-89. Idle system load 2% on 4 cores vs
      11% on 1. The A53s share one cluster and one frequency domain, so an
      idle sibling in WFI costs little and the loader/marquee threads do use
      it — the p90 hit is not worth it. All four stay online.
- [x] Smart Pro S, onlining the other two little cores (cpu2-3) for the
      launcher: measured, **decided against**. Glide with cpu4 offline: 2 little
      cores 16 ms median / p90 20-27; 4 little cores 17 / 20-21 (three runs
      each) — noise. Idle load 7% -> 3%, same work spread thinner, same
      cluster/frequency domain, so nothing gated. The 3-core boot policy
      (cpu0-1 + cpu4) stays; the launcher runs on cpu0-1 alone.
- [ ] Follow-up to evaluate, not built — **per-core CPU affinity for minarch
      games on tg5050**: minarch sets frequency (PERFORMANCE at start,
      then `minarch_cpu_speed` per core: powersave/normal/performance/auto, and
      MENU in its menu) but never pins threads; only the standalone/heavy paks
      do it in their launch.sh (N64: emu -> cpu4, others -> cpu0-1, extra cores
      onlined; DC: main -> cpu5, aux -> cpu4, rest -> cpu0-1; PS: minarch via
      `taskset -c 4,5`, main -> 4, aux -> 5, rest -> 0,1; NDS: both clusters
      pinned high). Generic minarch paks (GB, SNES, PCE, 32X, GPGX ...) run
      unpinned on cpu0-1 + cpu4 and rely on the scheduler to place the emu
      thread on the big core. Candidate: a minarch "CPU affinity" option
      (auto / big) applied with sched_setaffinity to the emu thread, audio on
      little; needs per-core measurement before defaulting anything. Brick:
      single cluster, nothing to pin to.
- [ ] Battery: untethered comparison, idle and navigating, old vs new — the
      idle-at-cap finding is the reason to actually measure.
- [ ] Cold first boot after installing over a NextUI card: browse during the
      first minute; the ROM rescan now runs uncapped, check the menu keeps up.
- [ ] minarch in-game menu still opens/scrolls normally (same constant).
- [ ] The `PILL_ANIM_MS` comment in `ui_list.c` still quotes the 600 MHz
      frame numbers; refresh once the tg5050 numbers exist too.

## Desktop: AppImage no longer bundles the host GL/driver stack (issue #86; built 2026-09-04)

v1.9.0's AppImage died before opening a window on every Mesa >= 25 host
(Ubuntu 24.04.4 HWE, Mint 22, 25.10): the ldd sweep bundled Ubuntu 22.04's
libstdc++/libGL/libGLX/libGLdispatch, AppRun puts usr/lib first on
LD_LIBRARY_PATH, and the host driver's libLLVM.so.20 then failed with
`GLIBCXX_3.4.32 not found` -> no GLX visual -> NULL window/renderer used
anyway -> segfault. Fix: `scripts/desktop/appimage-bundle-libs.sh` (vendored
`appimage-excludelist` + driver-adjacent families; freetype/harfbuzz kept),
and `PLAT_initVideo` now logs the SDL error and falls back to a plain window
+ software renderer instead of crashing.

Verified in amd64 containers under Xvfb/llvmpipe only (Ubuntu 24.04.4 +
25.10 with Mesa 25.2.8, and the 22.04 floor): trimmed bundle opens on the
`opengl` driver with libGL/libLLVM/libstdc++ all host-side, the old bundle +
new binary comes up on the software fallback. Not yet seen on a real GPU or
in a CI-built artifact (no local cores tree; the release workflow rebuilds
from scratch).

- [ ] Next release's AppImage on a real Ubuntu 24.04 / Mint 22 host with a
      GPU driver: `nextui.txt` shows `Current render driver: opengl`, no
      `[ERROR]` lines, no env vars needed (ask the #86 reporter to confirm).
- [ ] Same artifact on the oldest supported host (glibc 2.35 / Ubuntu 22.04):
      still opens (only host-provided libs were removed; a host missing one
      now fails with a clear "cannot open shared object file" line).
- [ ] `scripts/desktop/test-appimage-e2e.sh` in the 22.04 compose image still
      passes end to end (menu -> ROM launch) against the rebuilt AppImage.
- [ ] Optional: force the fallback (`SDL_VIDEO_X11_VISUALID= ` no longer
      needed; e.g. run on a GL-less VM) and confirm the menu + a non-shader
      game render on `software`, with `SDL_GL_CreateContext failed ...
      (shaders unavailable)` logged rather than a crash.

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
  menu / failed init) rarely has a moment worth resuming. FBNeo's error screen verified
  on both devices 2026-09-10 (needed the lazy input poll in `ma_input.c`, since that
  screen never calls `input_poll_callback`); the PRBOOM quit check and the resampler
  PAL soak were closed without hardware verification (user decision, 2026-09-10).
