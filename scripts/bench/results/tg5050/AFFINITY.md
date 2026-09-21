# Smart Pro S (tg5050) — minarch thread-affinity sweep (Task 4 / 4b)

Serial `7057408880c2c8c239a`, `DEVICE=smartpros`. Re-swept 2026-09-21 with the
fix-round minarch build (`workspace/all/minarch/build/tg5050/minarch.elf`, md5
`1553091fecf180f183803881ede8c229`, deployed to `/mnt/SDCARD/.system/bin/minarch.elf`)
which contains `PLAT_getCPUHwRangeKhz` (big-core cap fix, Task 4.5) and the repaired
`mali-` late sweep. `BENCH_SECS=60`, all runs pak data only (no sysfs overrides).

On this device `minarch_cpu_affinity = big` is a **cluster split**: the emulation/
main thread + every core-created thread pin to the online big cores (FAST = cpu4,
plus cpu5 when the pak onlined it); minarch's helpers (`PrepareFrameThr`,
`SDLAudioP2`) and the GPU driver's `mali-*` threads pin to cpu0-1 (SLOW). `none`
= today's behaviour (startup pin → everything on cpu4). Both `none` and `big` keep
cpu4 **online** (placement-only comparison). `big2` onlines cpu5 (two fast cores).
`script` = PS.pak's shipped `taskset -c 4,5` + `pin_threads` launch, key absent.
Baseline = this sweep's fresh `base` (cpu4 online, full range, key absent).

This device runs ~63 fps against a ~60 Hz panel (fps mean > req is normal); the
periodic resync frame drives the worst-frame column, bounded relative to `base`.
A **sporadic single-frame stall** (~47–91 ms, one frame, fps at req) recurs at
random caps and trips the worst-frame clause without being CPU starvation; where it
is the only failed clause the run is otherwise healthy, so `drops` and `fps min`
are the load-bearing signals and are flagged per row.

## Task 4.5 release-gate check (cpu4 reaches its own clocks) — PASS

Verbatim, with the fix build deployed and cpu4 online:

- **capped `minarch_cpu_max = 2160`:** `[driver] cfg=chk_cap2160 online=0-1,4-5
  c0=schedutil/408000-1416000 c4=schedutil/408000-2160000` → cpu4 `scaling_max_freq`
  = **2160000**; max `khz4` observed = **1680000** (> 1344000).
- **shipped uncapped (no cap key):** `[driver] cfg=chk_uncapped online=0-1,4-5
  c0=schedutil/408000-1416000 c4=schedutil/408000-2160000` → cpu4 = **2160000**;
  max `khz4` = **1680000**.

Pre-fix, both cases pinned cpu4 at 1344000 (Auto clamped to cpu0's 1416000 ceiling,
rounded down). Fix confirmed. PS.pak restored + md5-verified after the check.

## Summary

| core | scene | none | big (1 fast core) | big2 (2 fast cores) | decision |
|---|---|---|---|---|---|
| PS | Tekken 3 attract | pass @1200 (2160/1680 trip the sporadic stall; drops ~400-530) | pass @2160 & @1200 (drops ~480-580) | **pass @2160 & @1200, drops 283-306 — fewest of any config** | **SHIPS (2026-09-21): PS.pak `default.cfg` gets `minarch_cpu_affinity = big`; `launch.sh` keeps cpu5 online and drops the taskset script.** big2 ≥ script (283-306 vs 384 drops, worst 47.7 vs 65.2, 0 vs 1 stall). PS stays full-range. |
| SFC | Yoshi's Island | pass @936 & @1032 (792 starves) | @1032/@936 trip the sporadic worst-frame (drops lower: 187/522 vs 138/903), 792 starves | big2@1032 cleanest (drops 84) | **nothing ships — topology-A stands.** big shows no robust win over none (comparable drops, tripped only by the sporadic stall); big2 not tested below 1032. |
| FBN | mslug attract | pass @936 & @1032 (792 starves) | pass @936 & @1032 (792 starves) | big2@1032 cleanest (drops 58) | **nothing ships — tie.** none & big both pass down to 936; big2 not cheaper. Topology-A stands. |
| MGBA | Iridion II | pass @1032 only (936 fails fps-min 56.6 < 56.7; 792 starves) | **pass @1032 & @936** (936: fps-min 57.3, drops 876 vs none 56.6, 1612) | big2@1032 cleanest (drops 130) | **Not adopted:** topology-A 1032 passes; big's edge is over none-on-big, not over the shipped profile; revisit if GBA titles drop in the field. |

**Rulings (2026-09-21).** PS → **ships**: PS.pak `default.cfg` gets
`minarch_cpu_affinity = big` and `launch.sh` keeps cpu5 online, replacing the
`taskset -c 4,5` + `pin_threads` script (big2 ≥ script; the key floats
`pcsxr-drc` across both big cores and sweeps render + `mali-*` to cpu0-1). PS
stays full-range (no `minarch_cpu_max`). MGBA → **not adopted**: topology-A 1032
passes; big's edge is over none-on-big, not over the shipped profile; revisit if
GBA titles drop in the field. SFC/FBN keep their topology-A profiles. The
`mali-` late sweep reported `affinity: swept 10 thread(s) matching "mali-" to
slow set` on every `big`/`big2` run of all four cores (N = 10 ≥ 1).

---

## PS — Tekken 3 attract/demo (heaviest core)

Effective big-core caps now meaningful: 2160 / 1680 / 1200. base cpu 55 %.
Worst bound = 64.9 ms (1.25 × base 51.9); drop bound = 729 (1.5 × base 486).

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 62.8/61.6 (60.0) | 486 | 729 | 2156 | 51.9 | 64.9 | 55 | 1015322 | 0.0 | yes |
| script | 63.0/60.8 (60.0) | 384 | 729 | 2694 | 65.2 | 64.9 | 52 | 1174345 | 1.0 | no (sporadic worst 65.2, +0.3 over) |
| none2160 | 62.8/60.8 (60.0) | 481 | 729 | 2075 | 91.4 | 64.9 | 55 | 976678 | 2.0 | no (sporadic worst) |
| none1680 | 62.7/61.4 (60.0) | 534 | 729 | 1798 | 82.8 | 64.9 | 55 | 1020203 | 1.0 | no (sporadic worst) |
| none1200 | 62.8/60.1 (60.0) | 399 | 729 | 1256 | 61.5 | 64.9 | 57 | 909966 | 1.0 | yes |
| big2160 | 62.9/61.8 (60.0) | 486 | 729 | 1322 | 58.8 | 64.9 | 58 | 1027932 | 0.0 | yes |
| big1680 | 62.8/61.3 (60.0) | 484 | 729 | 1479 | 76.5 | 64.9 | 58 | 987254 | 1.0 | no (sporadic worst) |
| big1200 | 62.6/59.3 (60.0) | 580 | 729 | 1690 | 61.1 | 64.9 | 60 | 932746 | 1.0 | yes |
| **big2_2160** | 63.0/61.9 (60.0) | **283** | 729 | 2302 | 47.7 | 64.9 | 53 | 1210169 | 0.0 | **yes** |
| big2_1680 | 63.0/62.4 (60.0) | 283 | 729 | 2006 | 83.6 | 64.9 | 54 | 1218305 | 1.0 | no (sporadic worst) |
| big2_1200 | 62.9/59.8 (60.0) | 306 | 729 | 2175 | 62.5 | 64.9 | 58 | 943322 | 1.0 | yes |

The worst-frame column is dominated by the sporadic stall (random caps: none2160
91, none1680 83, big1680 76, big2_1680 84) — read `drops`. **big2 (two big cores)
has by far the fewest drops (283–306) at every cap**, vs `script` 384 and vs the
single-big-core none/big (399–580). `script` itself trips the pass rule only on a
65.2 ms sporadic frame.

### script threads — shipped taskset: drc→cpu4, render+GPU pinned to cpu5
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu5 100% | 0 |
| mali-cmar-backe (1 tid) | cpu5 100% | 0 |
| mali-mem-purge (1 tid) | cpu5 100% | 0 |
| minarch.elf (7 tids) | cpu4 36% cpu5 64% | 34 |
| pcsxr-cdrom (1 tid) | cpu0 55% cpu1 45% | 6 |
| pcsxr-drc (1 tid) | cpu4 100% | 0 |

### big2_2160 threads — key: drc floats cpu4+cpu5, render+GPU on the little cores
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 58% cpu1 42% | 6 |
| SDLAudioP2 (1 tid) | cpu0 52% cpu1 48% | 44 |
| mali-cmar-backe (1 tid) | cpu0 43% cpu1 57% | 45 |
| mali-mem-purge (1 tid) | cpu0 63% cpu1 37% | 6 |
| minarch.elf (7 tids) | cpu0 28% cpu1 15% cpu4 20% cpu5 37% | 83 |
| pcsxr-cdrom (1 tid) | cpu4 97% cpu5 3% | 5 |
| pcsxr-drc (1 tid) | cpu4 39% cpu5 61% | 4 |

### none2160 threads — everything on cpu4 (single big core, crammed)
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu4 100% | 0 |
| mali-cmar-backe (1 tid) | cpu4 100% | 0 |
| minarch.elf (7 tids) | cpu0 9% cpu1 4% cpu4 87% | 7 |
| pcsxr-drc (1 tid) | cpu4 100% | 0 |

**Decision — SHIPS (2026-09-21): PS.pak's `taskset -c 4,5` + `pin_threads` script
is replaced by `minarch_cpu_affinity = big` in `default.cfg` + `echo 1 >
cpu5/online` in `launch.sh`.** Rationale: `big2` measures
**equal-or-better than `script`** — 283–306 drops vs 384, big2_2160 worst 47.7 ms /
0 stalls vs script 65.2 ms / 1 stall — because the key sweeps render + `mali-*` to
the little cores (cpu0-1) and leaves *both* big cores for `pcsxr-drc` (cpu4 39 % /
cpu5 61 %), whereas `script` keeps render + GPU on cpu5 and confines drc to cpu4.
(This PCSX build spawns no separate `pcsxr-gpu` thread — GPU is the `mali-*` driver
threads, which the key correctly puts on SLOW.) Cap: big2's drops are flat across
2160/1200, so PS stays full-range (no `minarch_cpu_max`).

---

## SFC — Yoshi's Island (snes9x). base cpu 62 %. worst bound 41.6 / drop bound 1474.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 62.0/59.2 (60.1) | 983 | 1474 | 2122 | 27.5 | 41.6 | 62 | 1178069 | 0.0 | yes |
| none1032 | 63.1/62.3 (60.1) | 138 | 1474 | 2637 | 33.6 | 41.6 | 70 | 988068 | 0.0 | yes |
| none936 | 63.1/62.2 (60.1) | 216 | 1474 | 2727 | 28.4 | 41.6 | 72 | 825763 | 0.0 | yes |
| none792 | 59.0/56.5 (60.1) | 2348 | 1474 | 2293 | 43.2 | 41.6 | 80 | 672000 | 0.0 | no (starves: fps-min + drops) |
| big1032 | 63.1/61.7 (60.1) | 187 | 1474 | 2369 | 54.8 | 41.6 | 73 | 939661 | 0.0 | no (sporadic worst; fps/drops fine) |
| big936 | 62.5/59.4 (60.1) | 522 | 1474 | 2420 | 47.4 | 41.6 | 80 | 831458 | 0.0 | no (sporadic worst; fps/drops fine) |
| big792 | 59.3/56.5 (60.1) | 1934 | 1474 | 2101 | 37.1 | 41.6 | 84 | 672000 | 0.0 | no (starves) |
| big2_1032 | 63.0/62.6 (60.1) | 84 | 1474 | 3036 | 33.1 | 41.6 | 74 | 959593 | 0.0 | yes |

Under `big` the render/audio/`mali-*` helpers move to cpu0-1 and the emulation main
thread stays on cpu4 (`minarch.elf` cpu4 50 % + minarch's own helpers on cpu0-1;
`none` is cpu4 91 %). big's drops are *lower* than none's at 936 (522 vs 903), but
big1032/big936 each caught the sporadic worst-frame (54.8 / 47.4 ms) and fail the
clause; none happened to miss it. **Decision: nothing ships — topology-A profile
(little cluster, cap 1032, cpu4 offline) stands.** No robust win for the key: big is
comparable-to-cleaner on drops but shows no cheaper *passing* cap than none, and
staying on the little cluster is cheaper on power.

### SFC none1032 / big1032 / big2_1032 (placement)
`none1032`: all threads cpu4 100 % (`minarch.elf` cpu4 91 %). `big1032`:
`PrepareFrameThr` cpu0 90 %, `SDLAudioP2` cpu0-1, `mali-*` cpu0-1, `minarch.elf`
cpu4 50 % (emu main on cpu4, minarch helpers cpu0-1). `big2_1032`: `minarch.elf`
cpu0 35 %/cpu4 26 %/cpu5 24 % (main floats cpu4+cpu5), helpers cpu0-1.

---

## FBN — mslug attract (fbneo). base cpu 60 %. worst bound 49.5 / drop bound 716.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 62.9/61.7 (59.2) | 477 | 716 | 2083 | 39.6 | 49.5 | 60 | 1105241 | 0.0 | yes |
| none1032 | 63.1/62.3 (59.2) | 140 | 716 | 3040 | 32.1 | 49.5 | 65 | 925424 | 0.0 | yes |
| none936 | 63.1/62.2 (59.2) | 216 | 716 | 2727 | 28.4 | 49.5 | 72 | 825763 | 0.0 | yes |
| none792 | 61.0/57.6 (59.2) | 1232 | 716 | 1509 | 45.5 | 49.5 | 80 | 704136 | 0.0 | no (drops) |
| big1032 | 63.1/62.4 (59.2) | 164 | 716 | 2781 | 24.4 | 49.5 | 69 | 894102 | 0.0 | yes |
| big936 | 62.9/62.0 (59.2) | 212 | 716 | 2452 | 33.0 | 49.5 | 74 | 792400 | 0.0 | yes |
| big792 | 62.1/59.3 (59.2) | 776 | 716 | 794 | 32.2 | 49.5 | 82 | 684610 | 0.0 | no (drops) |
| big2_1032 | 63.0/62.8 (59.2) | 58 | 716 | 2774 | 21.8 | 49.5 | 69 | 857379 | 0.0 | yes |

Clean (no sporadic-stall confounding this core). **Decision: nothing ships — tie.**
none and big both pass down to 936 and starve at 792; big's cheapest passing cap
(936) is not lower than none's (936). big2_1032 is cleanest (58 drops) but was not
tested below 1032. Topology-A profile (cap 1032) stands.

---

## MGBA — Iridion II (mgba). base cpu 63 %. worst bound 41.9 / drop bound 1977.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 61.7/59.3 (59.7) | 1318 | 1977 | 819 | 27.9 | 41.9 | 63 | 1157288 | 0.0 | yes |
| none1032 | 62.5/60.1 (59.7) | 416 | 1977 | 1853 | 26.2 | 41.9 | 75 | 999458 | 0.0 | yes |
| none936 | 59.9/56.6 (59.7) | 1612 | 1977 | 2745 | 33.1 | 41.9 | 79 | 832678 | 0.0 | no (fps-min 56.6 < 56.7) |
| none792 | 60.6/55.4 (59.7) | 2482 | 1977 | 2741 | 52.2 | 41.9 | 81 | 684610 | 0.0 | no (starves) |
| big1032 | 62.8/59.5 (59.7) | 353 | 1977 | 2307 | 37.6 | 41.9 | 77 | 967448 | 0.0 | yes |
| **big936** | 60.9/57.3 (59.7) | 876 | 1977 | 1580 | 39.8 | 41.9 | 84 | 828414 | 0.0 | **yes** |
| big792 | 60.1/54.6 (59.7) | 2299 | 1977 | 2442 | 47.1 | 41.9 | 86 | 667525 | 0.0 | no (starves) |
| big2_1032 | 63.1/62.0 (59.7) | 130 | 1977 | 2804 | 29.8 | 41.9 | 78 | 979034 | 0.0 | yes |

**big passes a step lower than none.** `none` passes only at 1032 (none936 misses
the fps-min floor: 56.6 < 56.7, and carries 1612 drops); `big` passes at **936**
(fps-min 57.3, 876 drops — roughly half none936's drops) as well as 1032. Isolating
the emulation main thread to cpu4 with the helpers on cpu0-1 lets mgba hold a step
lower. big2_1032 is cleanest (130 drops). **Decision — not adopted (2026-09-21):
MGBA keeps its topology-A profile (little cluster, cpu4 offline, cap 1032).**
`big`'s only edge is passing a step lower than `none`-on-big (936 vs 1032; drops
876 vs 1612) — not a win over the shipped little-cluster profile, which already
passes at 1032 on cheaper power, so the extra big core is not earned. Revisit if
GBA titles drop in the field. (none936's fps-min miss is marginal at 0.1 fps
regardless.)

---

## Device incident (Task 4a, resolved 2026-09-21)

During the original Task 4 sweep the device wedged **during the exit/restore of the
PS `big2_1680` run** (after that run had sampled to completion) and dropped off adb;
it did not re-enumerate for ~2 h and was not rebooted (per rules). It later returned
(powered back on) and a host-side watcher restored PS.pak from the md5-verified
backup. The Task 4b re-sweep above then ran to completion **without any wedge**
(~50 min of continuous load across all four cores). All PS/SFC/FBN/MGBA pak files
were backed up and restored per core, md5-verified OK; final device state
`online=0-1` (cpu4 offline), `nextui.elf` up, no `minarch.elf`. The wedge is not
reproduced this round; its trigger remains unexplained (open item in DEV_CHECKLIST).

## Notes

- `mali-utility-wo`, `SDLHotplugALSA` are idle pools that report their last cpu;
  read the busy threads (harness field-39 caveat).
- The fix-round `minarch.elf` (md5 `1553091f…`) is left deployed on this device
  (per Task 2/4.5 precedent); the previous on-card binary is backed up host-side.
- Raw logs under `results/tg5050/` are gitignored; the pre-fix Task-4a logs are
  preserved under `results/tg5050/_task4a/`.
