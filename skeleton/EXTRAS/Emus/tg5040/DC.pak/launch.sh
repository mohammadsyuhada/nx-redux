#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/flycast"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# Single cluster: cpu0-3 (Cortex-A53, max 2000 MHz)
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

# User data (config per device, VMU + save states shared across devices)
USERDATA_DIR="$SHARED_USERDATA_PATH/DC-flycast"
if [ "$DEVICE" = "brick" ]; then
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-brick"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-brick.cfg"
elif [ "$DEVICE" = "brickpro" ]; then
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-brickpro"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-brickpro.cfg"
else
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-smart-pro"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-smartpro.cfg"
fi
mkdir -p "$DEVICE_CONFIG_DIR/flycast/mappings" "$USERDATA_DIR/data/flycast"

# First run: copy device-specific defaults
if [ ! -f "$DEVICE_CONFIG_DIR/.initialized" ]; then
    cp "$DEVICE_DEFAULT_CFG" "$DEVICE_CONFIG_DIR/flycast/emu.cfg"
    touch "$DEVICE_CONFIG_DIR/.initialized"
fi
EMU_CFG="$DEVICE_CONFIG_DIR/flycast/emu.cfg"

# Install the curated controller mapping if not already present. Deliberately
# NOT gated by .initialized: that marker only tracks the emu.cfg seed, so an
# existing install upgraded from an older pak version (already .initialized,
# but never had a mapping shipped) would otherwise never receive it. Install
# unconditionally-if-absent instead, every launch, and never overwrite a
# mapping the user has since customized via flycast's own Controls UI.
if [ ! -f "$DEVICE_CONFIG_DIR/flycast/mappings/SDL_Xbox 360 Controller.cfg" ]; then
    cp "$PAK_DIR/SDL_Xbox 360 Controller.cfg" "$DEVICE_CONFIG_DIR/flycast/mappings/" 2>/dev/null || true
fi

# Flycast resolves config to $XDG_CONFIG_HOME/flycast/ and data (BIOS search,
# VMUs, save states) to $XDG_DATA_HOME/flycast/ (core/linux-dist/main.cpp).
export HOME="$USERDATA_PATH"
export XDG_CONFIG_HOME="$DEVICE_CONFIG_DIR"
export XDG_DATA_HOME="$USERDATA_DIR/data"
export FLYCAST_BIOS_PATH="$SDCARD_PATH/Bios/DC"
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5040/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"

# BIOS: prefer real BIOS when present, HLE (Reios) otherwise. Never block launch.
# dc_flash.bin is not required -- flycast auto-creates it when missing
# (core/hw/flashrom/nvmem.cpp); only dc_boot.bin gates real-BIOS mode.
if [ -f "$SDCARD_PATH/Bios/DC/dc_boot.bin" ]; then
    sed -i 's/^UseReios =.*/UseReios = no/' "$EMU_CFG"
else
    sed -i 's/^UseReios =.*/UseReios = yes/' "$EMU_CFG"
fi

# RetroAchievements: sync credentials from the NextUI RA login (if any).
RA_USER=$(sed -n 's/^raUsername=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
RA_TOKEN=$(sed -n 's/^raToken=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
if [ -n "$RA_USER" ] && [ -n "$RA_TOKEN" ]; then
    sed -i "/^\[achievements\]/,/^\[/{s/^UserName =.*/UserName = $RA_USER/;s/^Token =.*/Token = $RA_TOKEN/;}" "$EMU_CFG"
else
    sed -i "/^\[achievements\]/,/^\[/{s/^Enabled =.*/Enabled = no/;}" "$EMU_CFG"
fi

# Overlay menu config
export EMU_OVERLAY_JSON="$EMU_DIR/overlay_settings.json"
export EMU_OVERLAY_INI="$EMU_CFG"
export EMU_OVERLAY_GAME="$(basename "$ROM" | sed 's/\.[^.]*$//')"
FONT_FILE=$(ls "$SDCARD_PATH/.system/res/"*.ttf 2>/dev/null | head -1)
export EMU_OVERLAY_FONT="${FONT_FILE:-$SDCARD_PATH/.system/res/font.ttf}"
export EMU_OVERLAY_RES="$SDCARD_PATH/.system/res"
MINUI_DIR="$SHARED_USERDATA_PATH/.minui/$EMU_TAG"
mkdir -p "$MINUI_DIR"
export EMU_OVERLAY_SCREENSHOT_DIR="$MINUI_DIR"
export EMU_OVERLAY_ROMFILE="$(basename "$ROM")"

# Mute speaker before launch to prevent audio pop, then unmute after init
echo 1 > /sys/class/speaker/mute 2>/dev/null || true
(sleep 5; echo 0 > /sys/class/speaker/mute 2>/dev/null; syncsettings.elf) &
SYNC_PID=$!

# Start power button sleep/poweroff handler
sleepmon.elf &

cd "$PAK_DIR"
./flycast "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
EMU_PID=$!

# Thread pinning (cpu0-3 are all Cortex-A53 @ 2000 MHz). Evidence-based (see
# task-11 report): with all threads free-floating across the 4 shared cores,
# the SH4/emu thread ("Flycast-emu") measured only ~57% utime of a single
# core - looked like headroom - but jumped to ~96-100% of ITS OWN core the
# moment it was given a dedicated one, meaning it was actually being diluted/
# contended across cores rather than genuinely under 100%. Pinning also cut
# the real audio thread's ("SDLAudioP2") nonvoluntary context switches by
# roughly 4-8x (measured ~65-118 per 10s floating -> ~13-28 per 10s pinned),
# i.e. it is preempted far less once the emu/render threads stop migrating
# through its cores. That reduction in preemption/jitter for the thread that
# actually feeds the audio buffer is the most direct evidence pinning helps
# the gameplay-only distortion this task investigated.
#   Flycast-emu  (SH4 CPU + dynarec)       -> cpu0
#   Flycast-rend (main thread, GL present) -> cpu1
#   audio + helpers                        -> cpu2-3
# NOTE: taskset used to resolve (via PATH) to $SHARED_SYSTEM_PATH/bin/taskset
# (skeleton/SYSTEM/shared/bin/taskset), which crashed on this Brick unit
# ("FATAL: kernel too old" / SIGILL) - a statically-linked glibc binary's
# own startup code (_dl_non_dynamic_init(), only run for static executables)
# rejecting this device's real 4.9.191 kernel against a compile-time
# __LINUX_KERNEL_VERSION floor baked in from the toolchain's own kernel
# headers - NOT the .note.ABI-tag (this repo's own dynamic binaries carry a
# "for GNU/Linux 4.19.0" ABI-tag note and run fine on 4.9, so that note
# itself clearly isn't what's enforced). Confirmed by rebuilding with
# tg5040's own toolchain: still aborted identically. This silently no-op'd
# N64.pak's identical pinning too (masked by its own `2>/dev/null`). Fixed
# in this same task by dropping -static from workspace/all/taskset/Makefile
# (dynamic linking resolves libc.so.6 from the device's own /usr/lib64 at
# runtime instead, same resolution flycast itself relies on) and shipping
# the rebuilt binary at skeleton/SYSTEM/tg5040/bin/taskset, which now
# shadows the broken shared copy via this launch.sh's own PATH ordering
# ($SYSTEM_PATH/bin before $SHARED_SYSTEM_PATH/bin) - N64.pak's existing
# bare `taskset` calls pick this fix up automatically too, no edit needed
# there. Confirmed working on-device (task-11): no crash, affinity applied.
#
# Two passes: Flycast-emu (the SH4 thread) doesn't spawn until the game
# actually starts, and a real-BIOS boot can take longer than a single fixed
# sleep, so a one-shot scan at the same point every launch can miss it.
# Both calls below already tolerate a not-yet-spawned thread (no comm
# match) and a still-broken taskset (2>/dev/null) safely, so re-running the
# same scan is harmless if the first pass already caught everything.
pin_threads() {
    taskset -p 2 "$EMU_PID" 2>/dev/null   # mask 0x2 = cpu1 (main/render thread)
    for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
        [ "$TID" = "$EMU_PID" ] && continue
        TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
        case "$TNAME" in
            Flycast-emu)
                taskset -p 1 "$TID" 2>/dev/null ;;    # mask 0x1 = cpu0
            Flycast-rend|SDLAudioP2|SDLHotplug*|SDLTimer|mali-*)
                taskset -p 0xc "$TID" 2>/dev/null ;;  # mask 0xc = cpu2-3
        esac
    done
}

# First pass: catches Flycast-rend and most helpers.
sleep 4
pin_threads

# Second pass: catches Flycast-emu and any other late-spawning thread that
# missed the first pass (e.g. a slow real-BIOS boot). Wait up to 8 more
# seconds for it to show up, but poll liveness every second and stop early
# (skipping the second pass entirely) if flycast has already exited - an
# unconditional sleep here would otherwise delay cleanup by up to ~12s on
# a quick quit or crash.
for _ in 1 2 3 4 5 6 7 8; do
    kill -0 "$EMU_PID" 2>/dev/null || break
    sleep 1
done
kill -0 "$EMU_PID" 2>/dev/null && pin_threads

wait $EMU_PID
killall sleepmon.elf 2>/dev/null || true
kill $SYNC_PID 2>/dev/null || true
echo 0 > /sys/class/speaker/mute 2>/dev/null || true
