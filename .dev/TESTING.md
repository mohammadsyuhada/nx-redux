# On-Device Testing & Debugging

adb deploy rules, headless input injection, screenshots, and profiling.
Hardware facts backing these recipes are in [DEVICES.md](DEVICES.md).

## Deploying binaries over adb

The SD card filesystems have **no ETXTBSY protection** — `adb push` over a
running elf corrupts the mapped executable in memory. The running process is
doomed either way; the rules are about recovering cleanly:

- **`nextui.elf` / `minarch.elf`** (always-running core binaries): push over
  them is fine, **but always `adb reboot` afterward**. Never try to
  hot-restart instead:
  - `killall nextui.elf` (SIGTERM) is ignored.
  - `killall -9 nextui.elf` with no `/tmp/next` queued makes the launch loop
    read "launcher exited without a next command" as shutdown — **the device
    powers itself off** and won't reappear on adb until physically powered on.
- **Any other pak elf**: no reboot needed. Ensure the pak isn't running
  (`ps w | grep <elf>`), push, relaunch from the menu.
- OSD tree files have their own rule — see "OSD files" below.

**Verify every deploy** — a push can "succeed" to the wrong place:

- The launcher PATH is `/mnt/SDCARD/.system/bin` first; the platform dir
  `.system/<plat>/bin` may exist on old cards but not be what runs. Confirm
  the real target with `readlink /proc/$(pidof nextui.elf)/exe` **before** the
  first push, and md5sum the pushed file against the local build.
- Tools paks live at `/mnt/SDCARD/.system/paks/Tools/<Name>.pak/` (flattened
  layout); stale `.system/<plat>/paks/` dirs on a card are not read.
- `adb push localdir remotedir` **nests** (`remotedir/localdir/...`) when
  remotedir exists, instead of merging — a redeploy silently leaves the stale
  tree in place. Either `rm -rf` the remote dir first or push contents with a
  glob (`adb push localdir/* remotedir/`). Verify with md5 of a changed file,
  never just exit status.

### OSD files

When only `skeleton/SYSTEM/osd/` content changed, skip the full build:
assemble with `./scripts/assemble-osd.sh <dev> <plat> <WxH> <dest>` and push
the assembled `osd/` (+ `osd-$DEVICE/` on tg5040) over
`/mnt/SDCARD/.system/`. Then **reboot** — the OSD tree is served through a
live read-only overlay mount, and pushing into its lowerdir serves **stale
content** (tg5040) or breaks with ESTALE (tg5050) until the next boot
re-mounts. See [OSD.md](OSD.md) for the mount mechanics.

### Editing `minuisettings.txt` over adb

nextui caches all settings in memory and `CFG_sync` rewrites the whole file on
clean shutdown — a `sed` edit followed by a normal reboot gets clobbered by
the stale cache. Either toggle via the on-device Settings app, or
`sed -i ... && sync && reboot -f` (force reboot skips the shutdown save).

## Launching apps headlessly (`/tmp/next` injection)

To launch any rom/pak exactly as the UI would, without menu navigation: write
the command to `/tmp/next` in nextui's own format —
`'<emu_pak>/launch.sh' '<rom_path>'` — then `kill -9 $(pidof nextui.elf)`.
The MinUI.pak launch loop evals `/tmp/next` on nextui exit and restarts nextui
when the app exits. Works on both platforms.

- Safe **only** with `/tmp/next` present (see the bare killall-9 power-off
  gotcha above).
- Injections written while an eval'd app is still running are consumed
  unexecuted by the loop's cleanup — only inject when `nextui.elf` itself has
  a pid.
- To end a session remotely, kill the app binary (not nextui) — wrapper
  cleanup runs and nextui returns on its own. Many apps ignore SIGTERM;
  `kill -9` the app pid is fine. An aborted shutdown test can leave
  `/tmp/poweroff` armed and `/tmp/nextui_exec` removed — restore both or the
  next app exit powers the device off.

## Input injection (headless UI driving)

Inject 24-byte `input_event` structs into the gamepad event node:

- **tg5040 gamepad = `/dev/input/event3`, tg5050 = `/dev/input/event4`**
  (`TRIMUI Player1`). Power button is `KEY_POWER` on event1 (tg5040) /
  event2 (tg5050) — both `axp2202-pek`-style PMIC keys.
- Codes: A=`BTN_EAST` 305, B=`BTN_SOUTH` 304, X=`BTN_WEST` 308,
  Y=`BTN_NORTH` 307, MENU=316, START=315, SELECT=314; d-pad =
  `EV_ABS` `ABS_HAT0X/Y` ±1 then 0; L2/R2 = EV_ABS codes 2/5 (value 255 for
  the screenshot combo). Physical X/Y vs event names are swapped per
  [INPUT_MAPPING.md](INPUT_MAPPING.md) — verify codes there before chaining
  blind presses.
- Busybox `printf` can't emit `\x` escapes — build the 24-byte events on the
  host (python `struct.pack('<QQHHi', 0, 0, type, code, value)`), push the
  `.bin` files, and `cat evt_down.bin > /dev/input/eventN; usleep 150000;
  cat evt_up.bin > ...`. **~150 ms between press and release** or the press is
  swallowed.
- Keep each adb shell command short (>~4 injected events per call hits
  "shell command too long"); wrap the sequence in a small on-device script.
- adbd kills its session's process group on disconnect and backgrounded
  children die with it (even setsid'd ones on session exit) — run
  start/verify/stop inside one adb shell. For a process that must survive a
  cable pull: tg5050 has `setsid`; tg5040 has neither `setsid` nor `nohup` —
  push a tiny dynamically-linked fork→setsid()→fork→execvp daemonizer
  (static glibc aborts on the 4.9 kernel), with stdio redirected or the adb
  shell hangs on the inherited pipe.

### The hybrid-sleep trap

With sleep-while-charging active the device re-enters hybrid sleep between
test steps (~30 s timeout), and fb0 keeps the stale frame so screenshots look
alive. Chain wake (`KEY_POWER`) + ~2 s + presses + screenshot in **one** adb
command; check for `SetRawBrightness(0)` at the tail of the app log to detect
sleep; poke a key every ~15 s during long waits.

## Screenshots for verification

- **tg5040**: fb0 is the real scanout — `dd if=/dev/fb0 of=/tmp/fb.raw bs=1048576 count=3`,
  pull, convert BGRA→PNG (1024×768 on Brick). Or on-device
  `ffmpeg -f fbdev -i /dev/fb0 -frames:v 1 -c:v mjpeg` (no PNG encoder on
  device). fb0 shows a **stale frame** until an input-driven redraw — inject
  one d-pad down/up first.
- **tg5050**: fb0 reads black (DRM scanout — see [DEVICES.md](DEVICES.md)).
  Use the screenshot daemon / DRM readback ([CAPTURE.md](CAPTURE.md)), or the
  GPU mirror `/tmp/fb_mirror.raw` (1280×720 RGBA, vflipped) while a capture
  daemon is armed.
- A 11003-byte JPEG at 1280×720 is the black-frame signature.
- App logs land at `/mnt/SDCARD/.userdata/<plat>/logs/<name>.txt` — a
  change-gated `LOG_info` at the suspect site plus this log is often faster
  than screenshots.

## CPU testing & profiling

**Trap first:** each pak's `launch.sh` sets CPU clocks on entry, and manual /
killed runs may leave caps behind (e.g. the Files pak caps big cores at
408 MHz). Check and reset `scaling_max_freq` before benchmarking. After a
normal app exit, `MinUI.pak/launch.sh` restores the default state
([DEVICES.md](DEVICES.md) has the tables).

```bash
# which cores are online
adb shell "for i in 0 1 2 3 4 5 6 7; do echo \"cpu\$i: \$(cat /sys/devices/system/cpu/cpu\$i/online 2>/dev/null)\"; done"

# freq/governor/min/max for a cluster (tg5050: cpu0=little, cpu4=big)
adb shell cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq

# sample frequency over time
for i in 1 2 3 4 5; do adb shell "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq" && sleep 1; done

# thread affinity of a process
adb shell "pid=\$(pidof PROCESS_NAME); for tid in \$(ls /proc/\$pid/task/); do \
  echo \"Thread \$tid: \$(grep Cpus_allowed_list /proc/\$pid/task/\$tid/status)\"; done"

# per-thread CPU usage: sample twice, compare utime deltas
adb shell "pid=\$(pidof PROCESS_NAME); for tid in \$(ls /proc/\$pid/task/); do \
  name=\$(cat /proc/\$pid/task/\$tid/comm); stat=\$(cat /proc/\$pid/task/\$tid/stat); \
  echo \"\$tid [\$name]: cpu=\$(echo \$stat | awk '{print \$39}') utime=\$(echo \$stat | awk '{print \$14}')\"; done"

# cores online/offline, governor, freq range — live
adb shell "echo 1 > /sys/devices/system/cpu/cpu5/online"
adb shell "echo performance > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor"
adb shell "echo 1584000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq"
```

The `performance` governor locks at `scaling_max_freq` (good for fixed-speed
tests); `schedutil` scales with load. GPU devfreq `cur_freq` stays at min
during menu rendering — useless as a first-frame/boot proxy.

## Boot timing

- `adb reboot` + immediate wait-for-device reads the **old** boot — wait for
  the disconnect first.
- nextui stamps boot phases to `/tmp/nextui_boottime` (start / gfx init /
  menu init / first frame).

## Misc

- `SIGCHLD=SIG_IGN` set anywhere in a process breaks `popen()`/`system()`
  exit statuses (ECHILD → -1) process-wide — the reason `runCommandAsync`
  uses double-fork + execvp instead. Symptom seen live: WiFi checks that
  shell out started failing permanently after the first async spawn.
- Standalone adb-shell runs of the vendored `keyboard` binary die instantly
  from a phantom controller event — test it through nextui (START on the main
  menu opens Search).
- Host-side script tests live in `scripts/tests/` (installer/catalog logic,
  PATH-shimmed) — run these before shipping shell changes; there is no C unit
  harness apart from per-feature host tests (e.g. `common/tests/`).
