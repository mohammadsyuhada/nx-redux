#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/mupen64plus"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# Single cluster: cpu0-3 (Cortex-A53, max 2000 MHz)
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

# Memory management: swap + VM tuning for hi-res texture loading
SWAPFILE="/mnt/UDISK/n64_swap"
if [ ! -f "$SWAPFILE" ]; then
    dd if=/dev/zero of="$SWAPFILE" bs=1M count=512 2>/dev/null
    mkswap "$SWAPFILE" 2>/dev/null
fi
swapon "$SWAPFILE" 2>/dev/null
echo 200 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
sync
echo 3 >/proc/sys/vm/drop_caches 2>/dev/null

# Config-dir resolution + mupen64plus.cfg seeding shared with options.sh
. "$PAK_DIR/nx_paths.sh"

# Render resolution is launch-only, so it stays out of nx_paths.sh
if [ "$DEVICE" = "brick" ] || [ "$DEVICE" = "brickpro" ]; then
    DEVICE_RESOLUTION="1024x768"
else
    DEVICE_RESOLUTION="1280x720"
fi

export HOME="$USERDATA_PATH"
# Saves/states genuinely shared across devices: mupen64plus writes .st*/.eep/
# .mpk/.sra under $XDG_DATA_HOME/mupen64plus/save (defaulting to the per-device
# $HOME/.local/share), so point the data home into the shared dir. HOME itself
# stays per-device on purpose — shader/texture caches must not cross devices.
export XDG_DATA_HOME="$USERDATA_DIR/data"
# Migrate saves that older builds wrote under the per-device $HOME. Existing
# shared files win; unmigrated originals stay put for manual recovery.
OLD_SAVE_DIR="$USERDATA_PATH/.local/share/mupen64plus/save"
NEW_SAVE_DIR="$XDG_DATA_HOME/mupen64plus/save"
mkdir -p "$NEW_SAVE_DIR"
if [ -d "$OLD_SAVE_DIR" ]; then
    for f in "$OLD_SAVE_DIR"/*; do
        [ -e "$f" ] || continue
        [ -e "$NEW_SAVE_DIR/$(basename "$f")" ] || mv "$f" "$NEW_SAVE_DIR/"
    done
    rmdir "$OLD_SAVE_DIR" 2>/dev/null
fi
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5040/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
export LD_PRELOAD="libEGL.so"

# Overlay menu config
export EMU_OVERLAY_JSON="$EMU_DIR/overlay_settings.json"
export EMU_OVERLAY_INI="$DEVICE_CONFIG_DIR/mupen64plus.cfg"
export EMU_OVERLAY_GAME="$(basename "$ROM" | sed 's/\.[^.]*$//')"
# Font and icon resources for overlay menu (from NextUI system resources)
FONT_FILE=$(ls "$SDCARD_PATH/.system/res/"*.ttf 2>/dev/null | head -1)
export EMU_OVERLAY_FONT="${FONT_FILE:-$SDCARD_PATH/.system/res/font.ttf}"
export EMU_OVERLAY_RES="$SDCARD_PATH/.system/res"
# Screenshot directory (matches minarch's .minui path for game switcher)
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

# Pick the device-open rate for the current audio sink (audiomon publishes
# /tmp/nx_audio_sink). Exact match on the native rate wins; otherwise the
# sink's preferred (first-listed) rate; 48000 when the file is absent.
nx_pick_audio_rate() {
    NATIVE="$1"
    RATE=48000
    if [ -f /tmp/nx_audio_sink ]; then
        RATES=$(sed -n 's/^rates=//p' /tmp/nx_audio_sink)
        for R in $RATES; do
            if [ "$R" = "$NATIVE" ]; then
                echo "$NATIVE"
                return
            fi
        done
        FIRST=${RATES%% *}
        [ -n "$FIRST" ] && RATE=$FIRST
    fi
    echo "$RATE"
}

# audio-sdl resamples the game (32-44.1 kHz) to OUTPUT_FREQUENCY; keep the cfg
# default of 48000 unless the active sink prefers a different rate (e.g. a
# 44.1 kHz Bluetooth link), so ALSA plug stays a pass-through.
NX_AUDIO_RATE=$(nx_pick_audio_rate 48000)
AUDIO_OVERRIDE=""
[ "$NX_AUDIO_RATE" != "48000" ] && AUDIO_OVERRIDE="--set Audio-SDL[OUTPUT_FREQUENCY]=$NX_AUDIO_RATE"

# --- Per-game option overrides ------------------------------------------
# Written by options.elf ("Emulator Options" in the game list's context
# menu). Each entry becomes a --set applied under --nosaveoptions, so the
# values live only in this session's memory: mupen64plus.cfg stays the
# device-global config and can never absorb per-game values. Values are
# ints/bools/floats with no whitespace, so word-splitting GAME_ARGS is
# safe (the [ ] glob chars only expand if a matching file exists in the
# cwd -- the same exposure AUDIO_OVERRIDE already has). A malformed or
# unreadable file yields no args: the game still launches on globals.
GAME_ARGS=""
NX_ROM_BASE="$(nx_rom_base "$ROM")"
NX_GAME_CFG="$DEVICE_CONFIG_DIR/games/$NX_ROM_BASE.cfg"
if [ -f "$NX_GAME_CFG" ]; then
    GAME_ARGS=$(awk -F' = ' '
        /^\[.*\]$/ { sec = substr($0, 2, length($0) - 2); next }
        NF == 2 && sec != "" { printf "--set %s[%s]=%s ", sec, $1, $2 }
    ' "$NX_GAME_CFG" 2>/dev/null)
fi
# --- end per-game overrides ---------------------------------------------

# Launch from PAK_DIR so core library resolves via ./
cd "$PAK_DIR"
# --nosaveoptions: without it, ui-console persists --set values at startup
# (SaveConfigurationOptions, main.c:991) and rewrites mupen64plus.cfg from
# core memory on exit (ConfigSaveFile, main.c:1135/1150) -- exactly the
# config rewriting this design forbids. With it, every --set is
# session-virtual and the cfg is owned solely by options.elf + the
# nx_paths.sh seeder. $GAME_ARGS before $AUDIO_OVERRIDE so the negotiated
# audio rate wins any future conflict (--set applies left-to-right).
./mupen64plus --fullscreen --resolution "$DEVICE_RESOLUTION" \
    --configdir "$DEVICE_CONFIG_DIR" \
    --datadir "$EMU_DIR" \
    --plugindir "$PAK_DIR" \
    --nosaveoptions \
    $GAME_ARGS $AUDIO_OVERRIDE \
    --gfx "$EMU_DIR/mupen64plus-video-GLideN64.so" \
    --audio mupen64plus-audio-sdl.so \
    --input mupen64plus-input-sdl.so \
    --rsp mupen64plus-rsp-hle.so \
    "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
EMU_PID=$!
sleep 4

# Thread pinning (cpu0-3 are all Cortex-A53 @ 2000 MHz):
#   main thread (cpu emu + dynarec) → cpu0
#   video thread (GLideN64)         → cpu1
#   audio + helpers                 → cpu2-3
taskset -p 1 "$EMU_PID" 2>/dev/null   # mask 0x1 = cpu0

# Pin known helper threads to cpu2-3
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    case "$TNAME" in
        SDLAudioP2|SDLHotplug*|SDLTimer|mali-*|m64pwq)
            taskset -p 0xc "$TID" 2>/dev/null ;;  # mask 0xc = cpu2-3
    esac
done

# Find the busiest non-main mupen64plus thread (video thread) and pin to cpu1
sleep 2
BEST_TID=""
BEST_UTIME=0
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    [ "$TNAME" = "mupen64plus" ] || continue
    UTIME=$(awk '{print $14}' /proc/$EMU_PID/task/$TID/stat 2>/dev/null)
    UTIME=${UTIME:-0}
    if [ "$UTIME" -gt "$BEST_UTIME" ]; then
        BEST_UTIME=$UTIME
        BEST_TID=$TID
    fi
done
[ -n "$BEST_TID" ] && taskset -p 2 "$BEST_TID" 2>/dev/null  # mask 0x2 = cpu1

wait $EMU_PID
killall sleepmon.elf 2>/dev/null || true
kill $SYNC_PID 2>/dev/null || true
echo 0 > /sys/class/speaker/mute 2>/dev/null || true

# Cleanup: disable swap, restore VM defaults
swapoff "$SWAPFILE" 2>/dev/null
echo 100 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
