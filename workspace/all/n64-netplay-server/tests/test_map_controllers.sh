#!/bin/sh
# Tests nx_netplay_map.awk: local pad on port P, ports 1..N plugged.
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)"
AWK="$DIR/../../../skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/nx_netplay_map.awk"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
CFG="$TMP/in.cfg"
cat > "$CFG" <<'EOF'
[Core]
Version = 1.010000
[Input-SDL-Control1]
version = 2.000000
mode = 0
device = 0
name = "Xbox 360 Controller"
plugged = True
plugin = 2
DPad R = "hat(0 Right)"
A Button = "button(1)"
X Axis = "axis(0-,0+)"
[Input-SDL-Control2]
device = -1
plugged = False
A Button = ""
[Input-SDL-Control3]
device = -1
plugged = False
A Button = ""
[Input-SDL-Control4]
device = -1
plugged = False
A Button = ""
[Rsp-HLE]
Version = 1.010000
EOF

fail=0
chk(){ if eval "$2"; then echo "ok $1"; else echo "FAIL $1"; fail=1; fi; }

# Player 2 of 3 — the pad STAYS on Control1 regardless of P (netplay routes it
# to this device's seat via netplay_get_controller). Only plugged tracks N.
dev(){ awk "/^\[Input-SDL-Control$1\]/{f=1} f&&/^device/{print \$3;exit}" "$2"; }
plg(){ awk "/^\[Input-SDL-Control$1\]/{f=1} f&&/^plugged/{print \$3;exit}" "$2"; }
awk -v P=2 -v N=3 -f "$AWK" "$CFG" > "$TMP/out.cfg"
# T1: still 4 control sections
chk T1 '[ "$(grep -c "^\[Input-SDL-Control[1-4]\]$" "$TMP/out.cfg")" = 4 ]'
# T2: passthrough sections intact and in place
chk T2 'grep -q "^\[Core\]$" "$TMP/out.cfg" && grep -q "^\[Rsp-HLE\]$" "$TMP/out.cfg"'
# T3: local pad on Control1 (device 0) even though this device is player 2
chk T3 '[ "$(dev 1 "$TMP/out.cfg")" = 0 ]'
# T4: the assigned port (2) is device=-1 — pad is NOT moved there
chk T4 '[ "$(dev 2 "$TMP/out.cfg")" = -1 ]'
# T5: ports 1..3 plugged True, port 4 False
chk T5 '[ "$(plg 3 "$TMP/out.cfg")" = True ]'
chk T6 '[ "$(plg 4 "$TMP/out.cfg")" = False ]'
# T7: Control1 has the button map (value with quotes intact)
chk T7 'awk "/^\[Input-SDL-Control1\]/{f=1} f&&/^A Button/{print;exit}" "$TMP/out.cfg" | grep -q "button(1)"'
chk T8 'awk "/^\[Input-SDL-Control1\]/{f=1} f&&/^DPad R/{print;exit}" "$TMP/out.cfg" | grep -q "hat(0 Right)"'
# T8b: ports 2..N are present (plugged) so the game shows N controllers
chk T8b '[ "$(plg 2 "$TMP/out.cfg")" = True ]'

# Player 1 of 2 (host): Control1 device 0 + buttons, port 2 plugged, 3/4 not
awk -v P=1 -v N=2 -f "$AWK" "$CFG" > "$TMP/out2.cfg"
chk T9  '[ "$(dev 1 "$TMP/out2.cfg")" = 0 ]'
chk T10 '[ "$(plg 2 "$TMP/out2.cfg")" = True ]'
chk T11 '[ "$(plg 3 "$TMP/out2.cfg")" = False ]'
# T12: player-4 of 4 still keeps the pad on Control1 (not port 4)
awk -v P=4 -v N=4 -f "$AWK" "$CFG" > "$TMP/out3.cfg"
chk T12 '[ "$(dev 1 "$TMP/out3.cfg")" = 0 ] && [ "$(dev 4 "$TMP/out3.cfg")" = -1 ] && [ "$(plg 4 "$TMP/out3.cfg")" = True ]'

[ $fail = 0 ] && echo "ALL PASS" || { echo "FAILURES"; exit 1; }
