# minarch GPU path — phase 0 spike results (Brick, 2026-09-28/29)

**Verdict: GO.** With matched settings, flycast libretro rendering through minarch's new GPU path runs
Dreamcast as fast as or faster than standalone flycast on every game tested, even though it ran at a
lower CPU clock (1.8 GHz schedutil vs standalone's 2.0 GHz performance). All correctness checks passed.

Plan: `docs/superpowers/plans/2026-09-28-minarch-gpu-spike.md` (local, gitignored).
Ledger: `.superpowers/sdd/2026-09-28-minarch-gpu-spike/progress.md`.

## Setup

- Device: Brick (tg5040, `5c000c8997414781d1d`), firmware card as of 2026-09-28.
- Standalone: `DC.pak` flycast v2.6 `392a429e` (shipped binary), config `default-brick.cfg` values, real BIOS.
- Libretro: flycast v2.6 `392a429e` built with `-DLIBRETRO=ON -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_OPENMP=OFF`
  (`workspace/all/other/flycast/build-libretro-spike.sh`, ICE fix + awbios hunks only). Run by a separate
  `minarch-spike.elf` (this branch) from `/mnt/SDCARD/.spike/`; real BIOS; options in `minarch.cfg` here,
  installed as `.userdata/tg5040/DC-flycast/minarch-brick.cfg`.

## Metric

Emulation speed = audio frames the core produces per second ÷ 44 100 (100 % = full speed).
- Libretro: counted in minarch's audio callbacks (`HWR_countAudio`, logged every 5 s as `[HWR] … audio N/s`).
- Standalone: counted by a temporary probe in flycast's `WriteSample` (`/tmp/fc-speed.log`), in a *copy* of
  `DC.pak` under `.spike/` (the installed pak was never modified). Probe removed and source reverted afterwards.

Frame counters are not comparable. Standalone's `F:` counts only new frames. Libretro re-presents the last
frame on every `retro_run` (Metal Slug 6: libretro 60 vs standalone F:30 on the same demo, and the game runs
at 30 natively). So the verdict uses speed only.

## Results (steady-state 5 s windows)

| Game / scene | Standalone speed | Libretro speed, matched cfg | Libretro, flycast defaults (first pass) |
|---|---|---|---|
| Crazy Taxi 2, attract driving | 34.4–35.2k/s → **78–80 %** | 36.0–37.5k/s → **82–85 %** | 25.0–26.4k/s → 57–60 % |
| Soulcalibur, attract fight | 31.0–33.6k/s → **70–76 %** (temple stage) | 32.3–35.7k/s → **73–81 %** (lake stage) | 31.0–32.5k/s → 70–74 % (lake stage) |
| Soulcalibur, intro | F: 48–57 (speed not probed) | — | 41.2–44.3k/s → 93–100 % |
| Metal Slug 6 (Atomiswave), attract | F: 30 (game-native 30 fps) | — | 44.2–44.4k/s → **100 %** |
| ChuChu Rocket, VMU-select screen | F: 59.5–60.7 | — | 60.2 runs/s (light screen; weak data point) |

"Flycast defaults" = the first pass, when the options file was pushed as `minarch.cfg` and ignored (minarch reads
`minarch-$DEVICE.cfg`). That pass ran with per-triangle alpha sorting, no auto frame-skip and 4× anisotropic filtering. The
Crazy Taxi 2 gap there came from auto frame-skip being off: runs == frames and 735 audio frames per run.
Disabling anisotropic filtering alone changed nothing.

## Correctness (all on libretro Soulcalibur unless noted)

- Image upright, correct colours, no black frames. The core requested a GLES2 context (`type 2`) and runs fine on minarch's GLES 3.2 context.
- In-game menu open → close: the menu background is a GL capture of the game, and the game resumes drawing at 60.
- Save state (slot 1): 35.9 MB `.state` with a correct preview thumbnail. Load back works, with the "State Loaded" notification drawn over the game.
- CRT shader (`crt-perfect`, 1 pass) + Grid screen effect: both render correctly over the GPU frame. Cost: static screen 60 → 49 fps.
- Software-core regression (gpsp, Advance Wars) via the same spike binary: renders with bezel, no `[HWR]` lines, menu round-trip OK.
- Metal Slug 6 boots on the patched awbios. The `bios0.ic23` warnings are the normal pre-fallback messages.

## Findings to carry into phases 1–5

1. **VMU:** Soulcalibur shows "No VMU found" / "Check the VMU" (a `vmu_save_A1.bin` appears in `Bios/DC/dc/`, but the game
   still rejects it). nx-mobile hit the same thing (controller-port device + VMU plumbing, per-game VMU naming).
2. **CPU speed:** `minarch_cpu_speed = Performance` in the options file did not take effect (schedutil, max 1.8 GHz).
   Libretro therefore ran ~10 % slower-clocked than standalone and still matched or beat it. Find out why (CPU-profile override?) before re-measuring.
3. **BIOS location:** libretro flycast wants `Bios/DC/dc/` (`dc_boot.bin`, `awbios.zip`, `naomi.zip`). The pak must seed or point there.
4. **Frame-skip settings matter a lot:** `reicast_auto_skip_frame` and `reicast_alpha_sorting` decide the heavy-game result. Ship explicit defaults.
5. **Save states are ~36 MB:** rewind must stay off for DC; auto-save on sleep/quit will take a moment.
6. **Spike-only code to replace in phase 1:** the `[HWR]` stats line and `HWR_countAudio` instrumentation; fade-in/debug HUD/ambient colour are skipped for GPU frames.
7. An earlier, unrecorded libretro build attempt exists in the standalone tree (`build-tg5040-libretro*`, 2026-09-13), with no notes.

## Smart Pro S (tg5050, Mali-G57) — 2026-09-29

Same method. Libretro core built with `build-libretro-spike.sh tg5050`. Launcher `launch-tg5050.sh` copies DC.pak's
CPU/GPU setup (big cores cpu4-5 at 1992–2160 MHz, GPU devfreq on performance) and pins minarch to cpu4-5. Options are in
`minarch-smartpros.cfg` (`DEVICE=smartpros`), set to match standalone's tg5050 `default.cfg`: HLE BIOS (the card has
no `dc_boot.bin`) and widescreen on. Standalone speed comes from the same audio probe, in a copy of `DC.pak` under
`.spike/`.

| Game / scene | Standalone speed | Libretro speed |
|---|---|---|
| Crazy Taxi 2, attract driving | 41.5–44.2k/s → **94–100 %** | 41.5–46.4k/s → **94–105 %** |
| Soulcalibur, intro/attract | 43.8–44.2k/s → **100 %** | 45.1–46.4k/s → **102–105 %** |
| Metal Slug 6, attract | 44.1k/s → **100 %** | 46.3k/s → **105 %** (plus a ~15 s burst at 92k/s, see below) |

- **Speed is at parity.** Both run full speed. The ">100 %" is minarch's screen sync: it presents at the
  panel rate (~63 Hz here) and resamples audio, the same as for every core. Here `minarch_cpu_speed = Performance` did apply (big
  policy locked at 2160 MHz). That supports the idea that the Brick's 1.8 GHz came from a thermal cap, not from minarch ignoring the
  option. Still unconfirmed on the Brick (it was not attached): read its cooling_device state during a run.
- **Mali renders correctly.** The core got the same GLES2-labelled context, and images captured through the tg5050 fb mirror
  (`/tmp/screenshot.pid`) are upright with correct colours.
- **Aspect ratio is wrong. This makes the deferred review item user-visible.** minarch keeps the load-time aspect and ignores
  flycast's later `SET_GEOMETRY` / `SET_SYSTEM_AV_INFO`. Crazy Taxi 2 shows at 4:3 (960 px wide) whether widescreen is on or off,
  so the 16:9 frame is squeezed. Soulcalibur with widescreen on shows square (720 px wide). With widescreen on, flycast also sends
  854×480 frames into an 853-wide FBO (one column clamped). The Brick's 4:3 panel hid all of this.
- **VMU works.** Crazy Taxi 2's memory-card screen finds a card in port A (106 free blocks). Soulcalibur's "Unable to
  load SOULCALIBUR game data. Check the VMU" still needs its own look (compare nx-mobile's per-game VMU handling).
- **Anomaly:** Metal Slug 6 on libretro produced audio at ~2× rate (92k/s) for about 15 s mid-attract. Listen for it.
- The in-game menu drawn by minarch did not show up in the fb-mirror capture, and the save-state key sequence created no
  state on this device. Neither is investigated yet. Menu and save/load were verified on the Brick.

## Follow-ups — 2026-09-29

**CPU speed (Brick) is resolved.** `minarch_cpu_speed = Performance` works: a fresh libretro run holds min = max = 2.0 GHz
(schedutil, `thermal-cpufreq-0` state 0, 52–61 °C). Standalone holds 2.0 GHz with the performance governor at ~55 °C. The
fixed 1.8/1.8 range seen earlier appeared after about an hour of back-to-back runs, which fits a thermal cap. Re-measured at 2.0 GHz,
Crazy Taxi 2 attract on libretro runs at 36.8–38.4k/s → **83–87 %** (vs 82–85 % at 1.8 GHz; standalone 78–80 %).

**Double-rate audio on 30 fps games (Metal Slug 6).** nx-mobile fixed this in `1ab0b36b` (+ `5ed0387` to lock it on).
flycast's `retro_run` runs until the game renders, so a 30 fps game gets two vblanks per call. The fix has two parts:
1. `reicast_detect_vsync_swap_interval = enabled`, so flycast reports the halved rate through `SET_SYSTEM_AV_INFO`;
2. the frontend paces to the reported rate. minarch ignores `SET_SYSTEM_AV_INFO`, which is the same gap as the aspect bug.

Caveat for us: flycast v2.6 **and** v2.7 force detection off when `AutoSkipFrame != 0 && ThreadedRendering`
(`shell/libretro/libretro.cpp` ~894). Our matched config uses `reicast_auto_skip_frame = some` + threaded rendering, and
auto-skip is what carries Crazy Taxi 2 on the Brick. nx-mobile leaves auto-skip at its default (off). Phase 1 must
implement AV-info handling, then measure auto-skip on (fast, 30 fps games double speed) against auto-skip off +
detection on (correct pacing, heavy 3D slower) on the Brick. The alternative is patching the rule so both can be on.

## Phase 1a — AV-info / geometry handling (2026-09-29, Brick)

Commits `27f51604` (AV classification + sync helpers), `5fe35b46` (in-place FBO growth), `eecb03f0` (env 32/37 for GPU
cores + pending apply after `retro_run` + AUTO sync paces by core fps below 75 % of the panel rate). Host tests pass. Builds
pass on tg5040, tg5050 and desktop. GBA (gpsp) regression is clean (no `[AV]`/`[HWR]` lines).

- **30 fps pacing works.** Metal Slug 6 with `reicast_auto_skip_frame = disabled` + `reicast_detect_vsync_swap_interval
  = enabled`: `[AV] fps=29.970` / `59.940` switches follow the game. It runs at 30.0 runs/s in 30 fps scenes and ~59 in 60 fps
  scenes, with audio ~44.1k/s throughout (no more 2×). flycast reports a bogus 4.995–6.66 fps once at boot and corrects it one
  report later.
- **The trade-off (Crazy Taxi 2 attract, 2.0 GHz, cooling state 0):** auto-skip `some` gives **83–87 %**, and auto-skip
  `disabled` + detection gives **61–73 %** (26.8–32.3k/s). Standalone gives 78–80 %. flycast forces detection off with auto-skip + threaded,
  so today it is either correct 30 fps games or heavy-3D speed.
- **Possible way out (not built): audio-master pacing for GPU cores.** minarch's fixed-rate audio path drops samples
  when late instead of blocking. A GPU-core pacing mode that waits after `retro_run` until the audio buffer has room
  would throttle a 2-vblank `retro_run` to real time by itself. That gives correct 30 fps games *with* auto-skip on, without
  flycast's detection. It needs its own design and a check that it does not add latency or jitter.
- **Smart Pro S (2026-09-29): the aspect bug is fixed.** With widescreen on, Crazy Taxi 2 logs `[AV] aspect=1.7778` and
  `[HWR] FBO grown 853x853 -> 854x854`, and the GL capture fills the full 1280 px width (was 960). With widescreen off it is 960 px
  (4:3). Soulcalibur's 2D memory-card screen now draws at correct 4:3 proportions (was square). One widescreen frame showed a
  garbled strip bottom-left; a second capture was clean. It is most likely flycast's widescreen hack exposing off-screen geometry;
  watch for it.
- **Smart Pro S 30 fps pacing:** Metal Slug 6 (auto-skip off + detection) runs at 30.0 runs/s, 44.1k/s audio in 30 fps scenes
  and ~63 (panel rate) in 60 fps scenes.
- Soulcalibur still stops at "Unable to load SOULCALIBUR game data. Check the VMU" on both devices. That is the remaining memory-card item.

## Emulated Core Sync (2026-09-29)

Commits `900fa539` (emulated-time counter), `65c02967` (slot scheduler; `GFX_flip_fixed_rate` rebuilt on it), `bb1169ef`
(the "Emulated" value of `minarch_sync_reference`; GPU cores only). Spec/plan (local):
`docs/superpowers/specs/2026-09-29-minarch-emulated-sync-design.md`, `docs/superpowers/plans/2026-09-29-minarch-emulated-sync.md`.

**Smart Pro S**, Emulated + `reicast_auto_skip_frame = some` + frame-rate detection OFF:

| Game | Result |
|---|---|
| Metal Slug 6 | 30.0 runs/s @ 44.1k in 30 fps scenes, 60.0 @ 44.1k in 60 fps scenes: exactly 100 % (screen sync gave ~105 % on the 63 Hz panel) |
| Quake III Arena (30 fps game) | 29.6–30.1 runs/s @ 44.1k |
| Marvel vs Capcom 2 | 60 @ 44.1k (slower only while loading) |
| Crazy Taxi 2 | 100 %, heaviest stretches 92–96 % (auto-skip catching up) |

- **Fast-forward on→off and menu open/close** recover within one 5 s window, with no stall and no audio burst.
- **Save/load could not be exercised on this device:** injected Save/Load keypresses create no state here (the menu opens; RA hardcore is off). To verify on the Brick.
- **One freeze during the first fast-forward test:** the device was hot with the fan silent. The fan was not on Auto then. After the user set Auto, the same sequence ran clean at 64–68 °C with the fan at 31/31. `fancontrol` stays alive across spike launches. This is most likely a thermal lockup with CPU/GPU pinned at max under fast-forward, not a pacing bug. Keep the fan on Auto for DC testing.

**Brick**, same configuration:

| Game | Result |
|---|---|
| Metal Slug 6 | 30.0 runs/s @ ~44.1k in 30 fps scenes, 59.9 @ 44.1k in 60 fps scenes |
| Crazy Taxi 2 attract | 36.0–38.2k/s → **82–87 %** (median ~84 %), meeting the ≥ 83 % target with auto-skip on (standalone 78–80 %; auto-skip off + detection 61–73 %). CPU fixed at 2.0 GHz, no throttling, 53–62 °C |

- **Save/load recovery (Crazy Taxi 2):** the window with menu + 36 MB save reads 13.8k/s, then straight back to 37.9k/s. Load: one partial window (31.5k/s), then 38.3k/s. No stall and no burst.
- **CPU layout observed on the Brick:** all 4 cores online in menu and game (a single cluster, one policy). Menu: schedutil 408–600 MHz. Game: minarch "Performance" locks 2.0 GHz. Threads spread across cores; no pinning (the Brick has no `taskset`).
- Marvel vs Capcom 2 and Quake III are now also on the Brick's card.

## Netplay (GGPO) in the libretro build — checked 2026-09-29

The rollback code is shared core code, but four places compile it out of the libretro build, and none of them is a build option:
1. `CMakeLists.txt` `if(NOT LIBRETRO)` excludes the GGPO library sources.
2. `core/build.h` `#if !defined(LIBRETRO) #define USE_GGPO`. Without it, `core/network/ggpo.cpp` compiles stubs
   (`active()` false, `startNetwork()` false).
3. `shell/libretro/option.cpp` declares `GGPOEnable`, `ActAsServer`, `NetworkServer`, `GGPODelay` and `NetworkEnable` with
   empty names, so they cannot be set as core options.
4. The session start/wait lives in the standalone UI (`core/ui/gui.cpp`), and the GGPO sync-start
   `dc_loadstate(-1)` in `emulator.cpp` is `#ifndef LIBRETRO`.

Enabling it is a source patch plus libretro glue (options, session start, frame-skip/rollback under `retro_run`). It is
not a flag. nx-mobile's DC spec lists it as a future spike, not a result. Estimate: a 2–4 day spike.

## Device state after the spike

Removed: `/mnt/SDCARD/.spike`, `Bios/DC/dc/`, `Saves/DC/reicast/`, `.userdata/tg5040/DC-flycast/`, the spike's
`Soulcalibur (USA).state`, and the `rend.ShowFPS` line in standalone `emu.cfg`. Kept, at the user's request:
`Roms/DreamCast (DC)/ChuChu Rocket.chd` and `Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!].chd`. Smart Pro S: same cleanup (`.spike`, `Bios/DC/dc/`, `Saves/DC/reicast/`, `.userdata/tg5050/DC-flycast/`); kept `Soulcalibur (USA).chd` + Crazy Taxi 2 in its DC folder.
