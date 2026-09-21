# Brick (tg5040) — minarch thread-affinity sweep (Task 4)

Serial `5c000c8997414781d1d`, `DEVICE=brick`. `BENCH_SECS=60`, run 2026-09-20.
Single big cluster (cpu0 governs cpu0-3); caps are `cpu0/cpufreq/scaling_max_freq`.
On this device `minarch_cpu_affinity = big` means **isolation**: the emulation/main
thread and every core-created thread pin to **cpu3** (FAST), minarch's helpers
(render `PrepareFrameThr`, audio `SDLAudioP2`) and the GPU driver's threads pin to
**cpu0-2** (SLOW). `none` = today's behaviour (the startup `PLAT_pinToCores`
targets cpu4-7, which do not exist here, so it fails harmlessly and every thread
spreads over cpu0-3). `herd` = `big` plus the `BENCH_PRELAUNCH` daemon herd
(`taskset -p 0x3` of keymon/audiomon/musicplayerd/trimui_inputd/trimui_osdd(.xbox)
onto cpu0-1 before launch; restored to `0xf` after). Every variant is pak data
(`default-brick.cfg` for PS/SFC/FBN, `default.cfg` for MGBA), no sysfs overrides.

Pass rule (spec §4.4 fix round 2, unchanged): fps mean ≥ 99 % req, fps min ≥ 95 %
req, buf_free never 0, drops ≤ max(2·n, 1.5·base_drops), worst ≤ max(2.5·period,
1.25·base_worst). Baselines are this sweep's fresh `base` (full range, key absent)
per core.

**Sporadic single-frame stall (carried over from the cap sweep):** a ~65–76 ms
one-off frame appears in a minority of runs at random caps, frequency-independent,
with fps at req and cpu 66–97 %. It trips the worst-frame clause (→ a `no` in the
table) without being CPU starvation; where it is the *only* failed clause the run
is genuinely healthy. Rows below flag which `no`s are this stall vs a real
shortfall (fps-min / drops).

## Summary

| core | scene | none | big (isolation) | herd | decision |
|---|---|---|---|---|---|
| PS | Tekken 3 attract | pass @1608 (2000 fails fps-min 55.3 + stall) | fails 2000 & 1608 (stall) | fails 2000 & 1608 (fps-min) | **nothing ships** — keep full range; big/herd do not beat none, core is marginal even at base |
| SFC | Yoshi's Island | pass @1008 & @1200(stall-only) | pass @1200, **fail @1008 (drops 183)** | pass @1200, fail @1008 | **nothing ships** — big's cheapest pass (1200) is *higher* than none's (1008); isolation hurts |
| FBN | mslug attract | pass @1008 & @1200 | pass @1008 & @1200 | pass @1008 & @1200 | **nothing ships** — tie (both pass at the cheapest tested cap 1008) |
| MGBA | Iridion II | pass @1008 & @1200 | pass @1200, fail @1008 (stall) | pass @1200, fail @1008 (stall) | **nothing ships** — big's cheapest clean pass (1200) ≥ none's (1008); no gain |

**Brick daemon herding: does NOT ship.** stalls/min are already ~0 for `big` and
`herd` is equal-or-worse (below); the ≥ 50 % reduction across the four cores is not
met and fps is unchanged. **Brick `big` (isolation): does NOT ship for any core**
— it never passes at a lower cap than `none`; on SFC/MGBA it raises the cheapest
passing cap and on SFC@1008 it triples drops. **Today's cap-only profiles stand**
(PS full range; SFC/FBN/MGBA cap 1200; the cap sweep's other cores unaffected).

### Herding stalls/min (big vs herd), per core per cap

| core | cap | big stalls/min | herd stalls/min | Δ |
|---|---|---|---|---|
| PS | 2000 | 1.0 | 1.0 | 0 % |
| PS | 1608 | 1.0 | 1.0 | 0 % |
| SFC | 1200 | 0.0 | 0.0 | — |
| SFC | 1008 | 0.0 | 1.0 | worse |
| FBN | 1200 | 0.0 | 0.0 | — |
| FBN | 1008 | 0.0 | 0.0 | — |
| MGBA | 1200 | 0.0 | 0.0 | — |
| MGBA | 1008 | 1.0 | 1.0 | 0 % |

No ≥ 50 % drop on any core (flat, or worse on SFC@1008). Herding does not ship.

---

## PS — Tekken 3 attract/demo (heaviest core)

Caps: 2000 (full) and 1608. base cpu 66 %, fps min 57.6 (the core is marginal even
uncapped — matches the cap sweep, where PS keeps the full range).

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 60.2/57.6 (60.0) | 41 | 116 | 1004 | 47.4 | 59.2 | 66 | 1796000 | 0.0 | yes |
| none2000 | 60.2/55.3 (60.0) | 56 | 116 | 1298 | 75.4 | 59.2 | 66 | 1856966 | 1.0 | no (fps-min 55.3 + stall) |
| none1608 | 60.2/58.0 (60.0) | 82 | 116 | 2244 | 49.9 | 59.2 | 70 | 1566621 | 0.0 | yes |
| big2000 | 60.2/57.7 (60.0) | 72 | 116 | 1310 | 76.1 | 59.2 | 66 | 1798759 | 1.0 | no (stall only) |
| big1608 | 60.3/57.5 (60.0) | 110 | 116 | 751 | 74.8 | 59.2 | 70 | 1532690 | 1.0 | no (stall only) |
| herd2000 | 60.2/56.7 (60.0) | 69 | 116 | 1003 | 76.7 | 59.2 | 66 | 1786621 | 1.0 | no (fps-min + stall) |
| herd1608 | 60.2/56.9 (60.0) | 113 | 116 | 1556 | 66.2 | 59.2 | 69 | 1536414 | 1.0 | no (fps-min + stall) |

### none2000 threads (all threads spread over cpu0-3)
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 30% cpu1 29% cpu2 22% cpu3 20% | 153 |
| SDLAudioP2 (1 tid) | cpu0 34% cpu1 25% cpu2 19% cpu3 22% | 161 |
| minarch.elf (7 tids) | cpu0 36% cpu1 16% cpu2 23% cpu3 25% | 659 |
| pcsxr-cdrom (1 tid) | cpu0 87% cpu1 6% cpu2 5% cpu3 2% | 25 |
| pcsxr-drc (1 tid) | cpu0 29% cpu1 40% cpu2 19% cpu3 12% | 31 |

### big2000 threads (emu isolated to cpu3, helpers on cpu0-2)
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 38% cpu1 29% cpu2 33% | 130 |
| SDLAudioP2 (1 tid) | cpu0 37% cpu1 32% cpu2 31% | 152 |
| minarch.elf (7 tids) | cpu0 25% cpu1 16% cpu2 17% cpu3 43% | 432 |
| pcsxr-cdrom (1 tid) | cpu3 100% | 0 |
| pcsxr-drc (1 tid) | cpu3 100% | 0 |

(Idle pools are omitted from these tables — `SDLHotplugALSA`, and `HTTPRequest` on
SFC/FBN/MGBA — because idle threads report only their *last* cpu, not real
residency; under `big` they sit at `cpu3 100%`, which is dormancy, not load. Read
the busy threads. This applies to every thread table below.)

**Decision: nothing ships (keep full range, no key, no herd).** `pcsxr-drc` is
correctly isolated to cpu3 under `big`, but PS is marginal on the Brick at every
placement: `none` clears only at 1608 (and 2000 only misses on fps-min + the
sporadic stall), while `big` and `herd` fail both caps (`big` on the sporadic
worst-frame at fps-min ≈ 57.5, `herd` on fps-min < 57.0). big never passes where
none fails nor at a lower cap; herd removes no stalls. PS keeps the full range it
already ships.

---

## SFC — Super Mario World 2: Yoshi's Island (snes9x, single core thread)

Caps: 1200 (current) and 1008. base cpu 70 %.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.0 (60.1) | 5 | 116 | 2357 | 24.5 | 41.6 | 70 | 1870897 | 0.0 | yes |
| none1200 | 60.3/60.1 (60.1) | 24 | 116 | 2453 | 75.9 | 41.6 | 89 | 1186345 | 1.0 | no (stall only — fps at req) |
| none1008 | 60.3/58.3 (60.1) | 58 | 116 | 1852 | 22.9 | 41.6 | 97 | 990621 | 0.0 | yes |
| big1200 | 60.3/60.1 (60.1) | 62 | 116 | 2593 | 33.4 | 41.6 | 87 | 1200000 | 0.0 | yes |
| big1008 | 60.3/58.4 (60.1) | 183 | 118 | 1319 | 28.4 | 41.6 | 97 | 984000 | 0.0 | no (drops 183 > 118) |
| herd1200 | 60.3/60.1 (60.1) | 64 | 116 | 2515 | 27.9 | 41.6 | 88 | 1186345 | 0.0 | yes |
| herd1008 | 60.4/59.5 (60.1) | 141 | 116 | 2334 | 74.5 | 41.6 | 96 | 994345 | 1.0 | no (drops 141 + stall) |

### none1200 threads (spread over cpu0-3; the only core thread is minarch main)
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 27% cpu1 33% cpu2 22% cpu3 19% | 179 |
| SDLAudioP2 (1 tid) | cpu0 28% cpu1 35% cpu2 22% cpu3 15% | 170 |
| minarch.elf (6 tids) | cpu0 25% cpu1 23% cpu2 30% cpu3 21% | 553 |

### big1200 threads (main/emu isolated to cpu3, helpers on cpu0-2)
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 34% cpu1 32% cpu2 34% | 145 |
| SDLAudioP2 (1 tid) | cpu0 43% cpu1 35% cpu2 22% | 166 |
| minarch.elf (6 tids) | cpu0 25% cpu1 20% cpu2 16% cpu3 39% | 287 |

**Decision: nothing ships.** `none` passes at **1008** (a clean run; the 1200 `no`
is the sporadic stall only, fps at req). `big` passes at **1200** but fails at
**1008** — drops 183 > bound 118 at cpu 97 %, a real shortfall: giving snes9x one
core (cpu3) while three cores idle-ish handle helpers is worse than letting
schedutil use all four. big's cheapest passing cap (1200) is higher than none's
(1008), so big does not beat none. Keep the shipped cap-1200 profile.

---

## FBN — Metal Slug (mslug) attract (fbneo)

Caps: 1200 (current) and 1008. base cpu 65 %.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (59.2) | 2 | 116 | 2288 | 33.2 | 42.2 | 65 | 1746207 | 0.0 | yes |
| none1200 | 60.3/60.1 (59.2) | 3 | 114 | 2404 | 19.6 | 42.2 | 82 | 1133895 | 0.0 | yes |
| none1008 | 60.3/59.9 (59.2) | 23 | 116 | 1383 | 27.3 | 42.2 | 91 | 997655 | 0.0 | yes |
| big1200 | 60.3/59.8 (59.2) | 48 | 116 | 2522 | 23.6 | 42.2 | 81 | 1135034 | 0.0 | yes |
| big1008 | 60.4/59.7 (59.2) | 59 | 116 | 1382 | 34.2 | 42.2 | 90 | 973655 | 0.0 | yes |
| herd1200 | 60.3/59.9 (59.2) | 55 | 116 | 2376 | 28.0 | 42.2 | 82 | 1189655 | 0.0 | yes |
| herd1008 | 60.3/60.1 (59.2) | 68 | 116 | 2414 | 25.7 | 42.2 | 90 | 997655 | 0.0 | yes |

### none1200 threads
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 32% cpu1 27% cpu2 22% cpu3 19% | 162 |
| SDLAudioP2 (1 tid) | cpu0 26% cpu1 32% cpu2 24% cpu3 17% | 178 |
| minarch.elf (6 tids) | cpu0 26% cpu1 23% cpu2 28% cpu3 22% | 705 |

### big1200 threads
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 38% cpu1 30% cpu2 32% | 162 |
| SDLAudioP2 (1 tid) | cpu0 37% cpu1 37% cpu2 26% | 144 |
| minarch.elf (6 tids) | cpu0 25% cpu1 19% cpu2 22% cpu3 33% | 467 |

**Decision: nothing ships (tie).** `none` and `big` both pass at every tested cap,
so both have the same cheapest passing cap (1008). big does not pass at a lower cap
than none → tie → today's cap-1200 profile stands. (big at 1008 is healthy but not
cheaper; drops rise modestly, 23 → 59, without failing.)

---

## MGBA — Iridion II (mgba)

Caps: 1200 (current) and 1008. base cpu 63 %.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | stalls/min | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.0 (59.7) | 4 | 116 | 2386 | 20.5 | 41.9 | 63 | 1786897 | 0.0 | yes |
| none1200 | 60.3/60.0 (59.7) | 4 | 114 | 2430 | 20.4 | 41.9 | 81 | 1193263 | 0.0 | yes |
| none1008 | 60.3/59.4 (59.7) | 36 | 116 | 2310 | 22.5 | 41.9 | 91 | 990621 | 0.0 | yes |
| big1200 | 60.3/60.0 (59.7) | 54 | 116 | 2711 | 26.7 | 41.9 | 81 | 1163172 | 0.0 | yes |
| big1008 | 60.4/59.8 (59.7) | 99 | 116 | 2734 | 74.5 | 41.9 | 91 | 987310 | 1.0 | no (stall only — fps at req, drops 99 < 116) |
| herd1200 | 60.3/60.0 (59.7) | 68 | 114 | 2548 | 35.9 | 41.9 | 81 | 1165053 | 0.0 | yes |
| herd1008 | 60.3/59.8 (59.7) | 105 | 116 | 2326 | 65.4 | 41.9 | 90 | 1000966 | 1.0 | no (stall only) |

### none1200 threads
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 33% cpu1 26% cpu2 27% cpu3 14% | 172 |
| SDLAudioP2 (1 tid) | cpu0 35% cpu1 26% cpu2 22% cpu3 16% | 183 |
| minarch.elf (6 tids) | cpu0 29% cpu1 24% cpu2 24% cpu3 23% | 575 |

### big1200 threads
| thread | residency % per cpu | migrations |
|---|---|---|
| PrepareFrameThr (1 tid) | cpu0 37% cpu1 31% cpu2 31% | 170 |
| SDLAudioP2 (1 tid) | cpu0 33% cpu1 38% cpu2 28% | 156 |
| minarch.elf (6 tids) | cpu0 28% cpu1 15% cpu2 17% cpu3 39% | 316 |

**Decision: nothing ships.** `none` passes at both 1008 and 1200. `big` passes at
1200 but its 1008 run failed on the sporadic worst-frame stall only (fps at req,
drops 99 < 116) — at best a tie with none@1008, never cheaper. big does not beat
none → keep the shipped cap-1200 profile.

---

## Notes

- All 60 s runs; the emulator was quit through minarch's own in-game menu (never
  SIGTERM). All four `default-brick.cfg`/`default.cfg` files were restored after
  their core and verified byte-for-byte (md5) against the pre-sweep backup. Raw logs
  under `results/tg5040/` are gitignored.
- Consistent thread placement was verified from the `[thr]` tables: `big` puts
  `pcsxr-drc` (PS) / the minarch main thread (SFC/FBN/MGBA) on cpu3 and the render/
  audio/GPU helpers on cpu0-2; `none` spreads everything over cpu0-3. The mechanism
  works; it simply gives the Brick no headroom, because trading a fourth general
  core for one reserved core does not help cores that schedutil already keeps at fps
  across four cores, and hurts the ones near saturation (SFC/MGBA at 1008).
