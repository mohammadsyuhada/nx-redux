#!/usr/bin/env bash
# Fixture-based test for the shared GPU shader-boost helper. Runs it exactly as a
# pak launch.sh would: sourced from a /bin/sh script inside the pak dir, with the
# GPU governor node and the userdata/system paths pointed at a temp tree.
set -euo pipefail
cd "$(dirname "$0")/../.."
HELPER="$PWD/skeleton/SYSTEM/shared/bin/gpu-shader-boost.sh"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1" >&2; exit 1; }

PAK="$TMP/Emus/MGBA.pak"
UD="$TMP/userdata"
CFGDIR="$UD/MGBA-mgba"
SYS="$TMP/system"
GOV="$TMP/governor"
ROM="$TMP/Roms/Pokemon - FireRed.gba"
mkdir -p "$PAK" "$CFGDIR" "$SYS" "$(dirname "$ROM")"
: > "$ROM"

cat > "$PAK/launch.sh" <<EOF
#!/bin/sh
EMU_EXE=mgba
EMU_TAG=MGBA
ROM="\$1"
. "$HELPER"
nx_gpu_shader_boost
nx_shader_on && on=1 || on=0
echo "boost=\${NX_GPU_SHADER_BOOST-unset} on=\$on"
EOF
chmod +x "$PAK/launch.sh"

# run [DEVICE]: reset the governor to simple_ondemand, launch, print
# "<governor after> boost=<exported base> on=<nx_shader_on as 1/0>"
run() {
	printf 'simple_ondemand\n' > "$GOV"
	out="$(USERDATA_PATH="$UD" SYSTEM_PATH="$SYS" DEVICE="${1:-}" NX_GPU_GOVERNOR_PATH="$GOV" sh "$PAK/launch.sh" "$ROM")"
	echo "$(cat "$GOV") $out"
}
expect() { # expect <label> <want> [DEVICE]
	got="$(run "${3:-}")"
	[ "$got" = "$2" ] || fail "$1: got '$got', want '$2'"
}

# no shader anywhere: the governor is left alone, the base still exported (in-game switching)
expect "no config" "simple_ondemand boost=simple_ondemand on=0"

# the pak default turns a shader on
printf 'minarch_nrofshaders = 1\n' > "$PAK/default.cfg"
expect "pak default" "performance boost=simple_ondemand on=1"

# a device default wins over the plain one
printf 'minarch_nrofshaders = off\n' > "$PAK/default-smartpros.cfg"
expect "device default" "simple_ondemand boost=simple_ondemand on=0" smartpros
rm "$PAK/default-smartpros.cfg"

# the console config overrides the default
printf 'minarch_nrofshaders = off\n' > "$CFGDIR/minarch.cfg"
expect "console off" "simple_ondemand boost=simple_ondemand on=0"
printf 'minarch_scale_filter = LINEAR\r\nminarch_nrofshaders = 2\r\n' > "$CFGDIR/minarch.cfg"
expect "console on, CRLF" "performance boost=simple_ondemand on=1"

# a game config replaces the console one (minarch reads one or the other)
printf 'minarch_nrofshaders = off\n' > "$CFGDIR/Pokemon - FireRed.gba.cfg"
expect "game off over console on" "simple_ondemand boost=simple_ondemand on=0"
# ... and without the key it falls to the pak default (1), not the console's
printf 'minarch_scale_filter = LINEAR\n' > "$CFGDIR/Pokemon - FireRed.gba.cfg"
expect "game without key" "performance boost=simple_ondemand on=1"
printf -- '-minarch_nrofshaders = off\n' > "$CFGDIR/Pokemon - FireRed.gba.cfg"
expect "locked key" "simple_ondemand boost=simple_ondemand on=0"

# the device's own game config
printf 'minarch_nrofshaders = 3\n' > "$CFGDIR/Pokemon - FireRed.gba-smartpros.cfg"
expect "device game config" "performance boost=simple_ondemand on=1" smartpros
rm "$CFGDIR"/*.cfg "$PAK/default.cfg"

# the system config is the last fallback
printf 'minarch_nrofshaders = 1\n' > "$SYS/system.cfg"
expect "system config" "performance boost=simple_ondemand on=1"

# no GPU governor node (tg5040): nothing written, nothing exported; nx_shader_on still answers
# (the system config above still has a shader on)
out="$(USERDATA_PATH="$UD" SYSTEM_PATH="$SYS" NX_GPU_GOVERNOR_PATH="$TMP/none" sh "$PAK/launch.sh" "$ROM")"
[ "$out" = "boost=unset on=1" ] || fail "no governor node: got '$out'"

echo "test-gpu-shader-boost: ok"
