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

## Soulcalibur "Unable to load game data / Check the VMU": resolved (2026-09-29, Brick)

- **Cause: the test harness, not the core.** flycast writes VMU blocks with `fseek` + `fwrite` and no `fflush`
  (`core/hw/maple/maple_devs.cpp` ~676). The data stays in the stdio buffer until the file is closed at unload. Ending a
  game with `kill -9` (what every spike run did) truncated the card. That happened on the shared `Bios/DC/dc/vmu_save_A1.bin`
  (hence Crazy Taxi 2's odd "106 free blocks") and on a per-game card.
- **Proof:** with `reicast_per_content_vmus = VMU A1`, a fresh boot shows Soulcalibur's normal first-run screen ("requires 12
  blocks … press Start to create"). After quitting through minarch's menu (a clean unload), the relaunch goes straight into the
  intro with no VMU error. After `kill -9` instead, the relaunch shows "Unable to load".
- **Real-user risk:** a power loss or crash mid-game loses unflushed card writes. Standalone has the same code, so this is not a
  regression. Cheap hardening for the pak switch: a flycast patch hunk that `fflush`es after each VMU block write (writes
  are rare).
- **Pak default:** `reicast_per_content_vmus = VMU A1` (one card per game in `Saves/DC/`, as DEV_TODO asked). flycast names
  it by disc ID (`T1401N.A1.bin` for Soulcalibur). nx-mobile patched it to `<content>.A1.bin` for cross-device saves; choose one
  at the pak switch.

## Sub-project 2 — GPU path polish (2026-09-29, Brick)

Commits `eaeadf24`, `698e7476`, `b6336e70`.
- **GPU debug HUD:** the same HUD text drawn into a transparent frame-sized buffer every 15 presents and shown over the game,
  with an extra `EMU nn%` line (audio-rate speed). The software-core HUD is unchanged (checked on GBA).
- **Ambient LEDs for GPU frames:** blit to 256², `glGenerateMipmap`, read back the 32² level (a true box average, 4 KB),
  every 8th present. On the Brick the LED colour follows scenes (e.g. `BC6A60` → `6767FE` → `CA7906`). Cost on
  Crazy Taxi 2 with ambient on (sampled every 4th present): 78–83 % vs 81–85 % off. Now sampled every 8th, which halves the cost
  (not re-measured).
- `[HWR]` stats only print with `NX_HWR_STATS=1` (the spike launchers set it).
- Emulated-sync drop and jitter stats use the slot.
- Fuller GL state restore.
- The desktop-guarded include.
- The comment on the shared FlipSchedule.

## Sub-project 1 — flycast **v2.7** libretro core in the cores Makefile (2026-09-29)

Commits `cef39ff9` (build: `workspace/all/cores/flycast/build-libretro.sh` + a minimal libretro toolchain with no curl/OpenSSL;
`CORES += flycast` in the tg5040/tg5050 cores Makefiles, v2.7 `5aa091fd`, only the libchdr/tinygettext/asio submodules) and
`6d055bfd` (patches in `workspace/all/cores/patches/flycast/`):
- `0001` the tg5040 GCC 8.3 ICE: **still needed on v2.7**;
- `0002` modern awbios;
- `0003` per-game VMU named after the ROM (`<rom>.A1.bin`, same as nx-mobile);
- `0004` `fflush` after each VMU write.

tg5040 also needs `-lstdc++fs` (GCC 8 keeps `std::filesystem` separate; v2.7's tinygettext uses it). Both platforms build from a clean
clone: ~35 MB, NEEDED only libc/libm/libstdc++/libgcc/pthread/dl/rt. Not packaged yet (sub-project 3).

**Device verification** (committed minarch GPU path, Emulated sync, per-game VMU):

| Check | Brick | Smart Pro S |
|---|---|---|
| Soulcalibur card | `Soulcalibur (USA).A1.bin`; relaunch after a clean quit loads | — |
| Card flush (save, then `kill -9`, relaunch) | loads (was "Unable to load" before `0004`) | — |
| Metal Slug 6 (modern awbios) | boots; 30 fps scenes 30.0 @ 44.1k | 30.0 / 60.0 @ 44.1k |
| Crazy Taxi 2 attract | 81–85 % (v2.6: 82–87 %, same within noise) | 100 %, heaviest 92–97 % |
| Widescreen aspect | — | fills 1280 px (`[AV] aspect=1.7778`, FBO 854) |

**Smart Pro S CPU layout in game:** online cpu0–1 (little, 1.416 GHz) + cpu4–5 (big, 2.16 GHz); cpu2–3 and 6–7 are offline
(the menu runs on cpu0–1 only). The spike launcher's `taskset 0x30` crowds the emu thread, minarch, audio and 8 Mali workers onto
cpu4–5. Standalone keeps emu on cpu4, main/render on cpu5, and audio/Mali on cpu0–1. **For the real DC.pak use
`minarch_cpu_affinity = big`** (helpers and Mali go to the little cores), as PS.pak does.

## Netplay (GGPO) spike in the libretro core — WORKS (2026-09-29, Brick host + Smart Pro S client)

The spike patch (throwaway) is saved as `flycast-libretro-ggpo.spike.patch`. It is ~170 lines against flycast v2.6 and is built with `-DNX_LIBRETRO_GGPO=ON`:
- the GGPO sources are compiled into the libretro core, and `USE_GGPO` is defined;
- ggpo.cpp's ImGui stats and UI includes are compiled out (no ImGui in libretro);
- `libretro.cpp` reads the session from env vars (`NX_GGPO`, `NX_GGPO_HOST`, `NX_GGPO_SERVER`, `NX_GGPO_DELAY`,
  `NX_GGPO_TIMEOUT`) before load, and forces UPnP off;
- it plugs a pad into port B (`retro_set_controller_port_device(1, JOYPAD)`: GGPO drives port B as player 2);
- after load it starts `NetworkHandshake` and blocks until synchronized, then carries on.

**Results** (Marvel vs Capcom 2, delay 1, HLE BIOS both sides, fresh identical per-game cards):
- **It works:** both sides synchronize in seconds; input from both devices lands (after the port-B fix); both screens stay
  identical through a real VS match (the user played); no desync.
- **Speed:** menus ~100 %, **fights ~41 frames/s / ~30k audio/s ≈ 67–71 %** on both (the slower side sets the pace),
  giving occasional lag and stuttering audio. **Standalone flycast measured on the same pair and scene: 30.0–32.3k/s ≈
  68–73 %.** It feels the same to the user. The cost is GGPO itself (per-frame snapshots plus rollback) on this hardware, not the port.
- **Pitfalls found:**
  1. The host needs the client's IP too (`server=` on both sides), as the standalone wizard already does.
  2. Port B must hold a controller on both sides.
  3. Any pause > 3 s drops the session (`ggpo_set_disconnect_timeout(3000)`): minarch's in-game menu, a debug
     screen capture. Standalone's overlay has the same limit.
  4. Both sides need identical memory cards and BIOS mode: the host's card must be copied to an isolated client copy
     (standalone wizard: serve/fetch; minarch already has `NETPLAY_SAVES_DIR`).
  5. `kill -9` truncates unflushed card writes (see the VMU section).

**Production estimate: ~4–6 days.**
- Core options instead of env (the four netplay options get real names/definitions): ½ day.
- minarch glue:
  - plug port B while netplay is on;
  - block the in-game menu, or keep it under 3 s, and show a netplay-aware "Disconnect" instead;
  - netplay-safe fast-forward, rewind and state restrictions (like the existing lockstep netplay).

  1–2 days.
- Netplay wizard for DC on minarch: reuse the existing discovery + card sync (serve the host card, client on an isolated copy),
  and feed host/peer to the core: 1–2 days.
- Device matrix (both roles on both devices, Brick Pro, cross-version standalone↔libretro not required): 1 day.

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

## Sub-project 3: DC.pak on minarch — device checks 2026-09-29

A copy of the new pak ran as a card-root `Emus/DC.pak` (which `getEmuPath` prefers). Its launcher called
`/mnt/SDCARD/.spike/minarch-spike.elf` (branch build) with `NX_HWR_STATS=1`. The installed `.system` pak was untouched.
flycast v2.7 with patches 0001–0005.

- **Save carry-over (Brick, real standalone data):** Soulcalibur's first launch created `Saves/DC/Soulcalibur (USA).A1.bin`,
  md5 = standalone `vmu_save_A1.bin`. `dc_nvmem.bin` and `vmu_save_A2.bin` were copied to `Bios/DC/`. The game went
  title → arcade character select with no card prompt. Metal Slug 6: `Saves/DC/reicast/mslug6.zip.nvmem`/`.nvmem2`
  seeded, md5 = standalone. Standalone files unchanged afterwards (md5, both devices).
- **BIOS:** Brick loaded the real `Bios/DC/dc_boot.bin` without a `dc/` folder (no "Forcing HLE" line). On the Smart Pro S,
  with no `dc_boot.bin`, the log says "Did not load BIOS, using reios" and games boot. The core creates an empty `Bios/DC/data/`.
- **Crazy Taxi 2 (Smart Pro S):** the seeded card has no CT2 file, so the game shows its own "No Save File, 196 free
  blocks" card screen. That is correct.
- **Controls:** Select inserts a coin on Metal Slug 6 ("CREDIT(S) 1"). Start works.
- **Pre-launch options:** `options.sh` (no ROM) opens "Dreamcast (Flycast)" with System / Video / Performance / Emulation
  Hacks / Input / Controller Expansion Slots.
- **In-game menu title:** showed "mslug6"; after the arcade-name fallback in `ma_menu.c` it shows "Metal Slug 6".
- **Smart Pro S CPU layout** (Crazy Taxi 2 attract after Start, 36 × 5 s windows each, Emulated sync, shipping
  `default.cfg`, fan Auto, 66–68 °C):

  | Layout | Threads | Mean | Worst window | 10th pct |
  |---|---|---|---|---|
  | `affinity = big` (shipping) | emu 4-5; PrepareFrame, audio, mali-* 0-1 | 96.4 % | 90.1 % | 91.9 % |
  | taskset 0x30 (spike) | everything 4-5 | 96.4 % | 90.4 % | 92.0 % |
  | `affinity = none` | everything inherits the launcher's 4-5 | 96.5 % | 90.2 % | 91.7 % |
  | `big` + cpu6–7 online | emu 4-7; helpers 0-1 | 95.5 % | 88.5 % | 90.9 % |

  No difference within noise, so this stretch is bound by the emulation thread or the GPU, not by core count. Kept
  `affinity = big` (as PS.pak), cpu6–7 stay offline. Note: the "100 %" Smart Pro S figure earlier in this file came from a
  lighter window. CT2's attract drive sits at ~96 % mean on every layout.
- **Devices after the checks:** removed `Emus/DC.pak`, `.spike`, seeded `Saves/DC/*.A1.bin`, `Saves/DC/reicast/`,
  `Bios/DC/{dc_nvmem.bin,vmu_save_A2.bin,data}`, `.userdata/<plat>/DC-flycast/`; Smart Pro S cpu6–7 offline.

## Sub-project 4: Dreamcast netplay on minarch — device checks 2026-09-29

Brick host (real BIOS) + Smart Pro S client (no BIOS), Soulcalibur, Wi-Fi. Test copies of the pak (card-root
`Emus/DC.pak`), minarch and the wizard (`.spike/bin` first on PATH).

- **Session:** both sides synchronize in seconds. BIOS fingerprints differ (Smart Pro S: `dcbios=none`), so both
  use HLE (`NX GGPO: host|client … HLE BIOS`). Fights run ~41–52 fps, as in the spike.
- **Host brings the save:** the client played on `/tmp/netplay-saves/Soulcalibur (USA).A1.bin` = the Brick's card
  (md5 `921d53…`). It got the host's `dc_nvmem`/A2 plus a copy of the arcade BIOS in `/tmp/netplay-system`. The client's
  own card (`6de215…`) and the host's card were unchanged. The host keeps a backup in `DC-flycast/netplay-backup/`. All
  `/tmp` copies and the session file are gone after the game.
- **In-game menu:** "Leave netplay?" (A Leave / B Continue) with a countdown. Continue resumed both; Leave quit. With
  no answer, it left on its own after 20 s. The peer's game waits meanwhile.
- **Leaving:** first build — the peer only ended after the 25 s disconnect timeout, so a UDP goodbye (port 55442) was added.
  Now the peer shows "Netplay ended" at once (all four checks, both directions).
- **First run hang:** the Smart Pro S froze on Leave (adb dead too, forced reboot; log cut off). Suspected cause: the emu
  thread spun in GGPO's prediction-barrier loop holding `ggpoMutex` while the unload's `stopSession()` waited for it.
  Patch 0006 now makes the loop exit when the frontend unloads (`ggpo::nxStopRequested`). Not reproduced in 5+ later
  sessions (tracer on both devices).
- **Notices** ("Connecting…", "Netplay ended", "Netplay failed") use the confirm dialog without buttons.
- Test-setup gotcha: a leftover `MinUI.zip` on the Smart Pro S card reinstalled v1.13.0 at a forced reboot and removed the
  card-root test pak.
