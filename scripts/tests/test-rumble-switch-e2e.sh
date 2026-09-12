#!/bin/bash
# On-device E2E for the OSD "Motor" master vibration switch (tg5040/tg5050 over adb).
#
# The switch lives in libmsettings shared memory (persisted to msettings.bin)
# and is exposed by `osdctl get/set rumble`; the OSD toggle_rumble widget and
# every app's VIB thread read the same flag. This script drives the widget
# scripts exactly as trimui_osdd does and checks:
#   T1  default reads ON (fresh or pre-existing msettings.bin)
#   T2  set.sh flips it OFF: osdctl and the widget status file agree
#   T3  update.sh (status query) reports the stored value, does not change it
#   T4  set.sh flips it back ON and the confirmation buzz reaches the motor
#       sysfs (gpio227 on tg5040, /sys/class/motor/level on tg5050)
#   T5  the value survives a reboot (OFF is remembered), then is restored to ON
#   T6  the "Vibration strength" level (osdctl rumblestrength) defaults to
#       Normal, round-trips, and clamps garbage back to Normal
#
# The in-game gate (VIB thread) and the shutdown-tap skip are covered by the
# hands-on checks in .dev/DEV_CHECKLIST.md; this script proves the plumbing.
#
# Usage: ANDROID_SERIAL=<serial> scripts/tests/test-rumble-switch-e2e.sh
set -u
die()  { echo "ABORT: $*"; exit 2; }
adb get-state >/dev/null 2>&1 || die "no adb device (set ANDROID_SERIAL)"

PLAT=$(adb shell 'readlink /proc/$(pidof nextui.elf)/exe' | tr -d '\r' | sed -n 's|.*/\.system/\(tg[0-9]*\)/.*|\1|p')
# busybox ls colours its output even on a pipe: strip the escapes
[ -z "$PLAT" ] && PLAT=$(adb shell 'ls -d /mnt/SDCARD/.userdata/tg50*' | tr -d '\r' | sed 's/\x1b\[[0-9;]*m//g' | head -1 | xargs -n1 basename)
case "$PLAT" in tg5040|tg5050) ;; *) die "cannot determine platform ($PLAT)";; esac
if [ "$PLAT" = tg5040 ]; then MOTOR=/sys/class/gpio/gpio227/value; ON_RE='^1$'; else MOTOR=/sys/class/motor/level; ON_RE='^[1-9][0-9]*$'; fi
echo "platform=$PLAT motor=$MOTOR"

OSD=/usr/trimui/osd/widgets/toggle_rumble
ENV='export LD_LIBRARY_PATH=/mnt/SDCARD/.system/lib:/usr/trimui/lib'
osdctl() { adb shell "$ENV; /mnt/SDCARD/.system/bin/osdctl $*" | tr -d '\r'; }
status() { adb shell 'cat /tmp/trimui_osd/toggle_rumble/status' | tr -d '\r'; }
pass=0; fail=0
check() { if [ "$2" = "$3" ]; then echo "PASS $1 ($2)"; pass=$((pass+1)); else echo "FAIL $1: got '$2' want '$3'"; fail=$((fail+1)); fi; }

adb shell "ls $OSD/set.sh $OSD/update.sh >/dev/null" || die "widget scripts not synced to $OSD"

# T1 default ON
check "T1 osdctl default on" "$(osdctl get rumble)" 1

# T2 widget turns it OFF
adb shell "sh $OSD/set.sh 1" </dev/null
check "T2 osdctl after set.sh" "$(osdctl get rumble)" 0
check "T2 status file" "$(status)" 0

# T3 update.sh is read-only
adb shell "sh $OSD/update.sh" </dev/null
check "T3 osdctl after update.sh" "$(osdctl get rumble)" 0
check "T3 status file" "$(status)" 0

# T4 widget turns it ON with a buzz: sample the motor node at ~5 ms while set.sh runs
adb shell "( for i in \$(seq 1 120); do cat $MOTOR; usleep 5000; done > /tmp/motor_samples ) & sleep 0.05; sh $OSD/set.sh 1; wait" </dev/null
SAMPLES=$(adb shell 'cat /tmp/motor_samples' | tr -d '\r')
ONS=$(echo "$SAMPLES" | grep -cE "$ON_RE"); LAST=$(echo "$SAMPLES" | tail -1)
check "T4 osdctl after set.sh" "$(osdctl get rumble)" 1
check "T4 status file" "$(status)" 1
[ "$ONS" -gt 0 ] && check "T4 buzz seen on motor node (${ONS} on-samples)" 1 1 || check "T4 buzz seen on motor node" 0 1
check "T4 motor released after buzz" "$LAST" 0

# T5 persistence: OFF must survive a reboot
osdctl set rumble 0 >/dev/null
check "T5 pre-reboot off" "$(osdctl get rumble)" 0
adb reboot; sleep 3; adb wait-for-device
for i in $(seq 1 60); do adb shell pidof nextui.elf >/dev/null 2>&1 && break; sleep 1; done; sleep 3
check "T5 off survives reboot" "$(osdctl get rumble)" 0
adb shell "sh $OSD/update.sh" </dev/null
check "T5 widget status after reboot" "$(status)" 0
osdctl set rumble 1 >/dev/null
check "T5 restored on" "$(osdctl get rumble)" 1

# T6 "Vibration strength" lives in the same block: default Normal (0), set/get, out-of-range clamps
check "T6 strength default normal" "$(osdctl get rumblestrength)" 0
osdctl set rumblestrength 2 >/dev/null
check "T6 strength set strong" "$(osdctl get rumblestrength)" 2
osdctl set rumblestrength 7 >/dev/null
check "T6 strength clamps to normal" "$(osdctl get rumblestrength)" 0

echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
