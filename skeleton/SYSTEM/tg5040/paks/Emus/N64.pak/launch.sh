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
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
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

# --- netplay pre-launch wizard -------------------------------------------
# sleepmon + the unmute timer + swap are already running/active here, so an
# early exit MUST undo them itself (same rationale as DC.pak's bail helper).
nx_netplay_bail() {
    killall sleepmon.elf 2>/dev/null || true
    kill $SYNC_PID 2>/dev/null || true
    echo 0 > /sys/class/speaker/mute 2>/dev/null || true
    swapoff "$SWAPFILE" 2>/dev/null
    echo 100 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
    exit 0
}

NETPLAY_ARGS=""
NETPLAY_SERVER_PID=""
NETPLAY_RAN=""
if [ -f /tmp/netplay_launch ]; then
    rm -f /tmp/netplay_launch
    NETPLAY_RAN=1
    # No sync args: the mupen64plus netplay protocol transfers P1's save
    # files to the joiner by itself (TCP_SEND_SAVE/TCP_RECEIVE_SAVE), so
    # the wizard is rendezvous-only here. --max-players 4: N64 supports up
    # to 4 players, so the host runs the multi-join lobby (X/N + press-A-to
    # -start); DC/minarch omit it and stay 2-player (shared wizard).
    netplay.elf --game "$EMU_OVERLAY_GAME" --max-players 4 &> "$LOGS_PATH/netplay-wizard.txt"
    if [ $? -ne 0 ]; then
        # cancelled or failed: back to the game list, never a peerless
        # netplay launch (single-player is one A press away)
        nx_netplay_bail
    fi
    # Sourcing an absent file is FATAL in ash; bail instead (see DC.pak).
    [ -f /tmp/netplay_session ] || nx_netplay_bail
    . /tmp/netplay_session
    NX_NETPLAY_PORT=55445
    # Player number + total: explicit from the wizard (4-player), else derived
    # from role for a 2-player session produced by an older wizard.
    NX_PLAYER="${NETPLAY_PLAYER:-}"
    NX_NUM="${NETPLAY_NUM_PLAYERS:-}"
    if [ -z "$NX_PLAYER" ]; then
        [ "$NETPLAY_ROLE" = "host" ] && NX_PLAYER=1 || NX_PLAYER=2
    fi
    [ -z "$NX_NUM" ] && NX_NUM=2

    # Per-session mupen config dir: local pad STAYS on Control1 (the core routes
    # it to this device's netplay seat), ports 1..N marked plugged so the game
    # shows N controllers. Values carry spaces/quotes, so rewrite the config
    # sections wholesale rather than via --set (see nx_netplay_map.awk).
    NX_SESSION_CFG="/tmp/n64-netplay-cfg"
    rm -rf "$NX_SESSION_CFG"; mkdir -p "$NX_SESSION_CFG"
    cp "$DEVICE_CONFIG_DIR"/*.cfg "$NX_SESSION_CFG"/ 2>/dev/null
    if ! awk -v P="$NX_PLAYER" -v N="$NX_NUM" -f "$PAK_DIR/nx_netplay_map.awk" \
            "$DEVICE_CONFIG_DIR/mupen64plus.cfg" > "$NX_SESSION_CFG/mupen64plus.cfg"; then
        nx_netplay_bail   # a broken controller map must not launch peerless
    fi

    if [ "$NETPLAY_ROLE" = "host" ]; then
        # Host runs the netplay server and plays seat 1: the protocol makes
        # player 1 the source of truth for saves and core settings, so the
        # host plays on its REAL saves ("host brings the memory card").
        killall m64p-server.elf 2>/dev/null || true
        "$PAK_DIR/m64p-server.elf" --port $NX_NETPLAY_PORT --players $NX_NUM --buffer-target 2 \
            &> "$LOGS_PATH/n64-netplay-server.txt" &
        NETPLAY_SERVER_PID=$!
        # Pin the relay server OFF the latency-critical cores. mupen's main/
        # dynarec thread is pinned to cpu0 and the GLideN64 video thread to cpu1
        # (below); on the 4-core Brick an UNPINNED server that lands on cpu0
        # steals dynarec cycles and stalls emulation, delaying even the host's
        # own input (it round-trips through this local 127.0.0.1 server). Share
        # the audio/helper cores (cpu2-3) instead — the relay is light but must
        # not preempt emu/video.
        taskset -p 0xc "$NETPLAY_SERVER_PID" 2>/dev/null || true
        NETPLAY_ARGS="--netplay 127.0.0.1 $NX_NETPLAY_PORT --netplay-player $NX_PLAYER"
    else
        # Joiner receives the host's saves in-memory at boot, but in-game
        # writes still land on disk: point them at a disposable staging dir
        # so the joiner's real single-player saves are untouched. The core
        # mkdirp()s the dir itself (main.c get_savepathdefault).
        NETPLAY_ARGS="--netplay $NETPLAY_PEER_IP $NX_NETPLAY_PORT --netplay-player $NX_PLAYER --set Core[SaveSRAMPath]=$USERDATA_DIR/netplay-data/mupen64plus/save"
    fi
    # GPU headroom for netplay on this platform's slower SoC. GLideN64/video
    # settings are LOCAL and never synced by the netplay protocol (only core
    # CPU/RSP timing is), so lightening them cannot desync the session -- it just
    # lets the slower device hold 60 fps and keep pace with the input buffer.
    # NETPLAY_ARGS is applied LAST in the mupen invocation, so these override any
    # per-game or global cfg value. Session-only (--nosaveoptions) -> single-
    # player is untouched (keeps hi-res + its configured resolution).
    #   - hi-res textures OFF: far larger than native N64 textures (GPU memory/
    #     bandwidth every frame + .hts-cache stream stutters). The biggest win —
    #     alone it dropped the Brick's buffer lag from ~50 frames to ~1.
    #   - 1x native resolution: 2x proved too heavy for the Brick during busy
    #     races even with hi-res off (display fell behind), so netplay forces 1x.
    # NB: 3+ player split-screen renders one 3D viewport PER seat (~Nx GPU work).
    # The Brick's Mali cannot hold 60 fps on a 3-way split and drifts seconds
    # behind (measured: renders ~45 fps while the host feeds 60; cpu2/3 sit idle,
    # so it's GPU-bound, not CPU/server/network). Stripping GPU features further
    # (framebuffer readbacks, etc.) did NOT recover it -- 3-player split-screen
    # needs a Smart-Pro-class GPU on every seat. 2-player (2-way split) is fine.
    NETPLAY_ARGS="$NETPLAY_ARGS --set Video-GLideN64[txHiresEnable]=False --set Video-GLideN64[UseNativeResolutionFactor]=1"
fi
# --- end netplay ----------------------------------------------------------

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
    --configdir "${NX_SESSION_CFG:-$DEVICE_CONFIG_DIR}" \
    --datadir "$EMU_DIR" \
    --plugindir "$PAK_DIR" \
    --nosaveoptions \
    $GAME_ARGS $AUDIO_OVERRIDE $NETPLAY_ARGS \
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

# Pin EVERY non-main mupen64plus worker thread (GLideN64 video, etc.) to cpu1,
# keeping cpu0 exclusively for the main CPU-emulation/dynarec thread. The old
# "find the single busiest thread and pin it" heuristic ranked by a 2 s
# utime-only snapshot and could pick the wrong (light) thread, leaving a heavy
# worker floating on cpu0 to steal dynarec cycles. That was tolerable in
# single-player but, under netplay, the extra sync work on cpu0 plus the
# intruding worker pushed the slower Brick behind the sync buffer (cyclic
# lag/catch-up). Pinning all workers is snapshot-independent. Non-mupen-named
# threads (SDL helpers on cpu2-3 above, transient "Netplay key request"
# threads) are intentionally left as they are.
sleep 2
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    [ "$TNAME" = "mupen64plus" ] || continue
    taskset -p 0x2 "$TID" 2>/dev/null  # mask 0x2 = cpu1
done

wait $EMU_PID
killall sleepmon.elf 2>/dev/null || true
kill $SYNC_PID 2>/dev/null || true
echo 0 > /sys/class/speaker/mute 2>/dev/null || true

# Netplay teardown: stop the host's server, undo hotspot/WiFi state.
[ -n "$NETPLAY_SERVER_PID" ] && kill $NETPLAY_SERVER_PID 2>/dev/null
if [ -n "$NETPLAY_RAN" ]; then
    netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
    rm -f /tmp/netplay_session
    rm -rf "$NX_SESSION_CFG"
fi

# Cleanup: disable swap, restore VM defaults
swapoff "$SWAPFILE" 2>/dev/null
echo 100 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
