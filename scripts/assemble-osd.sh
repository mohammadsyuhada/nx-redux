#!/bin/sh
# Assemble one device's OSD tree from the layered source in skeleton/SYSTEM/osd.
#
# Layers compose by last-wins recursive copy:
#   common/          device-agnostic (widgets, icons, progress art, key.wav)
#   res/<WxH>/       anything sized to the panel (bg.png, block*.png,
#                    show_*.sh toast scripts)
#   device/<dev>/    trimui_osdd, osdlayout.json, plus any override
#
# The output shape is dictated by what each platform's launch.sh reads and must
# not be changed without changing launch.sh:
#   tg5040   layered  osd/ (common) + osd-$DEVICE/ (res + device)
#   tg5050   flat     osd/ (common + res + device)
#
# usage: assemble-osd.sh <device> <platform> <osd_res> <dest_system_dir> [src_root]
set -e

DEVICE="$1"
PLATFORM="$2"
OSD_RES="$3"
DEST="$4"
SRC="${5:-skeleton/SYSTEM/osd}"

if [ -z "$DEVICE" ] || [ -z "$PLATFORM" ] || [ -z "$OSD_RES" ] || [ -z "$DEST" ]; then
	echo "usage: $0 <device> <platform> <osd_res> <dest_system_dir> [src_root]" >&2
	exit 1
fi

for layer in "$SRC/common" "$SRC/res/$OSD_RES" "$SRC/device/$DEVICE"; do
	if [ ! -d "$layer" ]; then
		echo "assemble-osd: missing layer $layer" >&2
		exit 1
	fi
done

shared_out="$DEST/$PLATFORM/osd"
if [ "$PLATFORM" = "tg5050" ]; then
	model_out="$shared_out"
else
	model_out="$DEST/$PLATFORM/osd-$DEVICE"
fi
mkdir -p "$shared_out" "$model_out"

cp -R "$SRC/common/."         "$shared_out/"
cp -R "$SRC/res/$OSD_RES/."   "$model_out/"
cp -R "$SRC/device/$DEVICE/." "$model_out/"

# Widget scripts carry a __PLATFORM__ placeholder instead of a hardcoded
# tg5040/tg5050 path, so this is resolved here at assembly time rather than
# via a runtime $SYSTEM_PATH: trimui_osdd is closed-source and it is
# unverified whether it passes its environment through to widget scripts, so
# a runtime variable could silently break these widgets on-device instead.
# The one or two output directories for this device, held in the positional
# parameters rather than a space-joined string: a $DEST containing a space
# would otherwise word-split into bogus paths.
set -- "$shared_out"
if [ "$model_out" != "$shared_out" ]; then
	set -- "$shared_out" "$model_out"
fi

for subst_dir in "$@"; do
	find "$subst_dir" -type f -name '*.sh' | while IFS= read -r f; do
		subst_tmp="$(mktemp)"
		sed "s/__PLATFORM__/$PLATFORM/g" "$f" > "$subst_tmp" && cat "$subst_tmp" > "$f" && rm -f "$subst_tmp"
	done
done

# Normalise executable bits: git records these widget scripts inconsistently
# (some 644, some 755), and cp -R preserves the mode of a file it OVERWRITES
# rather than the source's, so layered copying would otherwise ship whichever
# mode the first layer happened to write. Normalising here matches what
# launch.sh does at sync time (chmod +x widgets/*/*.sh) and makes the
# assembled payload independent of how git happened to record the source.
for chmod_dir in "$@"; do
	find "$chmod_dir" -type f -name '*.sh' -exec chmod 755 {} +
	if [ -f "$chmod_dir/trimui_osdd" ]; then
		chmod 755 "$chmod_dir/trimui_osdd"
	fi
	if [ -f "$chmod_dir/widgets/app_music/pic2argb" ]; then
		chmod 755 "$chmod_dir/widgets/app_music/pic2argb"
	fi
done

# Fail loudly rather than shipping a silently dead OSD: a botched layer copy
# otherwise only surfaces on hardware, as an OSD that never opens.
for required in trimui_osdd osdlayout.json bg.png block1x1.png show_default_msg.sh; do
	if [ ! -f "$model_out/$required" ]; then
		echo "assemble-osd: $DEVICE tree is missing $required" >&2
		exit 1
	fi
done
if [ ! -d "$shared_out/widgets" ]; then
	echo "assemble-osd: $DEVICE tree is missing widgets/" >&2
	exit 1
fi

# The substitution above only rewrites *.sh. A surviving __PLATFORM__ means the
# token was used somewhere it is never resolved — a config.json, say — which
# would ship a literal /mnt/SDCARD/.system/__PLATFORM__ path to the device and
# fail silently at runtime.
for leak_dir in "$@"; do
	if grep -rlF '__PLATFORM__' "$leak_dir" >/dev/null 2>&1; then
		echo "assemble-osd: $DEVICE tree still contains __PLATFORM__ in:" >&2
		grep -rlF '__PLATFORM__' "$leak_dir" 2>/dev/null | sed 's/^/  /' >&2
		echo "assemble-osd: substitution only rewrites *.sh (see skeleton/SYSTEM/osd/README.md)" >&2
		exit 1
	fi
done
