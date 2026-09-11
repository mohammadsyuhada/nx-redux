#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP=$(mktemp -d)
trap 'kill "${first_pid:-}" "${second_pid:-}" 2>/dev/null || true; rm -rf "$TMP"' EXIT

cat >"$TMP/config.json" <<'EOF'
{
  "canvaswidth": 540,
  "canvasheight": 260
}
EOF
cat >"$TMP/osdmusic.elf" <<'EOF'
#!/bin/sh
mkdir -p "$NX_DIR"
mkfifo "$NX_DIR/widget_cmd"
truncate -s "$NX_CANVAS_BYTES" "$NX_DIR/vfb_osd"
touch "$NX_DIR/widget.lock"
printf '%s\n' "$$" >"$NX_DIR/widget_ready"
trap 'exit 0' TERM
while :; do sleep 1; done
EOF
chmod +x "$TMP/osdmusic.elf"
sed -e "s|^WIDGET=.*|WIDGET=$TMP/osdmusic.elf|" \
    -e "s|^DIR=.*|DIR=$TMP/bridge|" \
    -e "s|^CONFIG=.*|CONFIG=$TMP/config.json|" \
    skeleton/SYSTEM/osd/common/widgets/app_music/launch.sh >"$TMP/launch.sh"
chmod +x "$TMP/launch.sh"

NX_DIR="$TMP/bridge" NX_CANVAS_BYTES=$((540 * 260 * 4)) "$TMP/launch.sh"
first_pid=$(cat "$TMP/bridge/widget_ready")
NX_DIR="$TMP/bridge" NX_CANVAS_BYTES=$((540 * 260 * 4)) "$TMP/launch.sh"
test "$(cat "$TMP/bridge/widget_ready")" = "$first_pid"
kill "$first_pid"
for _ in $(seq 1 20); do
    [ ! -d "/proc/$first_pid" ] && break
    sleep 0.05
done
NX_DIR="$TMP/bridge" NX_CANVAS_BYTES=$((540 * 260 * 4)) "$TMP/launch.sh"
second_pid=$(cat "$TMP/bridge/widget_ready")
test "$second_pid" != "$first_pid"

echo "osdmusic launcher readiness, duplicate, and restart checks passed"
