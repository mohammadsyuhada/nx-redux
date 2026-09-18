#!/usr/bin/env bash
# Derive the Xbox-button-layout variant of a device's stock TrimUI OSD daemon.
#
#   scripts/patch-osdd-layout.sh <brick|brickpro|smartpro|smartpros> [src] [dst]
#
# Default src: skeleton/SYSTEM/osd/device/<dev>/trimui_osdd
# Default dst: skeleton/SYSTEM/osd/device/<dev>/trimui_osdd.xbox
#
# trimui_osdd is closed source. Its input thread (do_osd_input_thread) turns
# SDL joystick button 0 (bottom, physical B) into OSD key 2 = cancel and
# button 1 (right, physical A) into key 1 = OK; buttons 2/3 map to keys the
# OSD ignores. Under Settings > System > Button layout = Xbox the launcher
# treats bottom as A/confirm, so the OSD must too: this script swaps that
# mapping with a handful of instruction edits (see .dev/OSD.md, "Button
# layout variant"). MinUI.pak/launch.sh starts trimui_osdd.xbox instead of
# trimui_osdd when the setting is on.
#
# The source daemons are themselves patched (slider icon/bar insets, see
# .dev/OSD.md "Slider layout patch"), so the md5s below are those of the
# checked-in device/<dev>/trimui_osdd, not the stock firmware binaries.
#
# Safety: the source md5 must match the build this table was derived from,
# and every original 4-byte instruction is verified before anything is
# written, so a different firmware build fails loudly instead of producing a
# daemon that maps buttons to nonsense.
#
# Byte strings are little-endian file order, exactly as `xxd -p` prints them.
set -euo pipefail

DEV="${1:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${2:-$ROOT/skeleton/SYSTEM/osd/device/$DEV/trimui_osdd}"
DST="${3:-$ROOT/skeleton/SYSTEM/osd/device/$DEV/trimui_osdd.xbox}"

case "$DEV" in
brick)
    MD5=a18d25dc01d78141c5c48c98804db1ba
    # 0x40b8ac b.eq  0x40b8c4 -> 0x40b9ac   button 1 -> cancel (key 2)
    # 0x40b8b0 cbz w0,0x40b9ac -> 0x40b9b8  button 0 -> key block
    # 0x40b8b8 b.eq  0x40b9b8 -> 0x40b810   button 2 -> ignored (loop head)
    # 0x40b9b8 mov w0,#4 -> mov w0,#1       key block now yields OK
    PATCHES="b8ac:c0000054:00080054 b8b0:e0070034:40080034 b8b8:00080054:c0faff54 b9b8:80008052:20008052"
    ;;
brickpro)
    MD5=0ff776f3dcdd76a0d0ff1167bc5c72fd
    # Brick daemon + the Home-closes-OSD patch (cmp w0,#15 at 0x40b8b4).
    # Same as brick, except button 15 (Home) keeps cancelling via 0x40b9ac
    # and the key block's constant was already 2 (Home) and becomes 1 (OK).
    PATCHES="b8ac:c0000054:00080054 b8b0:e0070034:40080034 b8b8:00080054:a0070054 b9b8:40008052:20008052"
    ;;
smartpro)
    MD5=56a429d4c9d059587d51911dbb4164f6
    # Identical code to brick, linked 0xb0 lower (0x40b7fc/0x40b800/0x40b808/0x40b908).
    PATCHES="b7fc:c0000054:00080054 b800:e0070034:40080034 b808:00080054:c0faff54 b908:80008052:20008052"
    ;;
smartpros)
    MD5=dc406fc92ba4cc517651a8c6f7352825
    # PIE build: file offset == vaddr. One instruction picks key 2-or-1 by
    # "button == 0": csel w0,w0,w1,ne -> csel w0,w0,w1,eq.
    PATCHES="12f0c:0010811a:0000811a"
    ;;
*)
    echo "usage: $0 <brick|brickpro|smartpro|smartpros> [src] [dst]" >&2
    exit 2
    ;;
esac

md5_of() {
    if command -v md5sum >/dev/null 2>&1; then md5sum "$1" | cut -d' ' -f1
    else md5 -q "$1"; fi
}

[ -f "$SRC" ] || { echo "patch-osdd-layout: $SRC not found" >&2; exit 1; }
have=$(md5_of "$SRC")
if [ "$have" != "$MD5" ]; then
    echo "patch-osdd-layout: $DEV source md5 $have != expected $MD5 (different firmware build?)" >&2
    exit 1
fi

# Verify every original instruction before touching anything.
for p in $PATCHES; do
    off="${p%%:*}"; rest="${p#*:}"; old="${rest%%:*}"
    got=$(xxd -s "$((16#$off))" -l 4 -p "$SRC")
    if [ "$got" != "$old" ]; then
        echo "patch-osdd-layout: $DEV bytes at 0x$off are $got, expected $old" >&2
        exit 1
    fi
done

tmp="$DST.tmp.$$"
cp "$SRC" "$tmp"
for p in $PATCHES; do
    off="${p%%:*}"; rest="${p#*:}"; new="${rest#*:}"
    # 4 hex bytes -> raw, via octal escapes (portable printf)
    printf "$(printf '%s' "$new" | sed 's/../&\n/g' | sed '/^$/d' | while read -r b; do printf '\\%03o' "$((16#$b))"; done)" \
        | dd of="$tmp" bs=1 seek="$((16#$off))" conv=notrunc status=none 2>/dev/null \
        || printf "$(printf '%s' "$new" | sed 's/../&\n/g' | sed '/^$/d' | while read -r b; do printf '\\%03o' "$((16#$b))"; done)" \
        | dd of="$tmp" bs=1 seek="$((16#$off))" conv=notrunc 2>/dev/null
done
chmod 755 "$tmp"
mv -f "$tmp" "$DST"
echo "patch-osdd-layout: wrote $DST ($(md5_of "$DST"))"
