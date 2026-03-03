#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/ppsspp"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# BIG cluster: cpu4-5 (Cortex-A55, 2160 MHz)
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo 2160000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
echo 1992000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq

# GPU: lock to performance for PPSSPP rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

# Memory management: swap for large PSP games
SWAPFILE="/mnt/UDISK/psp_swap"
if [ ! -f "$SWAPFILE" ]; then
    dd if=/dev/zero of="$SWAPFILE" bs=1M count=256 2>/dev/null
    mkswap "$SWAPFILE" 2>/dev/null
fi
swapon "$SWAPFILE" 2>/dev/null
echo 200 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
sync
echo 3 >/proc/sys/vm/drop_caches 2>/dev/null

# Shared save directories (across all devices)
SHARED_DIR="$SHARED_USERDATA_PATH/PSP-ppsspp/shared"
mkdir -p "$SHARED_DIR/SAVEDATA"
mkdir -p "$SHARED_DIR/PPSSPP_STATE"

# Device-specific HOME
DEVICE_HOME="$SHARED_USERDATA_PATH/PSP-ppsspp/tg5050"
mkdir -p "$DEVICE_HOME/PSP/SYSTEM"

# Symlink shared saves into device HOME
ln -sfn "$SHARED_DIR/SAVEDATA" "$DEVICE_HOME/PSP/SAVEDATA"
ln -sfn "$SHARED_DIR/PPSSPP_STATE" "$DEVICE_HOME/PSP/PPSSPP_STATE"

# First run: copy device-specific defaults
if [ ! -f "$DEVICE_HOME/.initialized" ]; then
    cp "$PAK_DIR/default.ini" "$DEVICE_HOME/PSP/SYSTEM/ppsspp.ini"
    touch "$DEVICE_HOME/.initialized"
fi

export HOME="$DEVICE_HOME"
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5050/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
export LD_PRELOAD="libEGL.so"

# Clear stale graphics backend failure markers (PPSSPP persists these across runs)
rm -f "$DEVICE_HOME/.config/ppsspp/PSP/SYSTEM/FailedGraphicsBackends.txt" 2>/dev/null

# Overlay menu config
export EMU_OVERLAY_JSON="$EMU_DIR/overlay_settings.json"
export EMU_OVERLAY_INI="$DEVICE_HOME/PSP/SYSTEM/ppsspp.ini"
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

# Launch PPSSPP (binary in PAK_DIR, assets in EMU_DIR)
cd "$EMU_DIR"
"$PAK_DIR/PPSSPPSDL" --fullscreen --xres 1280 --yres 720 "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
EMU_PID=$!
sleep 3

# Thread pinning (dual cluster):
#   main thread (SDL + emu)    → BIG cpu4
#   render thread (GL)         → BIG cpu5
#   audio/mali/helpers         → LITTLE cpu0-1
taskset -p 0x10 "$EMU_PID" 2>/dev/null   # mask 0x10 = cpu4

# Move audio/mali/helpers to LITTLE cores
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    case "$TNAME" in
        SDLAudioP2|SDLHotplug*|SDLTimer|mali-*)
            taskset -p 0x3 "$TID" 2>/dev/null ;;  # mask 0x3 = cpu0-1
    esac
done

# Find the busiest non-main thread (render/emu thread) and pin to cpu5
sleep 2
BEST_TID=""
BEST_UTIME=0
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    case "$TNAME" in
        EmuThread|PPSSPPSDL)
            UTIME=$(awk '{print $14}' /proc/$EMU_PID/task/$TID/stat 2>/dev/null)
            UTIME=${UTIME:-0}
            if [ "$UTIME" -gt "$BEST_UTIME" ]; then
                BEST_UTIME=$UTIME
                BEST_TID=$TID
            fi
            ;;
    esac
done
[ -n "$BEST_TID" ] && taskset -p 0x20 "$BEST_TID" 2>/dev/null  # mask 0x20 = cpu5

wait $EMU_PID

# Cleanup: disable swap, restore VM defaults
swapoff "$SWAPFILE" 2>/dev/null
echo 100 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
