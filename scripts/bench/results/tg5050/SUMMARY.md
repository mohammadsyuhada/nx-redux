# Smart Pro S (tg5050) -- minarch CPU profile sweep

Serial `7057408880c2c8c239a`. `BENCH_SECS=60`, run 2026-09-20. Pass rule (fix round
2): fps mean >= 99% req, fps min >= 95% req, buf_free never 0, drops <= max(2*n,
1.5*base_drops), and **worst frame <= max(2.5*period, 1.25*base_worst)**
(baseline-relative; see the `worst bound` column).

**Topology choice (controller ruling 2):** topology A (cpu4 / big cluster offline)
**wins whenever any A config passes**, regardless of the summary's cheapest-by-mean-kHz
line. Chosen A cap = cheapest passing A cap + one little-cluster step
(table: 408 672 792 936 1032 1128 1224 1320 1416; A1416 = uncapped little cluster).
Only if no A config passes is topology B run. `base` = launcher default topology
(online 0-1,4: two little cores + big core, schedutil).

Note: this device runs every core a few percent fast (~63 fps against a ~60 Hz
panel); fps mean > req is normal and the periodic resync frame is the source of the
worst-frame values. All measured scenes are the game's title/attract mode and were
CPU-loaded (base cpu 49-71%), so no scene was idle and none needed substituting.
Fix-round-2 note: MD now passes under the baseline-relative worst-frame rule
(chosen A792); it previously recorded no-pass because its periodic ~48 ms scene
hitch tripped the flat 2.5-period bound at every cap and even under topology B.

PS and GPGX are skipped on this device (no ROMs present, per the brief; GPGX.pak
also not deployed here).

## GB

Scene: Pokemon - Red title/attract. base cpu 49% (light core).

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 63.0/62.4 (59.7) | 80 | 120 | 2549 | 21.6 | 41.9 | 49 | 517241 | yes |
| A672 | 63.0/62.8 (59.7) | 18 | 120 | 2264 | 20.5 | 41.9 | 48 | 466169 | yes |
| A936 | 63.0/62.7 (59.7) | 11 | 120 | 2684 | 19.1 | 41.9 | 47 | 507310 | yes |
| A1032 | 63.0/62.9 (59.7) | 11 | 120 | 2690 | 18.1 | 41.9 | 47 | 510102 | yes |
| A1224 | 63.0/62.7 (59.7) | 13 | 120 | 2501 | 19.6 | 41.9 | 46 | 459724 | yes |
| A1416 | 63.0/62.9 (59.7) | 18 | 120 | 2680 | 18.4 | 41.9 | 46 | 477103 | yes |
| shipped | 63.0/62.8 (59.7) | 13 | 120 | 2675 | 18.7 | 41.9 | 48 | 477966 | yes |
| shipped (fix 1b re-check) | 63.0/62.7 (59.7) | 12 | 120 | 2620 | 21.6 | 41.9 | 48 | 493424 | yes |

Topology A (A672 passes). Cheapest passing A cap = 672, +one step. **Chosen: A792** (topology A, little cluster capped 792 MHz, cpu4 offline).

## GBC

Scene: Pokemon Pinball title/attract. base cpu 53% (light core).

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 63.0/62.7 (59.7) | 74 | 118 | 2603 | 27.0 | 41.9 | 53 | 525966 | yes |
| A672 | 63.0/62.9 (59.7) | 12 | 118 | 2636 | 18.4 | 41.9 | 52 | 484068 | yes |
| A936 | 63.0/62.7 (59.7) | 17 | 116 | 2683 | 19.3 | 41.9 | 51 | 492000 | yes |
| A1032 | 63.0/62.9 (59.7) | 11 | 116 | 2510 | 18.3 | 41.9 | 51 | 452690 | yes |
| A1224 | 63.0/62.8 (59.7) | 9 | 118 | 2446 | 18.6 | 41.9 | 51 | 531661 | yes |
| A1416 | 63.0/62.9 (59.7) | 12 | 116 | 2788 | 18.1 | 41.9 | 50 | 487034 | yes |
| shipped | 63.0/62.8 (59.7) | 11 | 118 | 2295 | 19.0 | 41.9 | 52 | 486915 | yes |

Topology A (A672 passes). **Chosen: A792**.

## GBA

Scene: The Legend of Zelda - The Minish Cap title/attract. base cpu 61%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 63.0/62.1 (59.7) | 87 | 130 | 2361 | 20.6 | 41.9 | 61 | 512542 | yes |
| A672 | 63.0/62.6 (59.7) | 32 | 130 | 2368 | 23.3 | 41.9 | 63 | 519864 | yes |
| A936 | 63.0/62.7 (59.7) | 9 | 130 | 2460 | 19.6 | 41.9 | 62 | 536542 | yes |
| A1032 | 63.0/62.9 (59.7) | 18 | 130 | 2556 | 18.4 | 41.9 | 61 | 521898 | yes |
| A1224 | 63.0/62.8 (59.7) | 17 | 130 | 2574 | 19.2 | 41.9 | 61 | 604138 | yes |
| A1416 | 63.0/62.9 (59.7) | 10 | 130 | 2507 | 20.2 | 41.9 | 61 | 523034 | yes |
| shipped | 63.0/62.9 (59.7) | 21 | 130 | 2494 | 22.6 | 41.9 | 63 | 506847 | yes |

Topology A (A672 passes). **Chosen: A792**.

## MGBA

Scene: Iridion II title/attract (heavy core, real load). base cpu 71%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 62.1/59.6 (59.7) | 695 | 1042 | 543 | 25.4 | 41.9 | 71 | 1026712 | yes |
| A672 | 58.7/54.1 (59.7) | 2018 | 1042 | 2179 | 37.7 | 41.9 | 90 | 658576 | no |
| A936 | 63.1/59.6 (59.7) | 234 | 1042 | 1938 | 29.2 | 41.9 | 80 | 895034 | yes |
| A1032 | 62.9/61.4 (59.7) | 125 | 1042 | 1890 | 26.0 | 41.9 | 77 | 943448 | yes |
| A1224 | 63.0/62.7 (59.7) | 49 | 1042 | 2812 | 24.9 | 41.9 | 75 | 998897 | yes |
| A1416 | 63.0/62.8 (59.7) | 31 | 1042 | 2731 | 24.2 | 41.9 | 74 | 1028690 | yes |
| shipped | 63.0/61.9 (59.7) | 126 | 1042 | 1716 | 31.6 | 41.9 | 78 | 950069 | yes |

Topology A (A936 cheapest passing; A672 fails fps min + drops). +one step. **Chosen: A1032**.

## FC

Scene: Mega Man 2 title/attract. base cpu 70%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 62.8/60.8 (60.1) | 132 | 198 | 2694 | 28.0 | 41.6 | 70 | 969517 | yes |
| A672 | 60.8/57.3 (60.1) | 731 | 198 | 2015 | 43.9 | 41.6 | 90 | 649627 | no |
| A936 | 63.0/62.6 (60.1) | 54 | 198 | 2585 | 22.4 | 41.6 | 75 | 857898 | yes |
| A1032 | 63.0/62.4 (60.1) | 46 | 198 | 2516 | 23.0 | 41.6 | 74 | 884746 | yes |
| A1224 | 63.0/62.7 (60.1) | 35 | 198 | 2558 | 19.5 | 41.6 | 73 | 907034 | yes |
| A1416 | 63.0/62.8 (60.1) | 16 | 198 | 2715 | 23.9 | 41.6 | 72 | 910345 | yes |
| shipped | 63.0/62.7 (60.1) | 50 | 198 | 2768 | 24.3 | 41.6 | 74 | 880271 | yes |

Topology A (A936 cheapest passing; A672 fails fps min + worst frame). **Chosen: A1032**.

## SFC

Scene: Super Mario World 2 - Yoshi's Island title/attract (heavy core, real load). base cpu 71%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 62.9/61.7 (60.1) | 130 | 195 | 1284 | 26.8 | 41.6 | 71 | 927458 | yes |
| A672 | 59.8/55.7 (60.1) | 1431 | 195 | 2572 | 40.5 | 41.6 | 89 | 663051 | no |
| A936 | 63.0/61.9 (60.1) | 95 | 195 | 1898 | 23.6 | 41.6 | 77 | 864000 | yes |
| A1032 | 63.0/62.6 (60.1) | 65 | 195 | 2646 | 23.7 | 41.6 | 75 | 905085 | yes |
| A1224 | 63.0/62.6 (60.1) | 32 | 195 | 2729 | 24.8 | 41.6 | 74 | 913655 | yes |
| A1416 | 63.0/62.7 (60.1) | 27 | 195 | 2723 | 21.6 | 41.6 | 73 | 923172 | yes |
| shipped | 63.0/62.6 (60.1) | 48 | 195 | 2706 | 25.9 | 41.6 | 75 | 904966 | yes |

Topology A (A936 cheapest passing; A672 fails fps min + drops). **Chosen: A1032**.

## MD

Scene: Sonic 3D Blast title/attract. base cpu 60%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 63.0/62.7 (60.0) | 87 | 130 | 3003 | 48.0 | 60.0 | 60 | 583862 | yes |
| A672 | 63.0/62.8 (60.0) | 29 | 130 | 1811 | 47.7 | 60.0 | 63 | 555661 | yes |
| A936 | 63.0/62.8 (60.0) | 16 | 130 | 2446 | 47.6 | 60.0 | 62 | 568678 | yes |
| A1032 | 63.0/62.8 (60.0) | 22 | 130 | 2341 | 47.9 | 60.0 | 61 | 657356 | yes |
| A1224 | 63.0/62.9 (60.0) | 19 | 130 | 1890 | 48.4 | 60.0 | 61 | 576414 | yes |
| A1416 | 63.0/62.9 (60.0) | 18 | 130 | 2109 | 47.2 | 60.0 | 61 | 593793 | yes |
| B1008 | 63.0/62.8 (60.0) | 39 | 130 | 2075 | 47.9 | 60.0 | 59 | 593793 | yes |
| B1680 | 63.0/62.6 (60.0) | 70 | 130 | 2099 | 53.7 | 60.0 | 59 | 564000 | yes |
| shipped | 63.0/62.8 (60.0) | 19 | 130 | 2347 | 49.5 | 60.0 | 62 | 589831 | yes |

Under the fix-round-2 worst-frame rule every config passes (worst bound 60.0 ms = 1.25 x base 48.0): the periodic ~48 ms frame is a config-independent scene hitch (fps at req, cpu ~60%), present in topology A, topology B and base alike. Topology A wins (A configs pass). Cheapest passing A cap = 672, +one step. **Chosen: A792.** (Previously recorded as no-cap under the flat 2.5-period bound; the topology-B rows B1008/B1680 were kept for the record.)

## FBN

Scene: Metal Slug (mslug) attract demo (heavy core, real load). base cpu 70%.

| config | fps mean/min (req) | drops | drop bound | buf_free min | worst ms | worst bound | cpu% | mean kHz | pass |
|---|---|---|---|---|---|---|---|---|---|
| base | 63.0/62.6 (59.2) | 52 | 118 | 2425 | 27.1 | 42.2 | 70 | 814780 | yes |
| A672 | 62.7/57.3 (59.2) | 400 | 118 | 1619 | 28.0 | 42.2 | 84 | 658576 | no |
| A936 | 63.0/62.6 (59.2) | 33 | 118 | 2736 | 23.0 | 42.2 | 73 | 784678 | yes |
| A1032 | 63.0/62.7 (59.2) | 44 | 118 | 2687 | 21.3 | 42.2 | 73 | 835525 | yes |
| A1224 | 63.0/62.8 (59.2) | 25 | 118 | 2586 | 18.9 | 42.2 | 72 | 845288 | yes |
| A1416 | 63.0/62.7 (59.2) | 38 | 116 | 2604 | 22.7 | 42.2 | 72 | 818897 | yes |
| shipped | 63.0/62.7 (59.2) | 29 | 118 | 2486 | 20.0 | 42.2 | 73 | 796475 | yes |

Topology A (A936 cheapest passing; A672 fails fps min + drops). **Chosen: A1032**.

## Decision summary (Smart Pro S)

| TAG | chosen |
|---|---|
| GB | A792 (topology A, little cap 792 MHz, big core offline) |
| GBC | A792 |
| GBA | A792 |
| MGBA | A1032 |
| FC | A1032 |
| SFC | A1032 |
| MD | A792 (topology A; fix round 2 -- periodic scene hitch now within baseline-relative worst bound) |
| FBN | A1032 |
| PS | skipped (no ROM on this device) |
| GPGX | skipped (no ROM / pak not deployed on this device) |

## Shipped verification (Task 8, 2026-09-20)

Each written pak re-run once as shipped (topology-A `launch.sh` cpu4-offline +
`default.cfg` cap, no sysfs overrides) on Smart Pro S `7057408880c2c8c239a`; the
`shipped` row sits in each core's table above. All 8 cores applied their
little-cluster cap and passed: `[driver] cfg=shipped online=0-1
c0=schedutil/408000-<cap>000 c4=schedutil/408000-408000` (cpu4 parked offline),
`[driver] done online=0-1` (launcher keeps the big core offline). Caps confirmed in
the cfg line: GB/GBC/GBA/MD 792, MGBA/FC/SFC/FBN 1032. GBA needed a re-run -- its
first two launches produced no bench frames (transient), the third was clean (cap
792, pass). No cap was clobbered on this device.

Fix round 1b re-check (2026-09-20): GB re-run once more as shipped with the
minarch rebuilt for fix round 1 (no functional change on tg5050 -- it ships no
`default-<DEVICE>.cfg` override, `DEVICE=smartpros`, so `default.cfg` is always the
loaded cfg). `online=0-1`, `c0=schedutil/408000-792000 c4=schedutil/408000-408000`,
pass -- confirming the tg5040-side fix did not regress this platform.
