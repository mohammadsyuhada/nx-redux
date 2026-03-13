# NextUI Development Guide

## Desktop Development Setup

### Prerequisites

Install dependencies via Homebrew:

```bash
brew install gcc sdl2 sdl2_image sdl2_ttf sqlite libsamplerate clang-format
```

### One-time Setup

#### 1. Create GCC symlinks

The build expects `gcc` to be Homebrew's GCC (not Apple Clang). This script symlinks Homebrew's GCC binaries into `/usr/local/bin/`:

```bash
sudo ./workspace/desktop/macos_create_gcc_symlinks.sh
```

Verify with:

```bash
gcc --version  # Should say "Homebrew GCC", not "Apple clang"
```

#### 2. Prepare fake SD card root

The desktop build uses `/var/tmp/nextui/sdcard` as a stand-in for the device's SD card:

```bash
./workspace/desktop/prepare_fake_sd_root.sh
```

#### 3. Generate compile commands (for IDE support)

Generate `compile_commands.json` so clangd can resolve includes and provide diagnostics:

```bash
make compile-commands
```

This is gitignored since it contains absolute paths. Each developer must run this once after cloning.

### Building (Desktop)

#### Build libmsettings (required first)

```bash
cd workspace/desktop/libmsettings
make build CROSS_COMPILE=/usr/local/bin/ PREFIX=/opt/homebrew PREFIX_LOCAL=/opt/homebrew
```

#### Build nextui

```bash
cd workspace/all/nextui
make PLATFORM=desktop CROSS_COMPILE=/usr/local/bin/ PREFIX=/opt/homebrew PREFIX_LOCAL=/opt/homebrew UNAME_S=Darwin
```

The binary is output to `workspace/all/nextui/build/desktop/nextui.elf`.

### Running (Desktop)

```bash
cd workspace/all/nextui
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./build/desktop/nextui.elf
```

## Quick Build (Device - Docker)

Build and push a specific component directly using docker:

```bash
# nextui
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/nextui && make PLATFORM=tg5040' && adb push workspace/all/nextui/build/tg5040/nextui.elf /mnt/SDCARD/.system/tg5040/bin/ && adb shell reboot

# audiomon
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5050-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/audiomon && make PLATFORM=tg5050' && adb push workspace/all/audiomon/build/tg5050/audiomon.elf /mnt/SDCARD/.system/tg5050/bin/ && adb shell reboot

# minarch
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/minarch && make PLATFORM=tg5040' && adb push workspace/all/minarch/build/tg5040/minarch.elf /mnt/SDCARD/.system/tg5040/bin/ && adb shell reboot

# settings
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5050-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/settings && make PLATFORM=tg5050' && adb push workspace/all/settings/build/tg5050/settings.elf /mnt/SDCARD/Tools/tg5050/Settings.pak/

# updater
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/updater && make PLATFORM=tg5040' && adb push workspace/all/updater/build/tg5040/updater.elf /mnt/SDCARD/Tools/tg5040/Updater.pak/

# bootlogo
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/bootlogo && make PLATFORM=tg5040' && adb push workspace/all/bootlogo/build/tg5040/bootlogo.elf /mnt/SDCARD/Tools/tg5040/Bootlogo.pak/

# music player
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/musicplayer && make PLATFORM=tg5040' && adb push workspace/all/musicplayer/build/tg5040/musicplayer.elf "/mnt/SDCARD/Tools/tg5040/Music Player.pak/"

# sync
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/sync && make PLATFORM=tg5040' && adb push workspace/all/sync/build/tg5040/sync.elf "/mnt/SDCARD/Tools/tg5040/Device Sync.pak/"

# portmaster
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/portmaster && make PLATFORM=tg5040' && adb push workspace/all/portmaster/build/tg5040/portmaster.elf "/mnt/SDCARD/Tools/tg5040/PortMaster.pak/"

# artwork manager (scraper)
docker run --rm -v $(pwd)/workspace:/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/scraper && make PLATFORM=tg5040' && adb push workspace/all/scraper/build/tg5040/scraper.elf "/mnt/SDCARD/Tools/tg5040/Artwork Manager.pak/"
```

## Component Locations

| Component | Source | Output |
|-----------|--------|--------|
| nextui | workspace/all/nextui | build/tg5040/nextui.elf |
| minarch | workspace/all/minarch | build/tg5040/minarch.elf |
| settings | workspace/all/settings | build/tg5040/settings.elf |
| keymon | workspace/tg5040/keymon | keymon.elf |

## IDE Setup (VS Code)

The project uses **clangd** for code intelligence. Install the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).

If you also have the Microsoft C/C++ extension installed, disable its IntelliSense to avoid conflicts (this is already configured in `.vscode/settings.json`):

```json
"C_Cpp.intelliSenseEngine": "disabled"
```

After cloning, run `make compile-commands` to generate `compile_commands.json` for clangd.

Format-on-save is enabled via `.clang-format` + `.vscode/settings.json`.

## Code Formatting

Format all project source files:

```bash
make format
```

This runs `clang-format -i` on all tracked `.c` and `.h` files, respecting `.gitignore` and `.clang-format-ignore`.

Install the pre-commit hook to enforce formatting:

```bash
./scripts/install-hooks.sh
```

## Syncing with Upstream

Pull updates from the upstream repo while keeping a linear history:

```bash
git fetch upstream
git rebase upstream/main
git push --force-with-lease
```


 System Default State (set by MinUI.pak/launch.sh)

 ┌──────────────────────┬────────────────────────────┬───────────────────────┐
 │       Setting        │           TG5050           │        TG5040         │
 ├──────────────────────┼────────────────────────────┼───────────────────────┤
 │ Big core governor    │ cpu4: userspace            │ cpu0: userspace       │
 ├──────────────────────┼────────────────────────────┼───────────────────────┤
 │ Big core speed       │ 2160 MHz                   │ 2000 MHz              │
 ├──────────────────────┼────────────────────────────┼───────────────────────┤
 │ Little core governor │ cpu0: schedutil            │ N/A (homogeneous)     │
 ├──────────────────────┼────────────────────────────┼───────────────────────┤
 │ Little core max      │ 2000 MHz (hw caps at 1416) │ N/A                   │
 ├──────────────────────┼────────────────────────────┼───────────────────────┤
 │ Cores online         │ cpu0,1,4 (others offline)  │ all                   │
 ├──────────────────────┼────────────────────────────┼───────────────────────┤
 │ MinUI loop restores  │ scaling_setspeed only      │ scaling_setspeed only │
 └──────────────────────┴────────────────────────────┴───────────────────────┘

 Available Frequencies

 - TG5050 big (cpu4): 408 672 840 1008 1200 1344 1488 1584 1680 1800 1992 2088 2160 MHz
 - TG5050 little (cpu0): 408 672 792 936 1032 1128 1224 1320 1416 MHz
 - TG5040 (cpu0): 408 600 816 1008 1200 1416 1608 1800 2000 MHz

## CPU Testing & Profiling (via ADB)

### Check which cores are online

```bash
adb shell "for i in 0 1 2 3 4 5 6 7; do echo \"cpu\$i: \$(cat /sys/devices/system/cpu/cpu\$i/online 2>/dev/null)\"; done"
```

### Check CPU frequency, governor, and min/max for both clusters (TG5050)

```bash
# cpu0 (little cluster)
adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq \
  /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor \
  /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq \
  /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

# cpu4 (big cluster)
adb shell cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
```

### Sample CPU frequency over time (5 samples, 1s apart)

```bash
# TG5050 (both clusters)
for i in 1 2 3 4 5; do echo "=== Sample $i ===" && \
  adb shell "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq \
  /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq" && sleep 1; done

# TG5040 (single cluster)
for i in 1 2 3 4 5; do echo "=== Sample $i ===" && \
  adb shell "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq" && sleep 1; done
```

### Check process thread affinity (which cores a process can use)

```bash
# Find PID and show Cpus_allowed_list for all threads
adb shell "pid=\$(pidof PROCESS_NAME); if [ -n \"\$pid\" ]; then \
  echo \"PID: \$pid\"; for tid in \$(ls /proc/\$pid/task/); do \
  echo \"Thread \$tid: \$(cat /proc/\$pid/task/\$tid/status | grep Cpus_allowed_list)\"; \
  done; else echo 'not running'; fi"
```

Replace `PROCESS_NAME` with the binary name (e.g. `ffplay`, `drastic`, `mediaplayer.elf`, `musicplayer.elf`).

### Profile per-thread CPU usage (which core and how much CPU time)

Take two samples a few seconds apart and compare `utime` deltas to see which threads are active:

```bash
adb shell "pid=\$(pidof PROCESS_NAME); for tid in \$(ls /proc/\$pid/task/); do \
  name=\$(cat /proc/\$pid/task/\$tid/comm 2>/dev/null); \
  stat=\$(cat /proc/\$pid/task/\$tid/stat 2>/dev/null); \
  cpu=\$(echo \$stat | awk '{print \$39}'); \
  utime=\$(echo \$stat | awk '{print \$14}'); \
  stime=\$(echo \$stat | awk '{print \$15}'); \
  echo \"Thread \$tid [\$name]: cpu=\$cpu utime=\$utime stime=\$stime\"; done"
```

### Bring cores online/offline

```bash
# Bring a core online
adb shell "echo 1 > /sys/devices/system/cpu/cpu5/online"

# Take a core offline
adb shell "echo 0 > /sys/devices/system/cpu/cpu5/online"
```

### Set CPU frequency and governor live

```bash
# Set governor (schedutil, performance, powersave, ondemand, conservative)
adb shell "echo schedutil > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor"

# Set frequency range
adb shell "echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq"
adb shell "echo 1584000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq"
```

### List running processes

```bash
adb shell ps
```

### Notes

- On TG5050, cores in the same cluster share frequency (cpu4-7 share one governor/freq)
- Bringing a core online automatically adds it to the cluster's shared frequency domain
- The `performance` governor locks at `scaling_max_freq` — useful for testing fixed speeds
- The `schedutil` governor scales dynamically based on load — better for battery life
- After exiting an app, MinUI.pak/launch.sh restores the default CPU state
- Launch scripts in each `.pak` are responsible for setting CPU speeds on entry and cleanup on exit