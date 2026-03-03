#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/ppsspp"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# Single cluster: cpu0-3 (Cortex-A53, max 2000 MHz)
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

# Memory management: swap for large PSP games
SWAPFILE="/mnt/UDISK/psp_swap"
if [ ! -f "$SWAPFILE" ]; then
    dd if=/dev/zero of="$SWAPFILE" bs=1M count=512 2>/dev/null
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

# Device-specific HOME and resolution
if [ "$DEVICE" = "brick" ]; then
    DEVICE_HOME="$SHARED_USERDATA_PATH/PSP-ppsspp/tg5040-brick"
    DEVICE_DEFAULT_INI="$PAK_DIR/default-brick.ini"
    DEVICE_XRES=1024
    DEVICE_YRES=768
else
    DEVICE_HOME="$SHARED_USERDATA_PATH/PSP-ppsspp/tg5040-smart-pro"
    DEVICE_DEFAULT_INI="$PAK_DIR/default-smartpro.ini"
    DEVICE_XRES=1280
    DEVICE_YRES=720
fi
mkdir -p "$DEVICE_HOME/PSP/SYSTEM"

# PPSSPP v1.20.1 on Linux reads config from XDG path ($HOME/.config/ppsspp/)
PPSSPP_CONFIG="$DEVICE_HOME/.config/ppsspp/PSP/SYSTEM"
mkdir -p "$PPSSPP_CONFIG"

# Symlink shared saves into device HOME
ln -sfn "$SHARED_DIR/SAVEDATA" "$DEVICE_HOME/PSP/SAVEDATA"
ln -sfn "$SHARED_DIR/PPSSPP_STATE" "$DEVICE_HOME/PSP/PPSSPP_STATE"

# First run: copy device-specific defaults to XDG config path
if [ ! -f "$DEVICE_HOME/.initialized" ]; then
    cp "$DEVICE_DEFAULT_INI" "$PPSSPP_CONFIG/ppsspp.ini"
    touch "$DEVICE_HOME/.initialized"
fi

export HOME="$DEVICE_HOME"
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5040/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
export LD_PRELOAD="libEGL.so"

# Clear stale graphics backend failure markers (PPSSPP persists these across runs)
rm -f "$DEVICE_HOME/.config/ppsspp/PSP/SYSTEM/FailedGraphicsBackends.txt" 2>/dev/null

# Overlay menu config
export EMU_OVERLAY_JSON="$EMU_DIR/overlay_settings.json"
export EMU_OVERLAY_INI="$PPSSPP_CONFIG/ppsspp.ini"
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
"$PAK_DIR/PPSSPPSDL" --fullscreen --xres "$DEVICE_XRES" --yres "$DEVICE_YRES" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"

# Cleanup: disable swap, restore VM defaults
swapoff "$SWAPFILE" 2>/dev/null
echo 100 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
