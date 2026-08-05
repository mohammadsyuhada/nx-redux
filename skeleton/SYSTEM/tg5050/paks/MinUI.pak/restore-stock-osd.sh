#!/bin/sh
# Restore the stock /usr/trimui/osd on the rootfs from the SD card's pristine
# per-model archive (shipped at $SYSTEM_PATH/osd-stock/$DEVICE.zip, extracted
# from the model's recovery image, minus regular.ttf which NX never touches).
# Also reverts the nx_skip_stock_sshd boot-time patch in /etc/init.d/rcS.
#
# The live OSD is unaffected: it is served from the SD card through the
# read-only overlay mount, so this only cleans what sits underneath. While
# that mount is up the real rootfs directory is shadowed, so it is reached
# through a temporary bind mount of / (a bind of / replicates the root
# filesystem without submounts).
#
# usage: restore-stock-osd.sh [device]   (falls back to $DEVICE from the env)
# exit 0 on success, 1 on any failure.

DEVICE="${1:-$DEVICE}"
SYSTEM_PATH="$(cd "$(dirname "$0")/../.." && pwd)"
STOCK="$SYSTEM_PATH/osd-stock/$DEVICE.zip"
OSD_DST="/usr/trimui/osd"

[ -n "$DEVICE" ] || exit 1
[ -f "$STOCK" ] || exit 1

# Prefer the Info-Zip `unzip` every installed card already carries at
# <SDCARD>/.tmp_update/<platform>/unzip (the updater's own vendored binary,
# see workspace/tg5040/install/boot.sh); fall back to whatever `unzip` is on
# PATH (e.g. busybox's applet) if that vendored copy isn't there or isn't
# executable. The .tmp_update tree keeps its per-platform layout, so the
# platform is spelled out here — $SYSTEM_PATH (now .system) no longer carries it.
SDCARD="$(cd "$SYSTEM_PATH/.." && pwd)"
UNZIP_BIN="$SDCARD/.tmp_update/tg5050/unzip"
[ -x "$UNZIP_BIN" ] || UNZIP_BIN="unzip"

# refuse before touching the rootfs if the archive is truncated/unreadable —
# vfat can't carry exec bits, so listing trimui_osdd (not testing -x) is the
# meaningful invariant here
"$UNZIP_BIN" -l "$STOCK" 2> /dev/null | grep -q 'trimui_osdd' || exit 1

# boot already left / rw; re-assert in case the boot flow ever changes
mount -o remount,rw / 2> /dev/null

# Revert the nx_skip_stock_sshd boot patch in /etc/init.d/rcS (applied every
# boot by MinUI.pak/launch.sh). Prefer the pristine backup; fall back to
# stripping the tagged lines so a missing backup can't leave the patch
# behind. Runs before the OSD work on purpose: even if the unzip below
# fails, the boot script is already back to stock, and the revert is
# idempotent across retries. Note the next NX boot re-applies the patch —
# restoring to stock only sticks if the card is removed afterwards.
if grep -q nx_skip_stock_sshd /etc/init.d/rcS 2> /dev/null; then
	if [ -f /etc/init.d/rcS.nxbak ] && ! grep -q nx_skip_stock_sshd /etc/init.d/rcS.nxbak; then
		cp -f /etc/init.d/rcS.nxbak /etc/init.d/rcS
	else
		sed -i -e '/nx_skip_stock_sshd/d' \
			-e '/waiting on kernel entropy/d' \
			-e '/"S50sshd" \] && continue/d' /etc/init.d/rcS
	fi
	chmod 755 /etc/init.d/rcS
fi
rm -f /etc/init.d/rcS.nxbak

REAL="$OSD_DST"
BIND=""
if grep -q " $OSD_DST overlay " /proc/mounts; then
	BIND="/tmp/nx_rootfs_bind"
	# a prior run may have crashed after mounting but before cleanup; clear
	# any leaked mount first so we don't stack a bind on top of it
	if grep -q " $BIND " /proc/mounts; then
		umount "$BIND" || exit 1
	fi
	mkdir -p "$BIND"
	mount --bind / "$BIND" || exit 1
	REAL="$BIND$OSD_DST"
fi

fail() {
	[ -n "$BIND" ] && umount "$BIND"
	exit 1
}

# one pass removes NX files, .nx_osd_stamp and any stale residue; the
# firmware font is the only survivor (the stock archive doesn't ship it)
find "$REAL" -mindepth 1 -maxdepth 1 ! -name regular.ttf -exec rm -rf {} + || fail
"$UNZIP_BIN" -oq "$STOCK" -d "$REAL" || fail
# the SD card's vfat has no real permission bits; re-assert exec on rootfs
chmod +x "$REAL/trimui_osdd" "$REAL"/*.sh "$REAL"/widgets/*/*.sh 2> /dev/null
# exit 0 must mean a runnable stock OSD, not just copied bytes
[ -x "$REAL/trimui_osdd" ] || fail

sync
if [ -n "$BIND" ]; then
	# the rootfs restore above already succeeded, but a leaked bind mount
	# of / is itself a real failure state the caller must see
	umount "$BIND" || exit 1
fi
exit 0
