#!/usr/bin/env bash
# Host test for the OSD temperature widget (skeleton/SYSTEM/osd/common/widgets/
# static_temperature/refresh.sh). It used to show the battery sensor
# (axp2202-battery: ~25-36 C) while the SoC ran at 55-70 C and throttled; it must
# show the SoC: the hottest cpu*/gpu* thermal zone, whatever the device names them
# (Brick: cpu_/gpu_thermal_zone; Smart Pro S: cpul_/cpub_/gpu_thermal_zone), never
# the battery, charger or DDR zones.
set -euo pipefail
cd "$(dirname "$0")/../.."
W="$PWD/skeleton/SYSTEM/osd/common/widgets/static_temperature/refresh.sh"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FAIL=0; fail() { echo "FAIL: $*" >&2; FAIL=1; }

zone() { mkdir -p "$1/thermal_zone$2"; echo "$3" > "$1/thermal_zone$2/type"; echo "$4" > "$1/thermal_zone$2/temp"; }
run() { THERMAL_ROOT="$1" OSD_TMP="$TMP/out" sh "$W"; cat "$TMP/out/toggle_temperature/temperature"; }

# Brick (tg5040) layout, battery listed last
B="$TMP/brick"; zone "$B" 0 cpu_thermal_zone 42639; zone "$B" 1 gpu_thermal_zone 43912
zone "$B" 2 ddr_thermal_zone 48000; zone "$B" 3 axp2202-battery 36000
[ "$(run "$B")" = "43.9" ] || fail "brick: got '$(run "$B")', want 43.9 (gpu, not ddr/battery)"

# Smart Pro S (tg5050) layout, hot big cluster, hotter charger chip
S="$TMP/sps"; zone "$S" 0 cpul_thermal_zone 54210; zone "$S" 1 cpub_thermal_zone 69225
zone "$S" 2 gpu_thermal_zone 64740; zone "$S" 3 npu_thermal_zone 64740
zone "$S" 4 ddr_thermal_zone 70000; zone "$S" 5 axp2202-usb 71600; zone "$S" 6 axp2202-battery 25400
[ "$(run "$S")" = "69.2" ] || fail "sps: got '$(run "$S")', want 69.2 (cpub, not usb/ddr/battery)"

# tenths are truncated like the old widget, and a leading zero stays
Z="$TMP/zero"; zone "$Z" 0 cpu_thermal_zone 50049
[ "$(run "$Z")" = "50.0" ] || fail "rounding: got '$(run "$Z")', want 50.0"

# no cpu/gpu zone at all: fall back to the battery reading (old behaviour)
N="$TMP/none"; mkdir -p "$N"; zone "$N" 0 axp2202-battery 30100
[ "$(run "$N")" = "30.1" ] || fail "fallback: got '$(run "$N")', want 30.1 (battery)"

[ "$FAIL" = 0 ] && echo "PASS: osd-temperature" || exit 1
