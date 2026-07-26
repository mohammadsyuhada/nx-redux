#!/bin/sh
# Extract the pristine stock /usr/trimui/osd tree from a Trimui sd_recovery
# image into skeleton/SYSTEM/osd-stock/<device>.zip (excluding regular.ttf,
# the 16MB CJK font NX never touches), and write an md5 manifest alongside
# describing the zip's contents (paths relative to the archive root).
#
# Must run on Linux (debugfs). From the repo root on macOS:
#   docker run --rm -v ~/Downloads:/downloads -v "$PWD":/repo alpine sh -c \
#     'apk add -q e2fsprogs e2fsprogs-extra p7zip python3 zip && \
#      /repo/scripts/extract-stock-osd.sh /downloads/<archive> <device> /repo'
#
# usage: extract-stock-osd.sh <recovery-archive-or-img> <device> [repo_root]
#
# Some recovery images (observed on tg5050/smartpros) are not a raw
# partitioned disk at all — they're an Allwinner "IMAGEWTY" firmware
# container (LiveSuit/PhoenixSuit format), with the real rootfs nested
# inside as a partition that is itself an Android *sparse* image. This
# script detects that case and stops with an explanatory error (see the
# IMAGEWTY check below) rather than silently failing the ext4 scan — it
# does NOT try to auto-unpack IMAGEWTY, since that needs a purpose-built
# tool (OpenixCard) that isn't worth vendoring here for a one-off extract.
# Manual pre-step used to unpack tg5050/smartpros (Alpine/aarch64 host):
#
#   # 1. Build OpenixCard (a debian container is much easier than alpine
#   #    here — it needs cmake/g++/libconfuse-dev; its own pinned submodule
#   #    ref for lib/inicpp was gone upstream, so clone that submodule's
#   #    HEAD directly instead of the broken pinned commit):
#   docker run --rm -v <workdir>:/build debian:bookworm sh -c '
#     apt-get update -qq && apt-get install -qq -y git cmake build-essential \
#       automake autoconf libconfuse-dev pkg-config libconfuse2
#     cd /build && git clone https://github.com/YuzukiTsuru/OpenixCard
#     cd OpenixCard && git submodule update --init lib/ColorCout lib/argparse \
#       lib/cpp-subprocess lib/ftxui
#     rm -rf lib/inicpp && git clone --depth 50 https://github.com/SemaiCZE/inicpp lib/inicpp
#     mkdir build && cd build && cmake .. && make -j'
#
#   # 2. Locate the IMAGEWTY container inside the recovery .img (it is NOT
#   #    necessarily at offset 0 — on the tg5050 image it started 68MiB in,
#   #    after a leading raw GPT/boot region) and carve it out from there
#   #    to end-of-file, then unpack+dump it to a flashable raw disk image:
#   grep -abom1 IMAGEWTY recovery.img          # -> "<offset>:IMAGEWTY"
#   dd if=recovery.img of=imagewty.img bs=1M skip=<offset/1MiB>
#   OpenixCard -d imagewty.img                 # -> imagewty.img.dump.out/imagewty.img
#
#   # 3. That dumped image has a normal GPT with a "rootfs" partition — but
#   #    its content is an Android sparse image (magic 0xed26ff3a at the
#   #    partition's very start), not raw ext4. Carve out that partition
#   #    (fdisk -l to get its start/size in sectors) and convert it:
#   fdisk -l imagewty.img.dump.out/imagewty.img
#   dd if=imagewty.img.dump.out/imagewty.img of=rootfs.sparse bs=512 skip=<start> count=<sectors>
#   simg2img rootfs.sparse rootfs_raw.img       # apt install android-sdk-libsparse-utils
#
#   # 4. rootfs_raw.img is now a bare raw ext4 image with the superblock at
#   #    its own offset 0 — pass it straight to this script as the *.img
#   #    input (the scan below checks offset 0 on its very first iteration,
#   #    same as any other candidate offset):
#   extract-stock-osd.sh rootfs_raw.img smartpros /repo
set -e

ARCHIVE="$1"
DEV="$2"
REPO="${3:-.}"

if [ -z "$ARCHIVE" ] || [ -z "$DEV" ]; then
	echo "usage: $0 <archive|img> <device> [repo_root]" >&2
	exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

case "$ARCHIVE" in
	*.zip) 7z x -o"$WORK" "$ARCHIVE" > /dev/null; IMG=$(find "$WORK" -name '*.img' | head -1) ;;
	*.7z)  7z x -o"$WORK" "$ARCHIVE" > /dev/null; IMG=$(find "$WORK" -name '*.img' | head -1) ;;
	*.img) IMG="$ARCHIVE" ;;
	*)     echo "unsupported archive: $ARCHIVE" >&2; exit 1 ;;
esac
if [ ! -f "$IMG" ]; then
	echo "no .img found in $ARCHIVE" >&2
	exit 1
fi

# Find the rootfs: scan for ext4 superblocks (magic 0xEF53 at byte 56 of the
# superblock, which sits 1024 bytes into its partition) and keep candidates
# over 100MB — boot partitions are far smaller than the ~540MB rootfs.
OFFSETS=$(python3 - "$IMG" <<'EOF'
import sys, mmap, struct
with open(sys.argv[1], 'rb') as f:
    m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    off = 0
    while off + 4096 < len(m):
        sb = off + 1024
        if m[sb+56:sb+58] == b'\x53\xef':
            blocks, = struct.unpack('<I', m[sb+4:sb+8])
            log_bs, = struct.unpack('<I', m[sb+24:sb+28])
            # log_bs (s_log_block_size) is 0-6 on any real ext4 filesystem;
            # a coincidental 0xEF53 hit in binary data can leave log_bs as
            # arbitrary garbage, and shifting 1024 by a huge garbage value
            # produces a multi-gigabyte-digit int that is absurdly slow to
            # compute/print. Reject those before doing the shift.
            if log_bs <= 16:
                size = blocks * (1024 << log_bs)
                if size > 100 * 1024 * 1024:
                    print(off, size)
        off += 512
EOF
)
if [ -z "$OFFSETS" ]; then
	if grep -qm1 IMAGEWTY "$IMG"; then
		echo "error: no ext4 rootfs candidate found in $IMG, and it contains an Allwinner" >&2
		echo "IMAGEWTY firmware-container marker (LiveSuit/PhoenixSuit format). Note: that" >&2
		echo "string alone isn't conclusive on its own (it can show up incidentally, e.g. in" >&2
		echo "bootloader strings, even on images the ext4 scan handles fine) — but combined" >&2
		echo "with zero ext4 candidates here, this image is likely a real IMAGEWTY container" >&2
		echo "whose rootfs is a partition nested inside that is itself an Android sparse" >&2
		echo "image, which a plain ext4-superblock scan cannot find directly. See the manual" >&2
		echo "OpenixCard-based pre-step documented in the header comment above; once you have" >&2
		echo "a bare, non-sparse raw ext4 rootfs image, re-run this script against it directly" >&2
		echo "as the *.img input." >&2
	else
		echo "no ext4 rootfs candidate found in $IMG" >&2
	fi
	exit 1
fi

OUT="$REPO/skeleton/SYSTEM/osd-stock/$DEV"
ZIP="$REPO/skeleton/SYSTEM/osd-stock/$DEV.zip"
MANIFEST="$REPO/skeleton/SYSTEM/osd-stock/$DEV.manifest.md5"
rm -rf "$OUT" "$ZIP" "$MANIFEST"
mkdir -p "$(dirname "$OUT")"

echo "$OFFSETS" | while read -r OFF SIZE; do
	PART="$WORK/rootfs.img"
	dd if="$IMG" of="$PART" bs=512 skip=$((OFF / 512)) count=$((SIZE / 512)) status=none
	if debugfs -R 'ls /usr/trimui/osd' "$PART" > /dev/null 2>&1; then
		debugfs -R "rdump /usr/trimui/osd $(dirname "$OUT")" "$PART" 2> /dev/null
		mv "$(dirname "$OUT")/osd" "$OUT"
		rm -f "$OUT/regular.ttf"
		(cd "$OUT" && find . -type f | sort | xargs md5sum) > "$MANIFEST"
		# repo artifact is the zip, not the loose tree -- members sit at the
		# archive root so the on-device restore can unzip straight into place
		(cd "$OUT" && zip -rqX "$ZIP" . -x '*.DS_Store')
		rm -rf "$OUT"
		echo "extracted $DEV from offset $OFF ($(du -sh "$ZIP" | cut -f1))"
		break
	fi
done

if [ ! -f "$MANIFEST" ]; then
	echo "no candidate partition contained /usr/trimui/osd" >&2
	exit 1
fi
