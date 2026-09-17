#!/bin/sh
# Tools/Cheat Database.pak/launch.sh - download/update/remove the libretro
# cheat database (the DATA side of the Cheat Database feature; the small pak
# CODE is installed/updated via Xtras). Runs with the full MinUI pak env
# (CHEATS_PATH, SHARED_USERDATA_PATH, LOGS_PATH; PATH has .system/shared/bin
# first so the vendored GNU wget + 7zzs win, and .system/<plat>/bin for
# options.elf/show2.elf). Busybox sh.
cd "$(dirname "$0")"
rm -f "$LOGS_PATH/cheatdb.txt"
exec >"$LOGS_PATH/cheatdb.txt" 2>&1

# Low fixed frequency for the simple UI.
echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

CHEATS_URL="https://buildbot.libretro.com/assets/frontend/cheats.zip"
: "${NX_EXTRAS_UNZIP:=$SDCARD_PATH/.system/shared/bin/7zzs.aarch64}"
STATE_DIR="$SHARED_USERDATA_PATH/xtras"
MANIFEST="$STATE_DIR/cheatdb.manifest"
DBVER="$STATE_DIR/cheatdb.db_lastmod"
CHEATS_DIR="${CHEATS_PATH:-$SDCARD_PATH/Cheats}"
TMPDIR_NX="$SDCARD_PATH/.extras_tmp"
ZIP="$TMPDIR_NX/cheats.zip"
NEED_KB=262144

MAP="FC|Nintendo - Nintendo Entertainment System
FDS|Nintendo - Family Computer Disk System
GB|Nintendo - Game Boy
SGB|Nintendo - Game Boy
GBC|Nintendo - Game Boy Color
GBA|Nintendo - Game Boy Advance
MGBA|Nintendo - Game Boy Advance
SFC|Nintendo - Super Nintendo Entertainment System
SUPA|Nintendo - Super Nintendo Entertainment System
VB|Nintendo - Virtual Boy
N64|Nintendo - Nintendo 64
NDS|Nintendo - Nintendo DS
SMS|Sega - Master System - Mark III
GG|Sega - Game Gear
MD|Sega - Mega Drive - Genesis
32X|Sega - 32X
SEGACD|Sega - Mega-CD - Sega CD
SG1000|Sega - SG-1000
DC|Sega - Dreamcast
PS|Sony - PlayStation
PCE|NEC - PC Engine - TurboGrafx 16
A2600|Atari - 2600
A5200|Atari - 5200
A7800|Atari - 7800
LYNX|Atari - Lynx
COLECO|Coleco - ColecoVision
NGP|SNK - Neo Geo Pocket
NGPC|SNK - Neo Geo Pocket Color
FBN|FBNeo - Arcade Games
MSX|Microsoft - MSX - MSX2 - MSX2P - MSX Turbo R
PRBOOM|PrBoom"

# --- progress helpers (show2 daemon; best-effort FIFO writes) --------------
FIFO=/tmp/show2.fifo
prog_start() {
    killall -9 show2.elf >/dev/null 2>&1
    rm -f "$FIFO"
    show2.elf --mode=daemon --image=/dev/null --texty=45 --progressy=55 --fontsize=28 &
    sleep 1
}
prog() { echo "TEXT:$1" > "$FIFO" 2>/dev/null; [ -n "$2" ] && echo "PROGRESS:$2" > "$FIFO" 2>/dev/null; }
prog_stop() { echo "QUIT" > "$FIFO" 2>/dev/null; killall -9 show2.elf >/dev/null 2>&1; }
# One-shot timed message (its own show2 invocation, blocks for --timeout).
msg() { killall -9 show2.elf >/dev/null 2>&1; show2.elf --mode=progress --image=/dev/null --text="$1" --progress=-1 --texty=45 --progressy=55 --fontsize=28 --timeout="${2:-3}"; }

extract_folder() { # zip "folder" destdir
    case "$NX_EXTRAS_UNZIP" in
        *7zzs*) "$NX_EXTRAS_UNZIP" e -y -o"$3" "$1" "$2/*.cht" >/dev/null 2>&1 ;;
        *)      "$NX_EXTRAS_UNZIP" -j -o -q "$1" "$2/*.cht" -d "$3" >/dev/null 2>&1 ;;
    esac
    return 0
}
list_folder() { # zip "folder" -> basenames from the archive index
    case "$NX_EXTRAS_UNZIP" in
        *7zzs*) "$NX_EXTRAS_UNZIP" l "$1" "$2/*.cht" 2>/dev/null ;;
        *)      "$NX_EXTRAS_UNZIP" -l "$1" "$2/*.cht" 2>/dev/null ;;
    esac | grep '\.cht$' | sed 's#.*/##'
}

remote_lastmod() {
    wget -S --spider --no-check-certificate --timeout=30 --tries=1 "$CHEATS_URL" 2>&1 \
        | sed -n 's/.*[Ll]ast-[Mm]odified: *//p' | head -1 | tr -d '\r'
}

db_installed() { [ -s "$MANIFEST" ]; }

do_download() { # downloads + extracts everything; returns 0 on success
    avail_kb="$(df -k "$SDCARD_PATH" 2>/dev/null | awk 'END{print $4}')"
    case "$avail_kb" in
        ''|*[!0-9]*) : ;;
        *) [ "$avail_kb" -ge "$NEED_KB" ] || { msg "Not enough space (need ~256 MB free)." 4; return 1; } ;;
    esac
    command -v "$NX_EXTRAS_UNZIP" >/dev/null 2>&1 || { msg "System unzip tool missing." 4; return 1; }

    rm -rf "$TMPDIR_NX"
    mkdir -p "$TMPDIR_NX" "$STATE_DIR" || { msg "Cannot create install dirs." 4; return 1; }

    prog_start
    prog "Downloading cheat database (~37 MB)..." "-1"
    if ! wget --no-check-certificate -q --timeout=30 --tries=2 -O "$ZIP" "$CHEATS_URL"; then
        prog_stop; rm -rf "$TMPDIR_NX"; msg "Download failed (check WiFi)." 4; return 1
    fi
    prog "Verifying archive..." "-1"
    case "$NX_EXTRAS_UNZIP" in
        *7zzs*) "$NX_EXTRAS_UNZIP" t "$ZIP" >/dev/null 2>&1 || { prog_stop; rm -rf "$TMPDIR_NX"; msg "Downloaded archive is corrupt." 4; return 1; } ;;
        *)      "$NX_EXTRAS_UNZIP" -tq "$ZIP" >/dev/null 2>&1 || { prog_stop; rm -rf "$TMPDIR_NX"; msg "Downloaded archive is corrupt." 4; return 1; } ;;
    esac

    : > "$MANIFEST"
    total="$(printf '%s\n' "$MAP" | wc -l | tr -d ' ')"
    idx=0
    OLDIFS="$IFS"; IFS='
'
    for line in $MAP; do
        IFS="$OLDIFS"
        idx=$((idx + 1))
        tag="${line%%|*}"; folder="${line#*|}"
        pct=$((idx * 100 / total))
        prog "Installing $tag ($idx/$total)" "$pct"
        dest="$CHEATS_DIR/$tag"
        mkdir -p "$dest"
        extract_folder "$ZIP" "$folder" "$dest"
        list_folder "$ZIP" "$folder" | while IFS= read -r b; do
            [ -n "$b" ] && printf '%s\n' "$dest/$b" >> "$MANIFEST"
        done
        IFS='
'
    done
    IFS="$OLDIFS"

    lm="$(remote_lastmod)"
    [ -n "$lm" ] || lm="$(date -u '+%Y-%m-%d')"
    printf '%s\n' "$lm" > "$DBVER"

    rm -rf "$TMPDIR_NX"
    prog_stop
    msg "Done. Open a game and see Options > Cheats." 3
    return 0
}

do_update() {
    msg "Checking for updates..." 1 &
    lm="$(remote_lastmod)"
    killall -9 show2.elf >/dev/null 2>&1
    if [ -z "$lm" ]; then msg "Could not check (WiFi?)." 4; return; fi
    cur="$(cat "$DBVER" 2>/dev/null)"
    if [ "$lm" = "$cur" ]; then msg "Cheat database is up to date." 3; else do_download; fi
}

do_remove() {
    if [ -f "$MANIFEST" ]; then
        dirs=""
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            rm -f "$p"
            d="$(dirname "$p")"
            case "$dirs" in *"|$d|"*) : ;; *) dirs="$dirs|$d|" ;; esac
        done < "$MANIFEST"
        OLDIFS="$IFS"; IFS='|'
        for d in $dirs; do [ -n "$d" ] && [ -d "$d" ] && rmdir "$d" 2>/dev/null; done
        IFS="$OLDIFS"
    fi
    rm -f "$MANIFEST" "$DBVER"
    msg "Cheat database removed. Hand-made cheats kept." 3
}

# --- menu loop -------------------------------------------------------------
while :; do
    set --
    if db_installed; then
        set -- "$@" --entry "Check for updates" update --entry "Remove cheat database" remove
    else
        set -- "$@" --entry "Download cheat database" download
    fi
    CHOSEN="$(options.elf --pick --title "Cheat Database" "$@" 2>>"$LOGS_PATH/cheatdb.txt")" || break
    [ -n "$CHOSEN" ] || break
    case "$CHOSEN" in
        download) do_download ;;
        update)   do_update ;;
        remove)   do_remove ;;
    esac
done
exit 0
