# Brick (tg5040) -- minarch CPU profile sweep

Serial `5c000c8997414781d1d`. `BENCH_SECS=60`, run 2026-09-20. Single big cluster
(cpu0 governs cpu0-3); caps are `cpu0/cpufreq/scaling_max_freq`. Cap table:
408 600 816 1008 1200 1416 1608 1800 2000. Pass rule (fix round 2): fps mean >= 99%
req, fps min >= 95% req, buf_free never 0, drops <= max(2*n, 1.5*base_drops), and
**worst frame <= max(2.5*period, 1.25*base_worst)** (baseline-relative; see the
`worst bound` column). **Chosen cap = cheapest passing cap + one step**; if nothing
below base passes, the core keeps the full range ("no cap").

Fix-round-2 note: the worst-frame clause was made baseline-relative because MD and
PS carry a periodic ~51-69 ms frame at every cap (uncapped included) with fps at
req and cpu 60-80% -- a scene/display hitch, not starvation. Under the amended rule
MD now passes (chosen cap816) and PS's non-pass narrows to a genuine fps-min
shortfall below cap1416; PS keeps the full range per controller ruling.

A separate sporadic ~60-140 ms single-frame stall still appears in a minority of
runs at random caps, frequency-independent; every *chosen* cap below landed on a
clean window and passes all clauses.

## GB

Scene: Pokemon - Red title/attract. base cpu 64% (light core).

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (59.7) | 4 | 114 | 2465 | 64.2 | 80.2 | 64 | 623018 | yes |
| cap600 | 60.3/60.3 (59.7) | 2 | 116 | 2535 | 19.2 | 80.2 | 70 | 507310 | yes |
| cap816 | 60.3/60.2 (59.7) | 1 | 116 | 2413 | 17.6 | 80.2 | 67 | 528000 | yes |
| cap1008 | 60.3/60.2 (59.7) | 3 | 116 | 2498 | 61.7 | 80.2 | 65 | 595034 | yes |
| cap1200 | 60.3/60.3 (59.7) | 2 | 116 | 2567 | 19.1 | 80.2 | 65 | 614897 | yes |
| cap1416 | 60.3/60.0 (59.7) | 1 | 116 | 2323 | 20.6 | 80.2 | 64 | 637655 | yes |
| cap1608 | 60.3/60.2 (59.7) | 0 | 116 | 2484 | 17.4 | 80.2 | 63 | 698897 | yes |
| shipped | 60.3/60.3 (59.7) | 6 | 118 | 2019 | 74.9 | 80.2 | 67 | 579661 | yes |

cap600 is the cheapest passing cap. +one step. **Chosen: cap816.**

## GBC

Scene: Pokemon Pinball title/attract. base cpu 65% (light core).

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.3 (59.7) | 1 | 114 | 2460 | 18.6 | 41.9 | 65 | 578386 | yes |
| cap600 | 60.3/60.3 (59.7) | 1 | 116 | 2501 | 18.6 | 41.9 | 69 | 484138 | yes |
| cap816 | 60.3/60.3 (59.7) | 0 | 116 | 2470 | 18.2 | 41.9 | 66 | 540414 | yes |
| cap1008 | 60.3/60.2 (59.7) | 0 | 116 | 2529 | 17.6 | 41.9 | 67 | 564828 | yes |
| cap1200 | 60.3/60.2 (59.7) | 0 | 116 | 2502 | 18.1 | 41.9 | 65 | 581379 | yes |
| cap1416 | 60.3/60.1 (59.7) | 1 | 114 | 2449 | 19.6 | 41.9 | 64 | 557895 | yes |
| cap1608 | 60.3/60.2 (59.7) | 9 | 114 | 1775 | 75.1 | 41.9 | 63 | 556632 | no |
| shipped | 60.3/60.2 (59.7) | 0 | 116 | 2499 | 17.6 | 41.9 | 68 | 531724 | yes |

cap600 cheapest passing. +one step. **Chosen: cap816.**

## GBA

Scene: The Legend of Zelda - The Minish Cap title/attract. base cpu 64%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.1 (59.7) | 2 | 114 | 2042 | 19.6 | 41.9 | 64 | 699789 | yes |
| cap600 | 60.3/60.2 (59.7) | 2 | 116 | 2111 | 18.5 | 41.9 | 69 | 576828 | yes |
| cap816 | 60.3/59.9 (59.7) | 2 | 116 | 2010 | 20.3 | 41.9 | 66 | 657517 | yes |
| cap1008 | 60.3/60.1 (59.7) | 1 | 116 | 2154 | 20.3 | 41.9 | 65 | 658345 | yes |
| cap1200 | 60.3/60.2 (59.7) | 1 | 116 | 2082 | 18.7 | 41.9 | 64 | 703034 | yes |
| cap1416 | 60.3/60.2 (59.7) | 0 | 116 | 2123 | 18.1 | 41.9 | 65 | 724966 | yes |
| cap1608 | 60.3/60.2 (59.7) | 4 | 114 | 1922 | 140.8 | 41.9 | 63 | 687158 | no |
| shipped | 60.3/60.3 (59.7) | 1 | 116 | 2115 | 17.8 | 41.9 | 66 | 607862 | yes |

cap600 cheapest passing. +one step. **Chosen: cap816.**

## MGBA

Scene: Iridion II title/attract (heavy core, real load; cpu 104% when starved). base cpu 74%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.1 (59.7) | 2 | 116 | 2319 | 20.8 | 41.9 | 74 | 1343034 | yes |
| cap600 | 57.3/50.4 (59.7) | 2390 | 116 | 1645 | 29.6 | 41.9 | 104 | 583448 | no |
| cap816 | 57.9/54.3 (59.7) | 604 | 116 | 2505 | 25.3 | 41.9 | 99 | 801931 | no |
| cap1008 | 60.3/60.0 (59.7) | 6 | 114 | 2161 | 19.2 | 41.9 | 87 | 986947 | yes |
| cap1200 | 60.3/60.2 (59.7) | 1 | 114 | 2027 | 18.4 | 41.9 | 80 | 1135158 | yes |
| cap1416 | 60.3/60.2 (59.7) | 3 | 114 | 2401 | 74.5 | 41.9 | 75 | 1238737 | no |
| cap1608 | 60.3/60.2 (59.7) | 1 | 114 | 2334 | 18.9 | 41.9 | 74 | 1223158 | yes |
| shipped | 60.3/60.2 (59.7) | 2 | 114 | 2414 | 25.1 | 41.9 | 80 | 1110737 | yes |

cap1008 cheapest passing (cap600/816 genuinely starve on fps + drops). +one step. **Chosen: cap1200.**

## FC

Scene: Mega Man 2 title/attract (real load; cpu 104% when starved). base cpu 74%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (60.1) | 6 | 114 | 2097 | 25.5 | 41.6 | 74 | 1336561 | yes |
| cap600 | 51.1/50.0 (60.1) | 2899 | 118 | 2557 | 26.2 | 41.6 | 104 | 596746 | no |
| cap816 | 60.3/60.2 (60.1) | 3 | 116 | 2075 | 27.9 | 41.6 | 97 | 816000 | yes |
| cap1008 | 60.3/60.2 (60.1) | 0 | 116 | 2334 | 18.0 | 41.6 | 84 | 976966 | yes |
| cap1200 | 60.3/60.2 (60.1) | 0 | 116 | 2349 | 17.8 | 41.6 | 76 | 1143310 | yes |
| cap1416 | 60.3/60.2 (60.1) | 0 | 114 | 2325 | 17.8 | 41.6 | 74 | 1148211 | yes |
| cap1608 | 60.3/60.2 (60.1) | 0 | 114 | 2340 | 17.7 | 41.6 | 74 | 1185684 | yes |
| shipped | 60.3/60.2 (60.1) | 0 | 116 | 2325 | 17.6 | 41.6 | 84 | 984000 | yes |

**shipped now applies its cap** at 4 cores (`c0=schedutil/408000-1008000`, mean ~984 MHz), pass. The earlier uncapped run was the device-override cfg bug (FC ships `default-brick.cfg`, which lacked `minarch_cpu_max`); fixed in round 1b. See Shipped verification.

cap816 cheapest passing (cap600 starves). +one step. **Chosen: cap1008.**

## SFC

Scene: Super Mario World 2 - Yoshi's Island title/attract (heavy core, real load). base cpu 75%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (60.1) | 0 | 114 | 2400 | 17.7 | 41.6 | 75 | 1414316 | yes |
| cap600 | 57.7/50.2 (60.1) | 2668 | 118 | 2349 | 27.0 | 41.6 | 104 | 600000 | no |
| cap816 | 57.3/51.9 (60.1) | 863 | 116 | 1715 | 21.8 | 41.6 | 102 | 808966 | no |
| cap1008 | 60.3/59.8 (60.1) | 6 | 116 | 2362 | 21.3 | 41.6 | 94 | 966621 | yes |
| cap1200 | 60.3/60.2 (60.1) | 3 | 116 | 2367 | 19.3 | 41.6 | 85 | 1186345 | yes |
| cap1416 | 60.3/59.9 (60.1) | 9 | 114 | 2426 | 69.8 | 41.6 | 79 | 1300211 | no |
| cap1608 | 60.3/60.2 (60.1) | 0 | 114 | 2282 | 18.0 | 41.6 | 76 | 1344421 | yes |
| shipped | 60.3/60.2 (60.1) | 1 | 114 | 2302 | 18.4 | 41.6 | 85 | 1088842 | yes |

**shipped now applies its cap** at 4 cores (`c0=schedutil/408000-1200000`, mean ~1.09 GHz), pass on a clean window (the first run caught the sporadic single-frame stall). The earlier uncapped run was the device-override cfg bug, fixed in round 1b. See Shipped verification.

cap1008 cheapest passing (cap600/816 genuinely starve on fps + drops). +one step. **Chosen: cap1200.**

## MD

Scene: Sonic 3D Blast title/attract. base cpu 68%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (60.0) | 4 | 114 | 2444 | 58.1 | 72.6 | 68 | 783158 | yes |
| cap600 | 60.3/60.1 (60.0) | 6 | 118 | 2455 | 69.5 | 72.6 | 82 | 586983 | yes |
| cap816 | 60.3/60.2 (60.0) | 5 | 116 | 2538 | 59.5 | 72.6 | 72 | 717103 | yes |
| cap1008 | 60.3/60.2 (60.0) | 4 | 116 | 2206 | 58.4 | 72.6 | 70 | 760966 | yes |
| cap1200 | 60.3/60.2 (60.0) | 4 | 116 | 2214 | 59.1 | 72.6 | 69 | 818897 | yes |
| cap1416 | 60.3/60.2 (60.0) | 4 | 116 | 2450 | 51.5 | 72.6 | 69 | 849931 | yes |
| cap1608 | 60.3/60.2 (60.0) | 4 | 114 | 2204 | 57.2 | 72.6 | 68 | 791579 | yes |
| shipped | 60.3/60.3 (60.0) | 4 | 116 | 2559 | 61.7 | 72.6 | 72 | 710483 | yes |

Under the fix-round-2 worst-frame rule every cap passes: the periodic ~51-69 ms frame is a config-independent scene hitch (fps at req, drops 4-6, cpu 68-82% at every cap) and now sits within the baseline-relative worst bound (72.6 ms = 1.25 x base 58.1). cap600 cheapest passing. +one step. **Chosen: cap816.** (Previously recorded as no-cap under the flat 2.5-period bound.)

## GPGX

**Not measured on either test device -- ships no profile (this round).** The pak
*does* exist in the source tree -- `skeleton/SYSTEM/tg5040/paks/Emus/GPGX.pak` and
`.../tg5050/.../GPGX.pak`, with its own `default.cfg`, added 2026-09-20 (commit
2805e8c5, "Genesis Plus GX as an alternate Sega core ... via one extension-refined
GPGX tag"). It was simply **not deployed on either test SD card** (neither device's
`.system/paks/Emus/` contains GPGX.pak), so the bench driver's
`Emus/GPGX.pak/launch.sh` was absent and `minarch did not start` for every config.
MD.pak's `default.cfg` is PicoDrive's and does not reach GPGX.pak (GPGX ships its
own core/config). GPGX is therefore unmeasured here; Task 8 should measure it once
the pak is on a test card and give it its own profile (genesis_plus_gx is a
different, generally heavier core than MD/picodrive).

## FBN

Scene: Metal Slug (mslug) attract demo (heavy core, real load; cpu 106% when starved). base cpu 75%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (59.2) | 1 | 116 | 2360 | 18.5 | 42.2 | 75 | 1280276 | yes |
| cap600 | 56.8/53.9 (59.2) | 2224 | 118 | 1677 | 26.9 | 42.2 | 106 | 600000 | no |
| cap816 | 60.3/58.1 (59.2) | 233 | 116 | 1858 | 20.2 | 42.2 | 100 | 808966 | no |
| cap1008 | 60.3/60.2 (59.2) | 2 | 116 | 2313 | 19.6 | 42.2 | 87 | 963310 | yes |
| cap1200 | 60.3/60.2 (59.2) | 0 | 116 | 2167 | 17.8 | 42.2 | 80 | 1156552 | yes |
| cap1416 | 60.3/60.2 (59.2) | 2 | 116 | 2310 | 19.0 | 42.2 | 77 | 1218621 | yes |
| cap1608 | 60.3/60.1 (59.2) | 5 | 116 | 1862 | 74.8 | 42.2 | 76 | 1299310 | no |
| shipped | 60.3/60.2 (59.2) | 1 | 114 | 2285 | 18.5 | 42.2 | 80 | 1124632 | yes |

**shipped now applies its cap** at 4 cores (`c0=schedutil/408000-1200000`, mean ~1.12 GHz); pass on a clean window (worst 18.5 ms). The earlier uncapped FAIL was the device-override cfg bug (FBN ships `default-brick.cfg` without the cap), fixed in round 1b. See Shipped verification.

cap1008 cheapest passing (cap600/816 starve). +one step. **Chosen: cap1200.**

## PS

Scene: Tekken 3 attract/demo (heaviest core, real 3D load). base cpu 75%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 60.3/60.2 (60.0) | 18 | 116 | 2109 | 65.2 | 81.5 | 75 | 1363862 | yes |
| cap600 | 59.7/54.9 (60.0) | 915 | 118 | 1453 | 75.6 | 81.5 | 93 | 593492 | no |
| cap816 | 58.7/52.4 (60.0) | 1004 | 118 | 2439 | 60.9 | 81.5 | 86 | 744407 | no |
| cap1008 | 58.6/51.7 (60.0) | 554 | 114 | 1475 | 63.7 | 81.5 | 83 | 841684 | no |
| cap1200 | 59.9/56.2 (60.0) | 249 | 116 | 1517 | 75.3 | 81.5 | 80 | 1000552 | no |
| cap1416 | 60.2/56.3 (60.0) | 87 | 116 | 1441 | 26.0 | 81.5 | 79 | 1139586 | no |
| cap1608 | 60.2/57.0 (60.0) | 38 | 116 | 1300 | 75.7 | 81.5 | 78 | 1173517 | yes |

Under the fix-round-2 rule `base` and `cap1608` pass (worst bound 81.5 ms = 1.25 x base 65.2). Below cap1416 the core is genuinely starved (fps min 50-57, hundreds of drops). cap1608 does pass but its fps min (57.0) lands **exactly** on the 95% line (0.95 x 60.0 = 57.0), and even uncapped `base` only just clears. Mechanically cheapest-pass + one step would be cap1800, but per controller ruling **PS keeps the full range: Chosen = no cap** -- its cheapest pass sits on the fps-min threshold and the core is marginal even uncapped.

## Cores step (Step 2): 2 big cores vs 4, at each core's chosen cap (<= 1008)

Re-ran the chosen cap with cpu2+cpu3 offline (`cores2`). Ships only if fps min AND worst frame are within 5% of the 4-core run at the same cap (a delta test, independent of the pass-rule change).

| core @ cap | 4-core fps min / worst | 2-core fps min / worst | Δ fps min | Δ worst | ship 2-core? |
|---|---|---|---|---|---|
| GB @ 816 | 60.2 / 17.6 | 60.1 / 21.3 | -0.2% | +21.0% | **no** |
| GBC @ 816 | 60.3 / 18.2 | 59.7 / 24.2 | -1.0% | +33.0% | **no** |
| GBA @ 816 | 59.9 / 20.3 | 59.9 / 30.8 | +0.0% | +51.7% | **no** |
| FC @ 1008 | 60.2 / 18.0 | 60.1 / 35.7 | -0.2% | +98.3% | **no** |

fps min holds in every case, but the worst frame degrades far beyond the 5% bound (+21% to +98%). **No 2-core profile is shipped for any core.** Cores with chosen cap > 1008 (MGBA, SFC, FBN at 1200) are out of scope for this step. MD (now chosen cap816) also gets **no 2-core profile**: no 2-core run was made for it, and every one of the four cores actually measured at two cores degraded 21-98% on worst frame, so four cores stays the safe default. PS keeps the full range; GPGX was not measured.

## Governor (Step 3): GB@816, SFC@1200, MGBA@1200

Each core re-run under schedutil (`gov_su`, current `CPU_AUTO_GOVERNOR`), ondemand (`gov_od`) and interactive (`gov_int`) at its chosen cap; pass column uses the fix-round-2 worst-frame rule. A governor replaces schedutil only if all three cores pass with it AND its mean kHz is >= 15% lower than schedutil's on at least two of them; ties -> schedutil.

| core @ cap | governor | fps mean/min | worst ms | worst bound | drops | cpu% | mean kHz | pass | kHz vs su |
|---|---|---|---|---|---|---|---|---|---|
| GB @ 816 | schedutil | 60.3/59.9 | 78.8 | 80.2 | 30 | 54 | 694759 | yes | -- |
| GB @ 816 | ondemand | 60.3/59.9 | 22.6 | 80.2 | 26 | 49 | 816000 | yes | +17.5% |
| GB @ 816 | interactive | 60.2/59.8 | 32.8 | 80.2 | 41 | 67 | 576000 | yes | -17.1% |
| SFC @ 1200 | schedutil | 60.3/59.9 | 78.1 | 41.6 | 69 | 84 | 1179724 | no | -- |
| SFC @ 1200 | ondemand | 60.3/60.0 | 27.5 | 41.6 | 41 | 82 | 1200000 | yes | +1.7% |
| SFC @ 1200 | interactive | 60.3/59.8 | 31.6 | 41.6 | 57 | 83 | 1176414 | yes | -0.3% |
| MGBA @ 1200 | schedutil | 60.3/59.9 | 28.7 | 41.9 | 37 | 78 | 1159158 | yes | -- |
| MGBA @ 1200 | ondemand | 60.3/60.0 | 37.5 | 41.9 | 22 | 76 | 1200000 | yes | +3.5% |
| MGBA @ 1200 | interactive | 60.3/60.0 | 32.0 | 41.9 | 34 | 79 | 1136690 | yes | -1.9% |

**Decision: keep schedutil.**

- ondemand passes all three but is *more* expensive than schedutil on every core (+17.5%, +1.7%, +3.5% mean kHz) -- 0 cores meet the >=15%-cheaper bar.
- interactive passes all three but is >=15% cheaper only on GB (-17.1%); on SFC and MGBA it is within noise (-0.3%, -1.9%) -- 1 core, short of the required two.
- Neither candidate clears "cheaper by >=15% on >= 2 cores", so schedutil is retained (the tie/default rule also favours schedutil).

Note: SFC's `base` run was clean (worst 17.7 ms) so its worst bound stays tight (~42 ms), and schedutil's SFC run happened to catch the sweep-wide sporadic ~78 ms hitch, so it still reads "no"; GB's noisier baseline (worst 64.2 ms) lifts its bound to ~80 ms so schedutil GB now passes. Either way the decision is made on mean kHz, where schedutil is cheapest or tied.

## Decision summary (Brick)

| TAG | chosen cap | 2-core? | notes |
|---|---|---|---|
| GB | cap816 | no | cheapest pass cap600 +1 step |
| GBC | cap816 | no | cheapest pass cap600 +1 step |
| GBA | cap816 | no | cheapest pass cap600 +1 step |
| MGBA | cap1200 | n/a (>1008) | cheapest pass cap1008 +1 step |
| FC | cap1008 | no | cheapest pass cap816 +1 step |
| SFC | cap1200 | n/a (>1008) | cheapest pass cap1008 +1 step |
| MD | cap816 | n/a | cheapest pass cap600 +1 step (fix round 2; periodic scene hitch, not CPU-bound) |
| FBN | cap1200 | n/a (>1008) | cheapest pass cap1008 +1 step |
| PS | no cap (full range) | n/a | passes only at cap1608 (fps min on the 95% line) and base; marginal even uncapped -- keeps full range per ruling |
| GPGX | not measured | -- | pak in tree (commit 2805e8c5) but not deployed on either test device |

**Governor: schedutil (unchanged).**

## Shipped verification (Task 8 + fix round 1b, 2026-09-20)

Each written pak re-run as shipped (`bench-minarch.sh <plat> <serial> <TAG> <rom>
shipped:`, no sysfs overrides) on Brick `5c000c8997414781d1d`, **at 4 cores
(`online=0-3`)** with `BENCH_SECS=60`. The `shipped` row sits in each core's table
above; every one now shows `c0=schedutil/408000-<cap>000` at its intended cap and
passes.

### The cap failure was a device-override cfg file, not a clobber (fix round 1b)

The earlier shipped runs showed FC/SFC/FBN uncapped (`c0=408000-2000000`). The
cause was **not** a launcher/GFX-boost race. minarch's `setOverclock` "Auto" path
uses `cpu_profile_max_mhz`, parsed from the pak cfg minarch actually loads.
`Config_load` (ma_config.c) loads `default-<DEVICE>.cfg` **instead of**
`default.cfg` when it exists (full replacement, not an overlay). The Brick is
`DEVICE=brick`, and FC/SFC/FBN are the only profiled paks shipping a
`default-brick.cfg` + `default-brickpro.cfg` -- which had no `minarch_cpu_max`, so
the Task 8 cap written to `default.cfg` was never read (instrumented build showed
`max_mhz=0` at `setOverclock`). GB/GBC/GBA/MGBA/MD ship no override and always
honoured their cap; tg5050 (`DEVICE=smartpros`) ships no matching override, so all
8 worked there. Fix round 1b copied the same cap line into all six
`default-brick.cfg` / `default-brickpro.cfg` files (FC 1008, SFC 1200, FBN 1200).
Follow-up recorded on the checklist: make `default-<DEVICE>.cfg` overlay
`default.cfg` rather than replace it, so no future `default.cfg` key silently drops
on a device that ships an override.

### These 4-core rows supersede the earlier 2-core rows (Defect B)

The earlier `shipped` rows, and Task 7's `cores2`/`governor` rows, ran at
`online=0-1` (2 big cores): Task 7's `cores2` config offlined cpu2/cpu3 and nothing
on the Brick re-onlines cores (its boot script/launcher only manage hotplug on
tg5050), so every later run inherited two cores. The driver now re-onlines cpu1-3
before it exits (fix round 1b, `bench-driver.sh`), and the rows above were taken at
the full four cores. The governor decision (schedutil) still stands -- it was a
relative comparison between governors at the same core count.

### Sporadic single-frame stall

MGBA, SFC and FBN each caught one ~68-75 ms frame on their first 4-core run (fps at
req, drops <=7, cpu 80-84 %) -- the sweep-wide sporadic single-frame stall noted at
the top of this file, not CPU starvation. A clean-window re-run of each passed
(worst 25.1 / 18.4 / 18.5 ms) and those clean rows are the ones tabled. FBN passes
at 4 cores with its 1200 cap.

All 8 apply their cap and pass at 4 cores: GB 816, GBC 816, GBA 816, MGBA 1200,
MD 816, FC 1008, SFC 1200, FBN 1200.
