#!/usr/bin/env bash
# Host side of the sweep: pushes the driver, runs each config, saves the log.
#   bench-minarch.sh <plat> <serial> <TAG> "<rom path on device>" <id:cmd;cmd> ...
# A config's cmds are sysfs writes applied after minarch has settled, e.g.
#   cap1008:"echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
#   little:"echo 0 > /sys/devices/system/cpu/cpu4/online"
set -euo pipefail
PLAT=$1; SERIAL=$2; TAG=$3; ROM=$4; shift 4
SECS="${BENCH_SECS:-60}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/results/$PLAT"; mkdir -p "$OUT"
adb -s "$SERIAL" push "$HERE/bench-driver.sh" /tmp/bench-driver.sh >/dev/null
for spec in "$@"; do
  id="${spec%%:*}"; cmds="${spec#*:}"
  args=""; IFS=';' read -ra parts <<< "$cmds"
  for c in "${parts[@]}"; do [ -n "$c" ] && args+=" '$c'"; done
  echo "== $TAG $id"
  # Forward the optional pre-launch hook into the device command when set, so the
  # driver can eval it before requesting the pak (e.g. Brick daemon herding).
  pre=""; [ -n "${BENCH_PRELAUNCH:-}" ] && pre="export BENCH_PRELAUNCH='$BENCH_PRELAUNCH'; "
  adb -s "$SERIAL" shell "${pre}sh /tmp/bench-driver.sh $PLAT $TAG \"$ROM\" $id $SECS $args" | tee "$OUT/$TAG-$id.log" | grep -E '^\[driver\]'
  sleep 5 # let the launcher settle (its own policy re-applies)
done
python3 "$HERE/bench-summary.py" "$PLAT" "$TAG"
