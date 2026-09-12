#!/usr/bin/env bash
# Host test for the tg5050 OSD overlay-mount block in
# skeleton/SYSTEM/tg5050/paks/MinUI.pak/launch.sh (the `^OSD_DST=` ...
# `fi # end osd overlay mount` region).
#
# The block is run verbatim under busybox sh in a privileged Linux container
# with real loop, overlay and tmpfs mounts, once per SD-card filesystem:
#
#   vfat  - a FAT32 card. Linux overlayfs refuses vfat as a layer (its
#           dentries carry custom hash/compare ops, "filesystem on ... not
#           supported"), so the SD tree can't be mounted directly; the block
#           has to stage it into tmpfs first. This is issue #87.
#   ext4  - stands in for the exFAT card the firmware mounts through FUSE,
#           which overlayfs accepts; the direct SD mount must keep working
#           and must not fall back to staging.
#
# Either way the merged /usr/trimui/osd must show the SD tree on top, the
# theme-tint layer above that, and rootfs-only files underneath.
# Needs docker.
set -euo pipefail
cd "$(dirname "$0")/../.."

LAUNCH=skeleton/SYSTEM/tg5050/paks/MinUI.pak/launch.sh
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

sed -n '/^OSD_DST=/,/^fi # end osd overlay mount/p' "$LAUNCH" > "$TMP/osd-block.sh"
grep -q '^OSD_DST=' "$TMP/osd-block.sh" || { echo "FAIL: OSD block not found in $LAUNCH"; exit 1; }
grep -q 'end osd overlay mount' "$TMP/osd-block.sh" || { echo "FAIL: OSD block end anchor missing"; exit 1; }

cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
# Runs inside the container. $1 = filesystem for the fake SD card.
set -e
FS="$1"
apk add -q dosfstools e2fsprogs > /dev/null 2>&1

fail() { echo "FAIL [$FS]: $*"; exit 1; }

# fake SD card
IMG=/sd.img
dd if=/dev/zero of="$IMG" bs=1M count=32 status=none
case "$FS" in
	vfat) mkfs.vfat "$IMG" > /dev/null ;;
	ext4) mkfs.ext4 -q "$IMG" ;;
esac
mkdir -p /mnt/SDCARD
mount -t "$FS" -o loop "$IMG" /mnt/SDCARD
export SYSTEM_PATH=/mnt/SDCARD/.system
mkdir -p "$SYSTEM_PATH/osd/widgets/toggle_wifi" "$SYSTEM_PATH/bin"
echo nx > "$SYSTEM_PATH/osd/osdlayout.json"
echo shipped > "$SYSTEM_PATH/osd/block1x1_sel.png"
printf '#!/bin/sh\necho nx-daemon\n' > "$SYSTEM_PATH/osd/trimui_osdd"
printf '#!/bin/sh\necho nx-widget\n' > "$SYSTEM_PATH/osd/widgets/toggle_wifi/set.sh"
# stand-in for osdmusic.elf --tint-osd <src> <dst>: emit one recoloured file
cat > "$SYSTEM_PATH/bin/osdmusic.elf" <<'STUB'
#!/bin/sh
[ "$1" = "--tint-osd" ] || exit 1
echo tinted > "$3/block1x1_sel.png"
STUB
# The firmware's card mounts (vfat, exfat-FUSE) present every file as 0755;
# ext4 needs the bits set explicitly to stand in for that.
chmod +x "$SYSTEM_PATH/bin/osdmusic.elf" "$SYSTEM_PATH/osd/trimui_osdd" \
	"$SYSTEM_PATH/osd/widgets/toggle_wifi/set.sh" 2> /dev/null || true

# fake firmware rootfs OSD tree
mkdir -p /usr/trimui/osd/widgets/toggle_wifi
echo stock > /usr/trimui/osd/osdlayout.json
echo stock > /usr/trimui/osd/block1x1_sel.png
echo font > /usr/trimui/osd/regular.ttf
printf '#!/bin/sh\necho stock-daemon\n' > /usr/trimui/osd/trimui_osdd
chmod +x /usr/trimui/osd/trimui_osdd
rm -f /tmp/nx_osd_mount_failed

sh /osd-block.sh

[ ! -f /tmp/nx_osd_mount_failed ] || fail "mount failure marker set"
grep -q " /usr/trimui/osd overlay " /proc/mounts || fail "/usr/trimui/osd is not an overlay"
[ "$(cat /usr/trimui/osd/osdlayout.json)" = nx ] || fail "layout is $(cat /usr/trimui/osd/osdlayout.json), SD tree not on top"
[ "$(cat /usr/trimui/osd/block1x1_sel.png)" = tinted ] || fail "tint layer not on top"
[ "$(cat /usr/trimui/osd/regular.ttf)" = font ] || fail "rootfs-only file does not show through"
[ "$(cd /usr/trimui/osd && ./trimui_osdd)" = nx-daemon ] || fail "SD daemon not executable from merged tree"
[ "$(/usr/trimui/osd/widgets/toggle_wifi/set.sh)" = nx-widget ] || fail "SD widget script not executable from merged tree"
case "$FS" in
	ext4) [ ! -d /tmp/nx_osd ] || fail "direct SD mount was expected, but the tree was staged" ;;
esac
echo "PASS [$FS]"
EOF

for fs in vfat ext4; do
	docker run --rm --privileged \
		-v "$TMP/osd-block.sh:/osd-block.sh:ro" -v "$TMP/run.sh:/run.sh:ro" \
		alpine:3.19 sh /run.sh "$fs"
done
