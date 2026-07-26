#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/flycast"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# BIG cluster: cpu4-5 (Cortex-A55, 2160 MHz)
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo 2160000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
echo 1992000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq

# GPU: lock to performance for flycast rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

# User data (config per device, VMU + save states shared across devices)
USERDATA_DIR="$SHARED_USERDATA_PATH/DC-flycast"
DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5050"
DEVICE_DEFAULT_CFG="$PAK_DIR/default.cfg"
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
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5050/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"

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

# Thread pinning (dual cluster). Evidence-based (see task-11 report): tg5040
# testing showed the SH4/emu thread ("Flycast-emu") measured only ~57% utime
# of a single floating core (spread across 4 shared cores by the scheduler)
# but jumped to ~96-100% of a dedicated core once pinned - it was being
# diluted/contended, not genuinely idle - and pinning cut the real audio
# thread's ("SDLAudioP2") nonvoluntary context switches by ~4-8x (measured
# ~65-118 per 10s floating -> ~13-28 per 10s pinned). Later confirmed on a
# real Smart Pro S (tg5050) too - dual-cluster pinning applies cleanly there
# as well, matching the tg5040 finding and the identical N64.pak precedent
# below.
#   Flycast-emu  (SH4 CPU + dynarec)       -> BIG cpu4
#   Flycast-rend (main thread, GL present) -> BIG cpu5
#   audio + helpers                        -> LITTLE cpu0-1
# NOTE: the shared $SHARED_SYSTEM_PATH/bin/taskset this used to fall back to
# was found (task-11, tg5040 testing) to crash with "FATAL: kernel too old"
# - a statically-linked glibc binary's own startup code
# (_dl_non_dynamic_init(), only run for static executables) rejecting the
# real device kernel against a compile-time __LINUX_KERNEL_VERSION floor
# baked in from the toolchain's own kernel headers, not a toolchain-target
# mismatch and NOT the .note.ABI-tag (this repo's own dynamic binaries
# carry a "for GNU/Linux 4.19.0" ABI-tag note and run fine on the Brick's
# 4.9 kernel, so that note itself isn't what's enforced) - a rebuild with
# tg5040's own toolchain still aborted identically. Fixed by dropping
# -static in workspace/all/taskset/Makefile and shipping a rebuilt tg5050
# binary at skeleton/SYSTEM/tg5050/bin/taskset, which this launch.sh's own
# PATH ordering ($SYSTEM_PATH/bin before $SHARED_SYSTEM_PATH/bin) picks up
# automatically. Confirmed working on real Smart Pro S (tg5050) hardware:
# no "kernel too old" crash, and pinning applies as expected.
#
# Two passes: Flycast-emu (the SH4 thread) doesn't spawn until the game
# actually starts, and a real-BIOS boot can take longer than a single fixed
# sleep, so a one-shot scan at the same point every launch can miss it.
# Both calls below already tolerate a not-yet-spawned thread (no comm
# match) and a still-broken taskset (2>/dev/null) safely, so re-running the
# same scan is harmless if the first pass already caught everything.
pin_threads() {
    taskset -p 0x20 "$EMU_PID" 2>/dev/null   # mask 0x20 = cpu5 (main/render thread)
    for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
        [ "$TID" = "$EMU_PID" ] && continue
        TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
        case "$TNAME" in
            Flycast-emu)
                taskset -p 0x10 "$TID" 2>/dev/null ;;  # mask 0x10 = cpu4
            Flycast-rend|SDLAudioP2|SDLHotplug*|SDLTimer|mali-*)
                taskset -p 0x3 "$TID" 2>/dev/null ;;   # mask 0x3 = cpu0-1
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
