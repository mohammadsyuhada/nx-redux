#!/bin/sh
# Xtras catalog: libretro cheat database.
# Contract: idempotent; installs from buildbot's cheats.zip (no GitHub release,
# so this entry is untracked - re-running just refreshes); progress on stdout
# ("@NN status text" hints - see gen1recomp/install.sh's header for the full
# contract); exit code is the verdict; every network command carries a timeout
# (the caller streams stdout via a blocking popen()/fgets() loop with no
# watchdog). The runner exports SDCARD_PATH/PLATFORM/LOGS_PATH/XTRAS_STATE_DIR/
# CATALOG_DIR; CHEATS_PATH is NOT exported, so derive it.
#
# What it does: download cheats.zip, then for each (TAG -> libretro folder) in
# the map, extract that folder's *.cht FLAT into Cheats/<TAG>/, recording every
# file it writes in a manifest so uninstall removes only the pack's files and
# leaves hand-authored .cht files alone. Standalone-emulator tags (NDS/N64/DC)
# receive files too, for later experimentation; minarch applies cheats for the
# libretro cores only.
set -u

CHEATS_URL="https://buildbot.libretro.com/assets/frontend/cheats.zip"
: "${NX_EXTRAS_UNZIP:=$SDCARD_PATH/.system/shared/bin/7zzs.aarch64}"
: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
TMPDIR_NX="$SDCARD_PATH/.extras_tmp"
CHEATS_DIR="$SDCARD_PATH/Cheats"
MANIFEST="$XTRAS_STATE_DIR/cheatdb.manifest"
ZIP="$TMPDIR_NX/cheats.zip"
NEED_KB=262144   # ~256 MB: the 37 MB zip + ~173 MB of extracted text + margin

# TAG|libretro-folder map (31 tags, 28 distinct folders). One folder can feed
# several tags (SNES -> SFC/SUPA, Game Boy -> GB/SGB, GBA -> GBA/MGBA).
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

fail() {
    echo "ERROR: $1"
    rm -rf "$TMPDIR_NX"
    exit 1
}

# Extract one libretro folder's *.cht FLAT (no path prefix) into a dir.
extract_folder() { # zip "libretro folder" destdir
    case "$NX_EXTRAS_UNZIP" in
        *7zzs*) "$NX_EXTRAS_UNZIP" e -y -o"$3" "$1" "$2/*.cht" >/dev/null 2>&1 || return 1 ;;
        *)      "$NX_EXTRAS_UNZIP" -j -o -q "$1" "$2/*.cht" -d "$3" >/dev/null 2>&1 || return 1 ;;
    esac
    return 0
}

# ---- preflight -----------------------------------------------------------
if ! command -v "$NX_EXTRAS_UNZIP" >/dev/null 2>&1; then
    fail "system unzip tool missing - update or reinstall NX Redux, then retry"
fi
avail_kb="$(df -k "$SDCARD_PATH" 2>/dev/null | awk 'END{print $4}')"
case "$avail_kb" in
    ''|*[!0-9]*) : ;;                                   # unknown -> don't block
    *) [ "$avail_kb" -ge "$NEED_KB" ] || fail "not enough space (need ~256 MB free)" ;;
esac

rm -rf "$TMPDIR_NX"
mkdir -p "$TMPDIR_NX" || fail "cannot create install dirs"
mkdir -p "$XTRAS_STATE_DIR" || fail "cannot create state dir"

echo "@5 Downloading cheat database..."
echo "Downloading cheat database (~37 MB)..."
wget --no-check-certificate -q --timeout=30 --tries=2 -O "$ZIP" "$CHEATS_URL" \
    || fail "download failed (check WiFi)"

echo "@18 Verifying archive..."
case "$NX_EXTRAS_UNZIP" in
    *7zzs*) "$NX_EXTRAS_UNZIP" t "$ZIP" >/dev/null 2>&1 || fail "downloaded archive is corrupt" ;;
    *)      "$NX_EXTRAS_UNZIP" -tq "$ZIP" >/dev/null 2>&1 || fail "downloaded archive is corrupt" ;;
esac

# Fresh manifest each successful install (rebuilt below).
: > "$MANIFEST" || fail "cannot write manifest"

# ---- extract each mapped system -----------------------------------------
total="$(printf '%s\n' "$MAP" | wc -l | tr -d ' ')"
idx=0
OLDIFS="$IFS"
IFS='
'
for line in $MAP; do
    IFS="$OLDIFS"
    idx=$((idx + 1))
    tag="${line%%|*}"
    folder="${line#*|}"
    pct=$((20 + (idx * 75 / total)))
    echo "@$pct $tag ($idx/$total)"

    dest="$CHEATS_DIR/$tag"
    mkdir -p "$dest" || fail "cannot create $dest"
    td="$TMPDIR_NX/x"
    rm -rf "$td"; mkdir -p "$td"
    extract_folder "$ZIP" "$folder" "$td"   # missing folder -> empty, not fatal

    for f in "$td"/*.cht; do
        [ -e "$f" ] || continue
        b="$(basename "$f")"
        # mv (same filesystem) is a cheap rename - no doubled space
        mv -f "$f" "$dest/$b" || fail "could not place $tag/$b"
        printf '%s\n' "$dest/$b" >> "$MANIFEST"
    done
    IFS='
'
done
IFS="$OLDIFS"

# ---- version record (buildbot has no release tag; store Last-Modified) ---
echo "@97 Finishing..."
lastmod="$(wget -S --spider --no-check-certificate --timeout=30 --tries=1 "$CHEATS_URL" 2>&1 \
           | sed -n 's/.*[Ll]ast-[Mm]odified: *//p' | head -1 | tr -d '\r')"
[ -n "$lastmod" ] || lastmod="$(date -u '+%Y-%m-%d')"
printf '%s\n' "$lastmod" > "$XTRAS_STATE_DIR/cheatdb.version" || fail "could not write version record"

rm -rf "$TMPDIR_NX"
echo "@100 Done"
echo "Done. Open a game and see Options > Cheats."
exit 0
