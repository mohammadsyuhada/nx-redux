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

## Device state after the spike

Removed: `/mnt/SDCARD/.spike`, `Bios/DC/dc/`, `Saves/DC/reicast/`, `.userdata/tg5040/DC-flycast/`, the spike's
`Soulcalibur (USA).state`, and the `rend.ShowFPS` line in standalone `emu.cfg`. Kept, at the user's request:
`Roms/DreamCast (DC)/ChuChu Rocket.chd` and `Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!].chd`.
