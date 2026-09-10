#!/bin/bash
# On-device deep-sleep (suspend-to-RAM) probe for tg5040 devices over adb.
#
# Written for issue #90 (Brick Pro never wakes from deep sleep). The tg5040
# `suspend` script now performs the stock-firmware handshake: it raises
# /tmp/system_suspend so the stock trimui_inputd pauses its input threads
# (on the Brick Pro: the thread reading the I2C hall sticks every 16 ms)
# across the suspend/resume cycle, and drops the flag right after resume.
#
# Phases:
#   A  flag handshake. With /tmp/system_suspend present, inputd's I2C stick
#      polling must stop: on the Brick Pro the stick bus's interrupt count in
#      /proc/interrupts goes flat (measured 2026-09-10: ~1030 twi3 irqs/s
#      without the flag, exactly 0 with it). Other models have no I2C stick
#      traffic, so only inputd's poll-thread CPU time is sampled there
#      (informational: the GPIO loop keeps waking to re-check the flag, so
#      the drop is small). The sampling windows are short because the stock
#      inputd deletes the flag by itself about 2 s after it appears while
#      the LCD is lit (its "work around suspend for backlight" path); with
#      the panel off -- which is how nextui always runs the real suspend --
#      the flag persists indefinitely.
#   B  round trip. Arms an RTC wake alarm and runs the candidate suspend
#      script from the device itself (setsid/nohup) with its log on the SD
#      card, synced before the suspend so it survives a hard reset. Then waits
#      for the device to come back over adb and classifies the log:
#      userspace resumed (PASS), kernel refused to suspend, or the device
#      never came back (hard-reset it, then rerun with --collect to read what
#      the log kept).
#
# Preconditions:
#   - a tg5040 device (Brick / Brick Pro / Smart Pro) attached over adb
#     (ANDROID_SERIAL if several), sitting idle at the nextui main menu
#   - Phase B suspends the device for ~ALARM seconds; the RTC alarm wakes it.
#     If the kernel refuses to suspend with the USB cable attached the log
#     says so: unplug after "runner started" and wake it with POWER by hand.
#   - the backlight is left as is. nextui switches it off before deep sleep;
#     with the LCD lit the stock inputd drops the flag on its own after
#     ~2 s, and the suspend script removes it right after resume anyway, so
#     an idle device with the screen on is a valid stand-in for the round
#     trip (the kernel path is identical; only the flag's lifetime differs).
#   - Phase B over USB cannot reproduce issue #90's report on its own: the
#     Brick Pro tested 2026-09-10 slept and woke with the pre-patch script
#     too, over USB (RTC wake) and on battery (POWER wake, menu and in-game).
#
# Usage: scripts/tests/test-deep-sleep-e2e.sh [options] [out_dir]
#   --installed       run the suspend script already on the card (A/B baseline)
#   --script <file>   candidate to push (default: skeleton/SYSTEM/tg5040/bin/suspend)
#   --alarm <secs>    RTC wake alarm delay for Phase B (default 25)
#   --skip-a | --skip-b
#   --collect         only fetch and classify the log of a previous Phase B
set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
CANDIDATE="$REPO/skeleton/SYSTEM/tg5040/bin/suspend"
MODE=candidate; ALARM=25; DO_A=1; DO_B=1; COLLECT=0; OUT=""
while [ $# -gt 0 ]; do
	case "$1" in
		--installed) MODE=installed ;;
		--script) CANDIDATE=$2; shift ;;
		--alarm) ALARM=$2; shift ;;
		--skip-a) DO_A=0 ;;
		--skip-b) DO_B=0 ;;
		--collect) COLLECT=1 ;;
		-h|--help) sed -n '2,45p' "$0"; exit 0 ;;
		*) OUT=$1 ;;
	esac
	shift
done
OUT=${OUT:-${TMPDIR:-/tmp}/deep-sleep-e2e-$(date +%H%M%S)}
mkdir -p "$OUT"

die()  { echo "ABORT: $*"; exit 2; }
# adb shell with a hard timeout: a suspended device just hangs the transport
ash()  { perl -e 'alarm shift; exec @ARGV' "$1" adb shell "$2" 2>&1 | tr -d '\r'; }
PASS=0; FAIL=0
ok()   { PASS=$((PASS + 1)); echo "  PASS: $*"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL: $*"; }
info() { echo "  info: $*"; }

perl -e 'alarm 10; exec @ARGV' adb get-state >/dev/null 2>&1 || die "no adb device"
MODEL=$(ash 20 'strings /usr/trimui/bin/MainUI | grep ^Trimui')
case "$MODEL" in
	"Trimui Brick Pro") DEVICE=brickpro ;;
	"Trimui Brick")     DEVICE=brick ;;
	"Trimui Smart Pro") DEVICE=smartpro ;;
	*) die "not a tg5040 device (model '$MODEL')" ;;
esac
DLOG=/mnt/SDCARD/.userdata/tg5040/logs/deep-sleep-probe.txt
echo "device=$DEVICE mode=$MODE alarm=${ALARM}s out=$OUT"

# ---- Phase A: inputd honours /tmp/system_suspend -----------------------------
phase_a() {
	echo "== Phase A: inputd pauses while /tmp/system_suspend exists"
	ash 60 '
P=$(pidof trimui_inputd); [ -n "$P" ] || { echo NO_INPUTD; exit 0; }
snap() {
	grep -iE "twi|i2c" /proc/interrupts | awk "{s=0; for(i=2;i<=NF;i++) if (\$i ~ /^[0-9]+\$/) s+=\$i; print \$NF, s}" > $1
	t=0; for d in /proc/$P/task/*; do [ "$(basename $d)" = "$P" ] && continue
		t=$(cut -d" " -f14,15 $d/stat | awk "{print \$1+\$2}"); done
	echo "pollthread_ticks $t" >> $1
}
delta() { awk "NR==FNR{a[\$1]=\$2; next} {print \$1, \$2-a[\$1]}" $1 $2; }
# 1 s windows: with the LCD lit inputd deletes the flag itself after ~2 s
W=1
rm -f /tmp/system_suspend
snap /tmp/ds0; sleep $W; snap /tmp/ds1; echo "[noflag]"; delta /tmp/ds0 /tmp/ds1
touch /tmp/system_suspend; sleep 0.3
snap /tmp/ds0; sleep $W; snap /tmp/ds1; echo "[flag]"; delta /tmp/ds0 /tmp/ds1
echo "flag_held=$([ -e /tmp/system_suspend ] && echo yes || echo no)"
rm -f /tmp/system_suspend; sleep 0.3
snap /tmp/ds0; sleep $W; snap /tmp/ds1; echo "[after]"; delta /tmp/ds0 /tmp/ds1
echo "flag_left=$([ -e /tmp/system_suspend ] && echo yes || echo no)"
rm -f /tmp/ds0 /tmp/ds1' | tee "$OUT/phase-a.txt" | sed 's/^/    /'
	grep -q NO_INPUTD "$OUT/phase-a.txt" && { fail "trimui_inputd not running"; return; }
	grep -q 'flag_left=no' "$OUT/phase-a.txt" || fail "flag left behind after rm"
	grep -q 'flag_held=no' "$OUT/phase-a.txt" && info "inputd deleted the flag before the window ended (LCD lit); the flag window undercounts"
	# per-window values: window name -> "irqname delta"
	win() { awk -v w="[$1]" '$0==w{f=1; next} /^\[/{f=0} f && $1==key{print $2}' key="$2" "$OUT/phase-a.txt"; }
	if [ "$DEVICE" = brickpro ]; then
		# the stick bus is the twi line busiest without the flag
		bus=$(awk '/^\[noflag\]/{f=1; next} /^\[/{f=0} f && $1!="pollthread_ticks"{print $2, $1}' "$OUT/phase-a.txt" | sort -n | tail -1 | awk '{print $2}')
		n=$(win noflag "$bus"); w=$(win flag "$bus"); a=$(win after "$bus")
		info "stick bus $bus irqs/1s: noflag=$n flag=$w after=$a"
		if [ "${n:-0}" -ge 100 ] && [ "${w:-0}" -le $((n / 10)) ] && [ "${a:-0}" -ge $((n / 2)) ]; then
			ok "I2C stick polling stops under the flag and resumes after it"
		else
			fail "I2C stick polling did not pause/resume as expected"
		fi
	else
		info "$DEVICE has no I2C sticks; poll-thread ticks/1s: noflag=$(win noflag pollthread_ticks) flag=$(win flag pollthread_ticks) after=$(win after pollthread_ticks) (informational)"
	fi
}

# ---- Phase B: one suspend/resume round trip -----------------------------------
runner() {
	cat <<'RUN'
#!/bin/sh
# deep-sleep probe runner: <script> <log> <alarm-secs>
SCRIPT=$1; LOG=$2; ALARM=$3
# same environment nextui gives the script (MinUI.pak/launch.sh): the wifi
# and bt init scripts it calls need rfkill.elf and the SD card's libs
export PATH=/mnt/SDCARD/.system/bin:/mnt/SDCARD/.system/shared/bin:$PATH
export LD_LIBRARY_PATH=/mnt/SDCARD/.system/lib:/mnt/SDCARD/.system/shared/lib:/usr/trimui/lib:${LD_LIBRARY_PATH:-}
trap '' HUP INT TERM PIPE
exec >>"$LOG" 2>&1
echo "=== probe start uptime=$(cut -d' ' -f1 /proc/uptime) epoch=$(date +%s) usb_online=$(cat /sys/class/power_supply/axp2202-usb/online 2>/dev/null) wpa=$(pidof wpa_supplicant) script=$SCRIPT"
echo 0 > /sys/class/rtc/rtc0/wakealarm
echo "+$ALARM" > /sys/class/rtc/rtc0/wakealarm
echo "armed wakealarm=$(cat /sys/class/rtc/rtc0/wakealarm) rtc_now=$(cat /sys/class/rtc/rtc0/since_epoch)"
echo "--- invoking suspend script epoch=$(date +%s)"
sync
sh "$SCRIPT"; rc=$?
echo "--- script returned rc=$rc epoch=$(date +%s) uptime=$(cut -d' ' -f1 /proc/uptime) flag=$([ -e /tmp/system_suspend ] && echo PRESENT || echo absent)"
sleep 2
echo "--- after: nextui=$(pidof nextui.elf) inputd=$(pidof trimui_inputd) keymon=$(pidof keymon.elf) wpa=$(pidof wpa_supplicant) usb_online=$(cat /sys/class/power_supply/axp2202-usb/online 2>/dev/null)"
sleep 6
echo "--- wlan0: $(ip -4 addr show wlan0 2>/dev/null | grep -o 'inet [0-9.]*')"
echo "--- dmesg (PM)"
dmesg | grep -iE 'PM:|suspend|resume|Freezing|wakeup' | tail -20
echo "=== probe end"
sync
RUN
}

collect() {
	adb pull "$DLOG" "$OUT/probe.txt" >/dev/null 2>&1 || { fail "could not pull $DLOG"; return; }
	sed 's/^/    /' "$OUT/probe.txt"
	if grep -q -- '--- script returned rc=0' "$OUT/probe.txt"; then
		t0=$(grep -o 'invoking suspend script epoch=[0-9]*' "$OUT/probe.txt" | tail -1 | grep -o '[0-9]*$')
		t1=$(grep -o 'script returned rc=0 epoch=[0-9]*' "$OUT/probe.txt" | tail -1 | grep -o '[0-9]*$')
		asleep=$((t1 - t0))
		if [ "$asleep" -ge 3 ]; then
			ok "userspace resumed after ${asleep}s (kernel and script both came back)"
		else
			fail "script returned rc=0 after only ${asleep}s: the device did not really suspend"
		fi
		grep -q 'flag=absent' "$OUT/probe.txt" && ok "/tmp/system_suspend cleared after resume" || fail "/tmp/system_suspend still present after resume"
		grep -q -- '--- after: nextui=[0-9]' "$OUT/probe.txt" && ok "nextui alive after resume" || fail "nextui gone after resume"
		grep -q -- 'inputd=[0-9]' "$OUT/probe.txt" && ok "trimui_inputd alive after resume" || fail "trimui_inputd gone after resume"
	elif grep -q -- '--- script returned rc=' "$OUT/probe.txt"; then
		fail "suspend script failed: $(grep -o 'script returned rc=[0-9]*' "$OUT/probe.txt" | tail -1) (see the 'kernel returned' lines; a first EBUSY is normal with USB/adb active, the script retries)"
	elif grep -q -- '--- invoking suspend script' "$OUT/probe.txt"; then
		fail "device never returned from suspend: the kernel (or the script's resume steps) hung after 'echo mem'"
	else
		fail "runner never reached the suspend script"
	fi
}

phase_b() {
	echo "== Phase B: suspend/resume round trip ($MODE script, RTC alarm ${ALARM}s)"
	if [ "$MODE" = installed ]; then
		DSCRIPT=/mnt/SDCARD/.system/bin/suspend
	else
		[ -f "$CANDIDATE" ] || die "candidate script $CANDIDATE not found"
		adb push "$CANDIDATE" /tmp/suspend-candidate >/dev/null 2>&1 || die "push failed"
		DSCRIPT=/tmp/suspend-candidate
		ash 10 'sh -n /tmp/suspend-candidate && echo SYNTAX_OK' | grep -q SYNTAX_OK || die "candidate fails busybox sh -n"
	fi
	info "$(ash 10 "grep -c system_suspend $DSCRIPT") system_suspend reference(s) in $DSCRIPT"
	runner > "$OUT/dsprobe.sh"
	adb push "$OUT/dsprobe.sh" /tmp/dsprobe.sh >/dev/null 2>&1 || die "push failed"
	ash 10 "rm -f $DLOG; [ -e /sys/class/rtc/rtc0/wakealarm ] && echo RTC_OK" | grep -q RTC_OK || die "no RTC wakealarm on this device"
	wpa_before=$(ash 10 'pidof wpa_supplicant')
	# Detach the runner from the adb pty: the USB link drops at suspend and
	# adbd's HUP would otherwise kill the suspend script mid-run (seen on the
	# Brick: rc=129 after resume). This busybox has neither setsid nor nohup;
	# start-stop-daemon -b gives a fresh session with no tty (pidfile mode,
	# otherwise it refuses because other /bin/sh instances exist). Fallback:
	# a host-side background adb shell, whose runner ignores HUP.
	if ash 10 'ls /sbin/start-stop-daemon 2>/dev/null' | grep -q start-stop-daemon; then
		ash 15 "/sbin/start-stop-daemon -S -b -m -p /tmp/dsprobe.pid -x /bin/sh -- /tmp/dsprobe.sh $DSCRIPT $DLOG $ALARM"
	else
		adb shell "sh /tmp/dsprobe.sh $DSCRIPT $DLOG $ALARM </dev/null >/dev/null 2>&1" >/dev/null 2>&1 &
	fi
	sleep 2
	ash 8 "grep -c 'probe start' $DLOG 2>/dev/null" | grep -q '^[1-9]' || die "runner did not start (no $DLOG)"
	echo "  runner started; waiting up to $((ALARM + 120))s for the device to suspend and come back"
	deadline=$(( $(date +%s) + ALARM + 120 )); state=online; done=0
	while [ "$(date +%s)" -lt "$deadline" ]; do
		if perl -e 'alarm 5; exec @ARGV' adb get-state >/dev/null 2>&1; then
			[ $state = offline ] && echo "  device back on adb"; state=online
			if ash 8 "tail -1 $DLOG 2>/dev/null" | grep -q '=== probe end'; then done=1; break; fi
		else
			[ $state = online ] && echo "  device left adb (suspended)"; state=offline
		fi
		sleep 3
	done
	[ $done = 1 ] || echo "  timed out waiting for '=== probe end'; collecting whatever the log holds"
	collect
	if [ -n "$wpa_before" ]; then
		ash 10 'pidof wpa_supplicant' | grep -q '[0-9]' && ok "wpa_supplicant restarted after resume" || fail "wpa_supplicant not back after resume"
	fi
	ash 10 'ls /tmp/system_suspend 2>/dev/null' | grep -q system_suspend && fail "flag left on the device" || ok "no stale /tmp/system_suspend on the device"
}

if [ $COLLECT = 1 ]; then collect
else
	[ $DO_A = 1 ] && phase_a
	[ $DO_B = 1 ] && phase_b
fi
echo "== $PASS passed, $FAIL failed (logs in $OUT)"
[ $FAIL = 0 ]
