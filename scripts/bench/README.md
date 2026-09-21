# minarch CPU sweep harness

Measures how a minarch core performs under different CPU configurations so a
per-core CPU preset can be chosen. minarch, when launched with `NX_BENCH=1` in
its environment, prints one `[bench]` line per second:

```
[bench] t=<s> fps=<f> req=<f> avg_ms=<f> max_ms=<f> drops=<d> buf_free=<d> buf_target=<d> cpu=<d> khz0=<d> khz4=<d>
```

`khz0`/`khz4` are the current frequency of the cpu0 and cpu4 cpufreq policies
(0 when the policy is absent — e.g. cpu4 offline on tg5050).

While a config is being measured the driver also emits, every 100 ms, one
thread-residency line naming each of minarch's tasks and the cpu it is on:

```
[thr] t=<uptime> <tid>:<comm>:<cpu> <tid>:<comm>:<cpu> ...
```

`comm` is `/proc/<pid>/task/<tid>/comm`; `<cpu>` is field 39 of
`.../task/<tid>/stat` (the cpu the thread last ran on). These feed the summary's
per-thread residency and migration tables (below); they interleave with the
`[freq]` lines and are teed into the same log. A token with an empty cpu (a
thread that vanished mid-sample) is ignored by the summary.

## The three scripts

- **`bench-driver.sh`** (runs on the device, busybox sh) —
  `bench-driver.sh <plat> <TAG> <rom> <config-id> <seconds> [cmd ...]`.
  Launches the emulator the way the launcher does (writes the pak dir into
  `/tmp/nextui_open`; the pak dir is a generated `/tmp/bench.pak/launch.sh` that
  exports `NX_BENCH=1` and execs the real `Emus/<TAG>.pak/launch.sh` with the
  ROM). It waits for `minarch.elf`, sleeps 5 s so minarch applies its own
  preset, runs each sysfs `cmd`, prints a `[driver] cfg=...` line, starts the
  100 ms `[thr]` thread sampler, samples `[freq]` lines for `<seconds>` seconds,
  then dumps the new `[bench]` lines from the emulator log (killing the sampler
  first). If the env var `BENCH_PRELAUNCH` is set, its value is `eval`ed **before**
  the pak is requested — the hook for herding background daemons off a cluster
  before the emulator starts, e.g. `taskset -p 0x3 $(pidof keymon.elf) || true` on
  the Brick. It quits the game through minarch's own in-game menu
  (MENU, Down x4, A) — **never SIGTERM**, which SDL turns into a power-off — with
  `kill -9` only as a fallback, then removes `/tmp/bench.pak` and prints
  `[driver] done`.

- **`bench-minarch.sh`** (runs on the host) —
  `bench-minarch.sh <plat> <serial> <TAG> "<rom path on device>" <config-spec>...`.
  Pushes the driver, runs each config-spec in turn, tees each run to
  `results/<plat>/<TAG>-<id>.log`, then calls the summary. `BENCH_SECS`
  (default 60) sets the sample window. `BENCH_PRELAUNCH`, when set, is exported
  into the device command so the driver runs it before requesting the pak.

- **`bench-summary.py`** (runs on the host) —
  `bench-summary.py <plat> <TAG>` reads `results/<plat>/<TAG>-*.log`, prints a
  markdown pass table and the cheapest passing config. The pass table carries a
  `stalls/min` column (count of `[bench]` rows in the analysed window whose worst
  frame exceeded 60 ms, scaled to a 60 s minute). After it, each run whose log has
  `[thr]` samples gets a second table — `thread | residency % per cpu | migrations`
  — with one row per `comm`: the share of that comm's samples spent on each cpu,
  the number of tids it covers, and the migration count (consecutive-sample cpu
  changes, summed over its tids). Older logs without `[thr]` samples are skipped.

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

## CPU affinity comparison

The Task 2 `minarch_cpu_affinity = big|little|none` key (in a pak's `default.cfg`)
pins minarch's emulation/main thread to a cluster. To compare a pak as shipped
against a `big`-pinned variant, edit the **on-device** pak data (no sysfs `cmd`s —
the variants are pak data, so both runs use an empty command list) and diff the
`[thr]` tables:

1. Back up the on-device pak's `default.cfg` and `launch.sh`
   (`/mnt/SDCARD/.system/paks/Emus/<TAG>.pak/`).
2. Shipped run — leave the pak untouched and run one config, e.g. `none:`.
3. `big` run — append `minarch_cpu_affinity = big` to the on-device `default.cfg`
   and, on tg5050, delete the `echo 0 > .../cpu4/online` line from the on-device
   `launch.sh` (otherwise the big cluster the key pins to is offline); push both
   back, then run `big:`.
4. Restore the backed-up files.

```bash
BENCH_SECS=20 scripts/bench/bench-minarch.sh tg5050 7057408880c2c8c239a SFC \
  "/mnt/SDCARD/Roms/Super Nintendo ES (SFC)/Super Mario World 2 - Yoshi's Island.sfc" \
  none:   # then, after editing the on-device pak, run big:
```

The `big` run's thread table should show the main thread (`comm minarch.elf`, the
tid equal to the pid) at ~100 % on the big core (cpu4 on tg5050) with the render
(`PrepareFrameThr`), audio (`SDLAudioP2`) and `mali-*` helpers on cpu0-1; the
shipped/`none` run shows every thread on cpu0-1 — but only because the untouched
pak keeps cpu4 offline, so to compare placement rather than core count delete the
`cpu4/online` line for the `none` arm too and keep cpu4 online for both, as the
real sweep did. (Idle threads that were never
scheduled during a run report whatever cpu they last ran on, so a dormant worker
pool may show 100 % on a single cpu — read residency for the busy threads.)

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
