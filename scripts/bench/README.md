# minarch CPU sweep harness

Measures how a minarch core performs under different CPU configurations so a
per-core CPU preset can be chosen. minarch, when launched with `NX_BENCH=1` in
its environment, prints one `[bench]` line per second:

```
[bench] t=<s> fps=<f> req=<f> avg_ms=<f> max_ms=<f> drops=<d> buf_free=<d> buf_target=<d> cpu=<d> khz0=<d> khz4=<d>
```

`khz0`/`khz4` are the current frequency of the cpu0 and cpu4 cpufreq policies
(0 when the policy is absent — e.g. cpu4 offline on tg5050).

## The three scripts

- **`bench-driver.sh`** (runs on the device, busybox sh) —
  `bench-driver.sh <plat> <TAG> <rom> <config-id> <seconds> [cmd ...]`.
  Launches the emulator the way the launcher does (writes the pak dir into
  `/tmp/nextui_open`; the pak dir is a generated `/tmp/bench.pak/launch.sh` that
  exports `NX_BENCH=1` and execs the real `Emus/<TAG>.pak/launch.sh` with the
  ROM). It waits for `minarch.elf`, sleeps 5 s so minarch applies its own
  preset, runs each sysfs `cmd`, prints a `[driver] cfg=...` line, samples
  `[freq]` lines for `<seconds>` seconds, then dumps the new `[bench]` lines
  from the emulator log. It quits the game through minarch's own in-game menu
  (MENU, Down x4, A) — **never SIGTERM**, which SDL turns into a power-off — with
  `kill -9` only as a fallback, then removes `/tmp/bench.pak` and prints
  `[driver] done`.

- **`bench-minarch.sh`** (runs on the host) —
  `bench-minarch.sh <plat> <serial> <TAG> "<rom path on device>" <config-spec>...`.
  Pushes the driver, runs each config-spec in turn, tees each run to
  `results/<plat>/<TAG>-<id>.log`, then calls the summary. `BENCH_SECS`
  (default 60) sets the sample window.

- **`bench-summary.py`** (runs on the host) —
  `bench-summary.py <plat> <TAG>` reads `results/<plat>/<TAG>-*.log`, prints a
  markdown pass table and the cheapest passing config.

## config-spec syntax

One argument per configuration: `id:cmd1;cmd2;...`. The `id` names the run (and
its log file); the semicolon-separated commands are sysfs writes applied on the
device after minarch has settled. `base:` (empty command list) measures the
core under minarch's own preset with no override. Examples:

```
cap600:"echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
little:"echo 0 > /sys/devices/system/cpu/cpu4/online"
little672:"echo 0 > /sys/devices/system/cpu/cpu4/online;echo 672000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
```

## Command lines (Task 7 sweep)

TrimUI Brick (tg5040, serial `5c000c8997414781d1d`) — cap cpu0's big cores:

```bash
scripts/bench/bench-minarch.sh tg5040 5c000c8997414781d1d GB \
  "/mnt/SDCARD/Roms/001)Game Boy (GB)/Pokemon - Red.gb" \
  base: \
  cap600:"echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq" \
  cap408:"echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"

scripts/bench/bench-minarch.sh tg5040 5c000c8997414781d1d SFC \
  "/mnt/SDCARD/Roms/Super Nintendo ES (SFC)/Super Mario World 2 - Yoshi's Island.sfc" \
  base: \
  cap1008:"echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq" \
  cap600:"echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
```

Smart Pro S (tg5050, serial `7057408880c2c8c239a`) — take the big cluster
(cpu4) offline; the ROM dir is `Game Boy (GB)`:

```bash
scripts/bench/bench-minarch.sh tg5050 7057408880c2c8c239a GB \
  "/mnt/SDCARD/Roms/Game Boy (GB)/Pokemon - Red.gb" \
  base: \
  little:"echo 0 > /sys/devices/system/cpu/cpu4/online" \
  little672:"echo 0 > /sys/devices/system/cpu/cpu4/online;echo 672000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"

scripts/bench/bench-minarch.sh tg5050 7057408880c2c8c239a SFC \
  "/mnt/SDCARD/Roms/Super Nintendo ES (SFC)/Super Mario World 2 - Yoshi's Island.sfc" \
  base: \
  little:"echo 0 > /sys/devices/system/cpu/cpu4/online" \
  little672:"echo 0 > /sys/devices/system/cpu/cpu4/online;echo 672000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
```

Set `BENCH_SECS=20` for a quick validation pass; leave it at the default (60)
for the real sweep. Run the ROM directory paths in double quotes — they contain
spaces, parentheses, and an apostrophe.

## Pass rule (spec §4.4, fix round 2)

A config passes when, over the sample window (first 3 samples after the config
change are dropped):

- fps mean >= 99% of req,
- fps min >= 95% of req,
- `drops <= max(2 * n, 1.5 * base_drops)` — a baseline-relative bound, where `n`
  is the number of analysed rows (seconds) for the config and `base_drops` is
  the drops of the same TAG's `base` run (`<TAG>-base.log`). If no base log
  exists the bound is `2 * n` alone and a warning is printed,
- buf_free is never 0,
- `worst frame <= max(2.5 * period, 1.25 * base_worst)` — a baseline-relative
  bound (shown in the summary's `worst bound` column), where `period` is
  `1000/req` ms and `base_worst` is the worst frame of the same TAG's `base` run.
  If no base log exists the bound is `2.5 * period` alone and a warning is
  printed.

Why the drops clause is baseline-relative, not `drops == 0`: minarch's
`frame_drops` counts any frame that overran 1.1x the display period, jitter
included. A perfectly healthy run at full fps still logs roughly 20-30 such
"drops" over a 20 s window (the game runs a hair fast, so the output resyncs);
a run that genuinely cannot keep up logs 200-800 drops with cpu pinned near
100 %. A flat `drops == 0` would fail every healthy light core, so the bound is
tied to the unconstrained `base` run's drop count instead.

Why the worst-frame clause is baseline-relative too (fix round 2): some cores
carry a periodic scene/display hitch — a single long frame of ~48-69 ms that
recurs at **every** cap including the uncapped `base` run, while fps stays at
target and cpu sits at only 60-80 %. That is a scene artifact (a ~63 fps vs
~60 Hz resync frame, or a background-daemon stall), not CPU starvation, and a
flat 2.5-period bound (~42 ms at 60 Hz) would wrongly fail such a core at every
frequency. Anchoring the worst-frame bound to the same TAG's `base` run
(`1.25 * base_worst`, but never tighter than `2.5 * period`) lets a genuinely
starved run — which shows a *higher* worst frame *and* lower fps / more drops —
still fail, while a core whose baseline already carries the hitch is judged
against its own clean-frequency behaviour. Measured on the Brick (MD, PS) and the
Smart Pro S (MD): 48-69 ms worst frame at every cap, fps at req, cpu 60-80 %.

The cheapest passing config is the one with the lowest mean kHz of the cluster
that was actually running. cpu4 (the big cluster) is only counted when the run's
`[driver] cfg=... online=<mask>` line includes core 4 and the config id is not a
`little`/`A`-cluster config; otherwise khz4 is treated as 0. This is because an
offline cpu4 keeps reporting its parked minimum frequency through
`cpu4/cpufreq/scaling_cur_freq` (it does not drop to 0), so a `little` run would
otherwise be mis-attributed to the big cluster.

## Device state

The driver restores CPU hotplug before it exits: on tg5040 (the Brick) it
re-onlines cpu1-3 after the run, because that platform has no hotplug restore
path of its own (only tg5050's boot script/launcher re-online their offlined
core), so a `cores2` config must not leave the device stuck on two cores.

## Results

`results/<plat>/<TAG>-<id>.log` holds the raw driver output per run. The logs
are git-ignored (`scripts/bench/results/**/*.log`); only `.gitkeep` is tracked.
