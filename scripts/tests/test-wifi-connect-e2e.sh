#!/bin/bash
# On-device E2E for Settings > Network connect-failure handling (tg5040/tg5050 over adb).
#
# Drives the REAL Settings UI by injecting evdev events into the gamepad node
# and checks the outcome against wpa_supplicant state and the settings log.
# Screenshots of every step land in OUT_DIR for eyeballing the dialogs.
#
# Scenarios:
#   T1  unknown network + wrong password  -> "Incorrect password" dialog,
#       retry reopens the keyboard, and NO profile is left behind
#   T2  saved network whose key is stale  -> same dialog + retry, profile kept
#
# Preconditions (set up by hand; the script checks what it can):
#   - a Brick or Smart Pro S attached over adb (ANDROID_SERIAL if several),
#     WiFi ON, WiFi diagnostics ON
#   - settings.elf running on the Network page, cursor on <ssid>
#   - <ssid> is a WPA/WPA2 network in range whose password you do NOT enter
#     (the script types "11111111"); it must have no saved profile
#
# Usage: scripts/tests/test-wifi-connect-e2e.sh <ssid> [out_dir]
set -u

SSID=${1:?usage: $0 <ssid> [out_dir]}
OUT=${2:-${TMPDIR:-/tmp}/wifi-e2e-$(date +%H%M%S)}
mkdir -p "$OUT/evt"

WPA='wpa_cli -p /etc/wifi/sockets -i wlan0'
die()  { echo "ABORT: $*"; exit 2; }
adb get-state >/dev/null 2>&1 || die "no adb device"

# Platform specifics, read off the device: the gamepad node differs (event3 on
# tg5040, event4 on tg5050), so do the log and supplicant config paths.
PLAT=$(adb shell 'ls -d /mnt/SDCARD/.userdata/tg50*0 2>/dev/null | head -1' | tr -d '\r' | xargs -n1 basename 2>/dev/null)
[ -n "$PLAT" ] || die "cannot tell the platform (no /mnt/SDCARD/.userdata/tg50x0)"
LOG=/mnt/SDCARD/.userdata/$PLAT/logs/settings.txt
CONF=$(adb shell 'ls /etc/wifi/wpa_supplicant/wpa_supplicant.conf /etc/wifi/wpa_supplicant.conf 2>/dev/null | head -1' | tr -d '\r')
EVDEV=/dev/input/$(adb shell "grep -A5 'TRIMUI Player1' /proc/bus/input/devices | grep -o 'event[0-9]*'" | tr -d '\r' | head -1)
[ "$EVDEV" != "/dev/input/" ] || die "gamepad node not found"
echo "platform=$PLAT gamepad=$EVDEV conf=$CONF"

PASS=0; FAIL=0
ok()   { PASS=$((PASS + 1)); echo "  PASS: $*"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL: $*"; }

# ---- input injection ---------------------------------------------------------
# 24-byte input_event structs (aarch64: two 64-bit timeval words) built on the
# host: busybox printf can't emit binary. Each press = down file, up file.
python3 - "$OUT/evt" <<'PY'
import struct, sys, os
d = sys.argv[1]
ev = lambda t, c, v: struct.pack('<QQHHi', 0, 0, t, c, v)
syn = ev(0, 0, 0)
keys = {'a': 305, 'b': 304, 'x': 308, 'y': 307}
for n, c in keys.items():
    open(os.path.join(d, n + '_down'), 'wb').write(ev(1, c, 1) + syn)
    open(os.path.join(d, n + '_up'), 'wb').write(ev(1, c, 0) + syn)
hats = {'up': (17, -1), 'down': (17, 1), 'left': (16, -1), 'right': (16, 1)}
for n, (c, v) in hats.items():
    open(os.path.join(d, n + '_down'), 'wb').write(ev(3, c, v) + syn)
    open(os.path.join(d, n + '_up'), 'wb').write(ev(3, c, 0) + syn)
# L2+R2 together (analog triggers, ABS 2/5): the screenshot daemon's capture combo
open(os.path.join(d, 'l2r2_down'), 'wb').write(ev(3, 2, 255) + ev(3, 5, 255) + syn)
open(os.path.join(d, 'l2r2_up'), 'wb').write(ev(3, 2, 0) + ev(3, 5, 0) + syn)
PY
cat > "$OUT/press.sh" <<EOS
#!/bin/sh
# press.sh <name> [count] [gap_ms]
n=\$1; c=\${2:-1}; g=\${3:-150}; i=0
while [ \$i -lt \$c ]; do
  cat /tmp/evt/\${n}_down > $EVDEV; usleep \$((g*1000))
  cat /tmp/evt/\${n}_up > $EVDEV; usleep \$((g*1000))
  i=\$((i+1))
done
EOS
adb shell 'mkdir -p /tmp/evt' && adb push -q "$OUT/evt/." /tmp/evt/ && adb push -q "$OUT/press.sh" /tmp/press.sh && adb shell 'chmod +x /tmp/press.sh' || die "cannot push input harness"

press() { adb shell "/tmp/press.sh $1 ${2:-1} ${3:-150}"; }

# Screenshots. tg5040 scans out fb0, so a plain fbdev grab works. tg5050 never
# does (GL/DRM path; fb0 is black), so there the screenshot daemon is started
# for the run and each shot is its L2+R2 capture, pulled and then deleted from
# the user's Screenshots folder.
SHOTS_DIR=/mnt/SDCARD/Images/Screenshots
DAEMON_STARTED=0
if [ "$PLAT" = "tg5050" ]; then
	if ! adb shell 'kill -0 $(cat /tmp/screenshot.pid 2>/dev/null) 2>/dev/null'; then
		adb shell 'setsid sh -c /mnt/SDCARD/.system/bin/screenshot.elf </dev/null >/dev/null 2>&1 &'
		sleep 1
		DAEMON_STARTED=1
	fi
	shot() {
		local before after f
		# find, not ls: the device shell colorizes and columnizes ls output
		before=$(adb shell "find $SHOTS_DIR -maxdepth 1 -type f 2>/dev/null" | tr -d '\r')
		adb shell "/tmp/press.sh l2r2 1 300"; sleep 2
		after=$(adb shell "find $SHOTS_DIR -maxdepth 1 -type f 2>/dev/null" | tr -d '\r')
		f=$(comm -13 <(echo "$before" | sort) <(echo "$after" | sort) | tail -1)
		[ -n "$f" ] || { echo "  shot: (no capture for $1)"; return 1; }
		adb pull -q "$f" "$OUT/$1.jpg" && adb shell "rm -f '$f'" && echo "  shot: $OUT/$1.jpg"
	}
	stop_daemon() { [ "$DAEMON_STARTED" = 1 ] && adb shell 'kill $(cat /tmp/screenshot.pid 2>/dev/null) 2>/dev/null'; }
else
	shot() { adb shell "ffmpeg -loglevel error -y -f fbdev -i /dev/fb0 -frames:v 1 -c:v mjpeg -q:v 3 /tmp/e2e.jpg" && adb pull -q /tmp/e2e.jpg "$OUT/$1.jpg" && echo "  shot: $OUT/$1.jpg"; }
	stop_daemon() { :; }
fi
trap stop_daemon EXIT
wpa()   { adb shell "$WPA $*"; }
log_len() { adb shell "wc -l < $LOG" | tr -d '\r '; }
# wait_log <since_line> <regex> <timeout_s>: block until the log gained a matching line
wait_log() {
	local since=$1 re=$2 t=$3 i=0
	while [ $i -lt "$t" ]; do
		if adb shell "tail -n +$((since + 1)) $LOG" | grep -qE "$re"; then return 0; fi
		sleep 1; i=$((i + 1))
	done
	return 1
}
log_since() { adb shell "tail -n +$(( $1 + 1 )) $LOG"; }
profile_id() { wpa list_networks | awk -F'\t' -v s="$SSID" '$2 == s {print $1; exit}' | tr -d '\r'; }
profile_flags() { wpa list_networks | awk -F'\t' -v s="$SSID" '$2 == s {print $4; exit}' | tr -d '\r'; }
conf_has_ssid() { adb shell "grep -c 'ssid=\"$SSID\"' $CONF" | tr -d '\r'; }

# Keyboard: cursor starts on "1"; Confirm is row 5 col 1.
type_wrong_password_and_confirm() {
	press a 8 120          # "11111111"
	press down 5 200
	press right 1 200
	shot "$1_keyboard_confirm"
	press a                # Confirm
}

# ---- preconditions ------------------------------------------------------------
echo "== preconditions"
[ -n "$(adb shell pidof settings.elf | tr -d '\r')" ] || die "settings.elf is not running"
wpa scan >/dev/null; sleep 3
wpa scan_results | grep -qF "$SSID" || die "SSID '$SSID' not in scan results"
[ -z "$(profile_id)" ] || die "SSID already has a saved profile (Forget it first)"
shot pre_network_page
echo "  ok: device ready, '$SSID' in range, no saved profile"

# ---- T1: unknown network, wrong password ---------------------------------------
echo "== T1: unknown network + wrong password"
since=$(log_len)
press a; sleep 1.5                     # network options submenu
shot t1_options
press a; sleep 1.5                     # Connect -> keyboard
type_wrong_password_and_confirm t1
wait_log "$since" "Attempting to connect" 5 || die "no connect attempt in log (WiFi diagnostics off?)"
wait_log "$since" "connected successfully|connection timeout|wrong key|rejected" 25 || die "connect attempt never finished"
sleep 1
shot t1_result
log_since "$since" | grep -qE "wrong key" && ok "backend reported wrong key" || fail "backend did not report wrong key (log: $(log_since "$since" | grep -E 'timeout|wrong|success' | tail -1 | tr -d '\r'))"
[ -z "$(profile_id)" ] && ok "no profile left in wpa_supplicant" || fail "rejected password persisted as profile id $(profile_id) flags $(profile_flags)"
[ "$(conf_has_ssid)" = "0" ] && ok "config file untouched" || fail "rejected password saved to $CONF"

[ "$FAIL" -eq 0 ] || { echo "== T1 failed; skipping T1b/T2 (UI position depends on the fix). $PASS passed, $FAIL failed"; exit 1; }

echo "== T1b: retry from the error dialog reopens the keyboard"
press a; sleep 1.2
shot t1_retry_keyboard
press b; sleep 1.2                     # cancel keyboard -> back to the network list
shot t1_after_cancel

# ---- T2: saved network with a stale key ----------------------------------------
echo "== T2: saved network whose key is stale"
id=$(wpa add_network | tr -d '\r')
wpa "set_network $id ssid '\"$SSID\"'" >/dev/null
wpa "set_network $id psk '\"stale-key-e2e\"'" >/dev/null
wpa "enable_network $id" >/dev/null
wpa save_config >/dev/null
echo "  seeded stale profile id $id; waiting for the scanner to mark it known"
sleep 10
since=$(log_len)
press a; sleep 1.5
shot t2_options                        # expect Connect + Forget
press a                                # Connect with saved (stale) creds
wait_log "$since" "connected successfully|connection timeout|wrong key|rejected" 25 || die "connect attempt never finished"
sleep 1
shot t2_result
log_since "$since" | grep -qE "wrong key" && ok "backend reported wrong key for saved profile" || fail "backend did not report wrong key for saved profile"
[ -n "$(profile_id)" ] && ok "pre-existing profile kept (id $(profile_id))" || fail "pre-existing profile was removed"
press a; sleep 1.2
shot t2_retry_keyboard
press b; sleep 1.2
shot t2_after_cancel

# ---- cleanup -----------------------------------------------------------------------
id=$(profile_id)
[ -n "$id" ] && { wpa "remove_network $id" >/dev/null; wpa save_config >/dev/null; echo "  cleanup: removed seeded profile $id"; }

echo "== summary: $PASS passed, $FAIL failed  (screenshots in $OUT)"
[ "$FAIL" -eq 0 ]
