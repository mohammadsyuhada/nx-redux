# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## minarch thread affinity (built + measured 2026-09-20; Smart Pro S re-swept with the Task 4.5 fix 2026-09-21)

The `minarch_cpu_affinity = none|big|little` key is built and measured (spec
`docs/superpowers/specs/2026-09-20-minarch-thread-affinity-design.md`). `none`
(default) is today's behaviour including the legacy startup pin; `big` pins the
emulation main thread + core-created threads to the **FAST** set and minarch's
helpers (frame prep, audio callback, CPU monitor, rewind, achievements sync,
screenshot) + the GPU driver threads to the **SLOW** set; `little` runs the whole
process on SLOW. Core sets: tg5050 FAST = online cpu4-7 (a pak's `launch.sh` may
online cpu5+ when the core uses them), SLOW = cpu0-1; tg5040 FAST = cpu3, SLOW =
cpu0-2 (isolation). The mechanism lives in `ma_cpu_affinity.c` via
`PLAT_pinToCoreSet` / `PLAT_pinOtherThreadsToCoreSet` /
`PLAT_pinThreadsByCommToCoreSet` and `PWR_setHelperThreadCoreSet` /
`PWR_pinHelperThread`, applied before `Core_init` with a late `mali-` sweep 2 s
after the first frame. Placement is verified on both devices (Smart Pro S `big`:
main on cpu4, helpers 0-1; Brick `big`: main cpu3, helpers 0-2; PS `pcsxr-drc`
balanced across cpu4/cpu5 under `big2`, `mali-*` on cpu0-1). The sweep harness gained a `[thr]` 100 ms
thread sampler (per-thread residency % + inferred migrations), a `stalls/min`
column, and a `BENCH_PRELAUNCH` hook (used for the Brick daemon herd). Harness
caveat: field 39 is the last-run cpu, so idle threads misreport — read the busy
threads.

**Decision table** (from `scripts/bench/results/{tg5040,tg5050}/AFFINITY.md`):

| core | device | none | big | big2 / herd | decision |
|---|---|---|---|---|---|
| PS | Brick (tg5040) | pass @1608 (2000 fails fps-min + stall) | fails 2000 & 1608 (stall) | herd: fails 2000 & 1608 (fps-min) | nothing ships — PS marginal even at base; big/herd beat neither |
| SFC | Brick (tg5040) | pass @1008 & @1200 | pass @1200, **fail @1008** (drops 183 > 118) | herd: pass @1200, fail @1008 | nothing ships — big's cheapest pass (1200) > none's (1008) |
| FBN | Brick (tg5040) | pass @1008 & @1200 | pass @1008 & @1200 | herd: pass @1008 & @1200 | nothing ships — tie at cheapest cap (1008) |
| MGBA | Brick (tg5040) | pass @1008 & @1200 | pass @1200, fail @1008 (stall) | herd: pass @1200, fail @1008 | nothing ships — big never cheaper than none |
| PS | Smart Pro S (tg5050) | pass @1200 (2160/1680 trip the sporadic stall) | pass @2160 & @1200 (drops ~480-580) | **pass @2160 & @1200, drops 283-306 (fewest of any config)** | **SHIPS: `minarch_cpu_affinity = big` + cpu5 (replaces the taskset script)** — big2 ≥ script (283-306 vs 384 drops, worst 47.7 vs 65.2, 0 vs 1 stall) |
| SFC | Smart Pro S (tg5050) | pass @936 & @1032 | @1032/@936 trip the sporadic worst-frame (drops lower: 187/522 vs 138/903) | big2@1032 cleanest (drops 84) | nothing ships — topology-A (cap 1032) stands; no robust big win |
| FBN | Smart Pro S (tg5050) | pass @936 & @1032 | pass @936 & @1032 | big2@1032 cleanest (drops 58) | nothing ships — tie at 936; topology-A (cap 1032) stands |
| MGBA | Smart Pro S (tg5050) | pass @1032 only (936 fails fps-min 56.6 < 56.7) | **pass @1032 & @936** (936: fps-min 57.3, drops 876 vs none 1612) | big2@1032 cleanest (drops 130) | not adopted: topology-A 1032 passes; big's edge is over none-on-big, not over the shipped profile; revisit if GBA titles drop in the field |

Brick daemon herding (`BENCH_PRELAUNCH` taskset of keymon/audiomon/etc. onto
cpu0-1) does **not** ship: stalls/min stayed flat-to-worse (PS 1.0→1.0, SFC@1008
0.0→1.0, FBN 0.0→0.0, MGBA@1008 1.0→1.0), no ≥50 % reduction. The Smart Pro S was
re-swept complete on 2026-09-21 with the Task 4.5 fix build; the earlier wedge
(during the Task-4a PS `big2@1680` exit) did not recur across ~50 min of load.

**Defect fixed alongside (Task 4.5):** `PLAT_getCPUHwRangeKhz` — Auto's bounds
now span every online cpufreq policy, not cpu0 alone. Commit 12bf8e30 clamped
Auto's range to cpu0's 1416000 kHz (an effective ~1344 MHz after rounding), so
on the Smart Pro S every minarch pak that keeps cpu4 online and ships no
`minarch_cpu_max` runs Auto over the full range and drove the big core at ~1344
MHz instead of its own 2160 MHz — the whole uncapped, cpu4-online set (PS, 32X,
SEGACD, GPGX, PCE, SUPA, PUAE, the C64/C128/VIC/PET/PLUS4 line, CPC, MSX,
A2600/A5200/A7800, COLECO, SG1000, SMS, GG, SGB, FDS, LYNX, NGP/NGPC, PKM, VB,
PRBOOM, P8), not just PS as an earlier note implied; the PS caps 2160/1680
collapsing to one effective run was only its most visible symptom. The 8 capped
paks (GB/GBC/GBA/MD/FC/SFC/MGBA/FBN) offline cpu4 and were unaffected, and
tg5040 is unaffected (single cluster, weak cpu0-only fallback). The regression
is present in the committed 12bf8e30; the staged `PLAT_getCPUHwRangeKhz`
restores the pre-12bf8e30 behaviour. **Device check PASSED (2026-09-21):** with the
fix build on the Smart Pro S, cpu4 `scaling_max_freq` = 2160000 and `khz4` reached
1680000 both with a temporary `minarch_cpu_max = 2160` and in the shipped uncapped
case (pre-fix both were pinned at 1344000). Release gate satisfied.

Open items:

- [x] Smart Pro S re-sweep with the Task 4.5 fix (2026-09-21): SFC/FBN/MGBA `none`
  vs `big` vs `big2` at 1032/936/792, PS `base`/`script`/`none`/`big`/`big2` at
  the now-meaningful caps. `affinity: swept N thread(s) matching "mali-"` reported
  **N = 10** on every `big`/`big2` run of all four cores.
- [x] Task 4.5 device check on the Smart Pro S — PASSED (cpu4 max 2160000 with cpu4
  online, capped and uncapped; `khz4` reached 1680000). Release gate satisfied.
- [x] Restore/verify PS.pak on the Smart Pro S — restored + md5-verified
  (`default.cfg` 874221a48e987a24f8c9bf7079bc9071, `launch.sh`
  5f036aa6ee3f55debb982fd9804261f5); SFC/FBN/MGBA paks also restored md5-OK after
  the re-sweep.
- [ ] Investigate the wedge during PS exit under `big2` (repro: PS, cpu4+cpu5
  online, key `big`, menu-quit) — did **not** recur in the 2026-09-21 re-sweep;
  trigger still unexplained.
- [ ] Brick sporadic ~75 ms single-frame stall still unexplained (daemon herding
  did not help — daemon placement is not the cause). The Smart Pro S carries the
  same class of sporadic ~47-91 ms single-frame stall.

**Ruling (2026-09-21): exactly one pak ships the key — PS.pak on the Smart Pro S
(tg5050).** Its `default.cfg` now carries `minarch_cpu_affinity = big` and its
`launch.sh` keeps `echo 1 > cpu5/online` while dropping the old `taskset -c 4,5`
+ `pin_threads` block, because `big2` measured equal-or-better than that script
(drops 283-306 vs 384, worst 47.7 vs 65.2 ms, 0 vs 1 stall): the key floats
`pcsxr-drc` across both big cores and sweeps render + `mali-*` onto cpu0-1. PS
stays full-range (no `minarch_cpu_max`). **MGBA (Smart Pro S) is not adopted:**
topology-A cap 1032 already passes, and `big`'s only edge is a step lower than
`none`-on-big — not over the shipped little-cluster profile — so it does not
earn the extra big core; revisit if GBA titles drop in the field. Everything
else stands unchanged: the Brick ships no affinity key and no herding; Smart
Pro S SFC/FBN keep their topology-A cap-1032 profiles. The mechanism is built
and verified on both devices, off by default on every other pak.

## minarch per-core CPU profiles (built + measured 2026-09-20)

Per-pak CPU profiles ship as pak data: each measured Emu pak's `default.cfg`
carries `minarch_cpu_max` (with `minarch_cpu_min` left at the table floor, 408),
so the in-app CPU Speed **Auto** runs the core inside a measured range instead of
the full clock. Shipped alongside in minarch/platform: `ma_bench.c`
(`NX_BENCH=1` → one `[bench]` line per second), `ma_cpu_profile.c` + the
`minarch_cpu_min` / `minarch_cpu_max` keys, `PLAT_setCPUSpeedRange`
(cluster-aware on tg5050; Auto governor `CPU_AUTO_GOVERNOR` = schedutil on both),
and the platform range writers now emit min-max-min (which also fixes
`CPU_SPEED_MENU` from a fixed-clock preset). Host tests
`scripts/tests/test-minarch-bench.sh` and
`scripts/tests/test-minarch-cpu-profile.sh`; the sweep harness is `scripts/bench/`
(its README documents the config specs and the pass rule; per-device results in
`scripts/bench/results/<plat>/SUMMARY.md`). tg5040 caps `cpu0` (all four A53s
share one domain). On tg5050 the pak `launch.sh` runs **topology A** —
`echo 0 > cpu4/online` first, so the core runs on the two little cores and
`default.cfg` caps the little cluster; minarch still tries to pin itself to
cpu4-7 at start and logs a harmless "Failed to pin" when cpu4 is offline. All 16
shipped rows (8 cores × 2 devices) pass; the Brick rows were re-taken at 4 cores
after fix round 1b made the driver re-online cpu1-3 after every run.

**Per-core profiles and shipped-row pass metrics** (`shipped` row of each
core's SUMMARY table; fps mean/min · drops · worst ms):

| core | scene (title/attract) | tg5040 cap | tg5050 profile | tg5040 shipped | tg5050 shipped |
|---|---|---|---|---|---|
| GB | Pokemon - Red | 816 | A792 | 60.3/60.3 · 6 · 74.9 | 63.0/62.8 · 13 · 18.7 |
| GBC | Pokemon Pinball | 816 | A792 | 60.3/60.2 · 0 · 17.6 | 63.0/62.8 · 11 · 19.0 |
| GBA | Zelda: The Minish Cap | 816 | A792 | 60.3/60.3 · 1 · 17.8 | 63.0/62.9 · 21 · 22.6 |
| MGBA | Iridion II | 1200 | A1032 | 60.3/60.2 · 2 · 25.1 | 63.0/61.9 · 126 · 31.6 |
| FC | Mega Man 2 | 1008 | A1032 | 60.3/60.2 · 0 · 17.6 | 63.0/62.7 · 50 · 24.3 |
| SFC | Yoshi's Island | 1200 | A1032 | 60.3/60.2 · 1 · 18.4 | 63.0/62.6 · 48 · 25.9 |
| MD | Sonic 3D Blast | 816 | A792 | 60.3/60.3 · 4 · 61.7 | 63.0/62.8 · 19 · 49.5 |
| FBN | Metal Slug (mslug) | 1200 | A1032 | 60.3/60.2 · 1 · 18.5 | 63.0/62.7 · 29 · 20.0 |
| PS | Tekken 3 | no cap (full range) | no ROM | base 60.3/60.2 · 18 · 65.2 | — |
| GPGX | Streets of Rage 2 | unmeasured | unmeasured | — | — |

The little/heavy split: GB/GBC/GBA/MD are light (chosen at the low steps — 816 on
tg5040, A792 on tg5050); MGBA/FC/SFC/FBN are real load (1008-1200 / A1032). PS
ships no profile — it passes only at cap1608 and base, with fps min landing on the
95% line, marginal even uncapped, so it keeps the full range per ruling. GPGX pak
exists in the tree (commit 2805e8c5) but was not deployed on either test card, so
it was never measured and keeps the full range. tg5050 shows higher drop counts
because the panel runs ~63 fps against ~60 Hz and the periodic resync frame counts
as jitter; every shipped row still passes its baseline-relative bounds.

Measurement notes:

- Pass rule is baseline-relative for both drops and worst frame:
  `perf.frame_drops` counts any frame > 1.1× the display period, so it registers
  jitter as well as misses; a literal zero-drop / flat worst-frame bound failed
  healthy cores.
- The Genesis and PlayStation attract scenes carry a periodic 48-69 ms frame at
  every cap (uncapped included) — a scene/display hitch, not starvation. Under the
  baseline-relative rule MD passes (chosen 816 / A792).
- A sweep-wide sporadic 68-75 ms single-frame stall hits ~1/3 of runs regardless
  of cap; every *chosen* row was taken on a clean window.
- No 2-core Brick profile ships: at the chosen cap two cores worsened the worst
  frame +21% to +98% (fps min held), so four cores stays the default.

**Brick Auto governor — kept schedutil.** Re-ran GB@816, SFC@1200, MGBA@1200
under each governor at its chosen cap. A governor replaces schedutil only if it
passes all three cores **and** is ≥15% cheaper (mean kHz) on ≥2 of them; ties go
to schedutil.

| core @ cap | governor | fps mean/min | worst ms | mean kHz | kHz vs su | pass |
|---|---|---|---|---|---|---|
| GB @ 816 | schedutil | 60.3/59.9 | 78.8 | 694759 | — | yes |
| GB @ 816 | ondemand | 60.3/59.9 | 22.6 | 816000 | +17.5% | yes |
| GB @ 816 | interactive | 60.2/59.8 | 32.8 | 576000 | -17.1% | yes |
| SFC @ 1200 | schedutil | 60.3/59.9 | 78.1 | 1179724 | — | no* |
| SFC @ 1200 | ondemand | 60.3/60.0 | 27.5 | 1200000 | +1.7% | yes |
| SFC @ 1200 | interactive | 60.3/59.8 | 31.6 | 1176414 | -0.3% | yes |
| MGBA @ 1200 | schedutil | 60.3/59.9 | 28.7 | 1159158 | — | yes |
| MGBA @ 1200 | ondemand | 60.3/60.0 | 37.5 | 1200000 | +3.5% | yes |
| MGBA @ 1200 | interactive | 60.3/60.0 | 32.0 | 1136690 | -1.9% | yes |

ondemand is *more* expensive than schedutil on all three cores (0 meet the bar);
interactive is ≥15% cheaper only on GB (1 core, short of two). Neither clears the
bar, so schedutil is retained (the decision rests on mean kHz). *schedutil's SFC
"no" caught the sweep-wide sporadic ~78 ms stall, not starvation.

**Unmeasured (no ROM on either card, keep today's full range — spec §4.2):** PCE,
SMS, GG, 32X, SEGACD, NGP/NGPC, LYNX, VB, WSC, PKM, A2600/5200/7800, COLECO, MSX,
CPC, C64/C128/PET/PLUS4/VIC, PUAE, PRBOOM, FDS, SGB, SUPA, P8, DOS.

Open items:

- [x] tg5050 user presets now apply to every online cluster (`setAllOnlinePoliciesRange`, 2026-09-20): a profiled pak keeps cpu4 offline so
  Powersave/Normal/Performance pin cpu0 alone; an unprofiled pak pins cpu0 and
  cpu4 both. Smart Pro S readings — profiled+Powersave cpu0 1224000 (rounds
  1200000); profiled+Performance cpu0 1416000 (2160000 clamped); profiled+Auto
  cpu0 408000-792000; unprofiled+Powersave (`online=0-1,4`) cpu0 1224000 & cpu4
  1200000. Launcher menu speeds stay big-core only.
- [ ] Untethered battery A/B on GB and SFC (cheapest cap vs full range) — the
  tether charges, so power was never measured directly; use the Battery app's
  discharge readout.
- [ ] GPGX: deploy the pak to a test card and sweep it; until then it keeps the
  full range (genesis_plus_gx is generally heavier than MD/picodrive).
- [ ] `Config_load` device-override semantics: `default-<DEVICE>.cfg` replaces
  `default.cfg` wholesale, so any new `default.cfg` key silently drops on a pak
  that ships an override (why the FC/SFC/FBN caps had to be copied into
  `default-brick.cfg` / `default-brickpro.cfg`). Consider overlay semantics
  (broader minarch change, not done).
- [ ] Sporadic 68-75 ms single-frame stall: cause unknown (candidates: autosave /
  RetroAchievements / SD write / OSD daemon); frequency-independent, ~1/3 of runs.
- [ ] Re-run the unmeasured systems (spec §4.2) once ROMs for them exist on a card.
- [ ] Tier 2 spike (spec §7): frame-time-adaptive scaling inside Auto — later,
  separate effort, with this harness as ground truth.
- [ ] PS: marginal even uncapped on the Brick (fps min 57-60 in Tekken 3 attract);
  ships no profile — revisit if a heavier scene, a Brick Pro, or a firmware change
  moves the numbers.

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
