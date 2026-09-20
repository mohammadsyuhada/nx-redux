#!/bin/sh
# On-device sweep driver (busybox sh). Launches a minarch pak exactly like a
# user would (the launcher's pak-request file), lets it settle, applies one
# CPU configuration over sysfs, samples for N seconds, stops the emulator.
# The boot script's restore block + the launcher's own CPU policy return the
# device to normal afterwards (verified 2026-09-20).
#   bench-driver.sh <plat> <TAG> <rom> <config-id> <seconds> [cmd ...]
PLAT=$1; TAG=$2; ROM=$3; CFG=$4; SECS=$5; shift 5
PAK=/mnt/SDCARD/.system/paks/Emus/$TAG.pak
LOGDIR=/tmp/nx-logs; [ -d $LOGDIR ] || LOGDIR=/mnt/SDCARD/.userdata/$PLAT/logs
L=$LOGDIR/$TAG.txt
C0=/sys/devices/system/cpu/cpu0/cpufreq; C4=/sys/devices/system/cpu/cpu4/cpufreq
[ -n "$(pidof minarch.elf)" ] && { echo "[driver] minarch already running"; exit 2; }
mkdir -p /tmp/bench.pak
printf '#!/bin/sh\nexport NX_BENCH=1\nexec "%s/launch.sh" "%s"\n' "$PAK" "$ROM" > /tmp/bench.pak/launch.sh
chmod +x /tmp/bench.pak/launch.sh
: > "$L" 2>/dev/null
echo /tmp/bench.pak > /tmp/nextui_open
n=0; while [ -z "$(pidof minarch.elf)" ] && [ $n -lt 150 ]; do sleep 0.2; n=$((n+1)); done
PID=$(pidof minarch.elf); [ -z "$PID" ] && { echo "[driver] minarch did not start"; exit 3; }
sleep 5 # minarch has applied its own preset by now
for cmd in "$@"; do eval "$cmd"; done
echo "[driver] cfg=$CFG online=$(cat /sys/devices/system/cpu/online) c0=$(cat $C0/scaling_governor 2>/dev/null)/$(cat $C0/scaling_min_freq 2>/dev/null)-$(cat $C0/scaling_max_freq 2>/dev/null) c4=$(cat $C4/scaling_governor 2>/dev/null)/$(cat $C4/scaling_min_freq 2>/dev/null)-$(cat $C4/scaling_max_freq 2>/dev/null)"
n0=$(grep -c '\[bench\]' "$L")
i=0
while [ $i -lt $SECS ]; do
  echo "[freq] t=$i khz0=$(cat $C0/scaling_cur_freq 2>/dev/null) khz4=$(cat $C4/scaling_cur_freq 2>/dev/null || echo 0)"
  sleep 1; i=$((i+1))
done
grep '\[bench\]' "$L" | tail -n +$((n0+1))
# Exit through minarch's own menu: MENU, Down x4 (Continue Save Load Options
# Quit), A. NEVER SIGTERM the emulator: SDL turns SIGTERM into SDL_QUIT, which
# the shared input code treats as the OSD power widget's power-off request
# (api.c PAD_poll) — the device shuts down. SIGKILL bypasses SDL and is the
# fallback only.
DEV=/dev/input/event3; [ "$PLAT" = tg5050 ] && DEV=/dev/input/event4
ev() { printf "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000$1" > $DEV; }
key() { ev "\001\000$1\001\000\000\000"; ev '\000\000\000\000\000\000\000\000'; sleep 0.1; ev "\001\000$1\000\000\000\000"; ev '\000\000\000\000\000\000\000\000'; sleep 0.35; }
hat() { ev "\003\000\021\000$1\000\000\000"; ev '\000\000\000\000\000\000\000\000'; sleep 0.1; ev '\003\000\021\000\000\000\000\000'; ev '\000\000\000\000\000\000\000\000'; sleep 0.35; }
key '\074\001'; sleep 1.5                      # MENU (316) opens the in-game menu
hat '\001'; hat '\001'; hat '\001'; hat '\001'  # Down x4 -> Quit
key '\060\001'                                 # A (304) confirms
n=0; while [ -n "$(pidof minarch.elf)" ] && [ $n -lt 50 ]; do sleep 0.2; n=$((n+1)); done
[ -n "$(pidof minarch.elf)" ] && { echo "[driver] menu quit failed, SIGKILL fallback"; kill -9 $(pidof minarch.elf); }
n=0; while [ -z "$(pidof nextui.elf)" ] && [ $n -lt 150 ]; do sleep 0.2; n=$((n+1)); done
rm -rf /tmp/bench.pak
# The Brick (tg5040) has no hotplug restore path: its boot script and the
# launcher only manage cpu hotplug on tg5050, so a `cores2` config that offlined
# cpu2/cpu3 would otherwise leave the device stuck on two cores after the sweep.
# Re-online cpu1-3 here so the driver can never leave a tg5040 short of its four
# big cores. tg5050 restores its own cpu4 via the boot script/launcher.
if [ "$PLAT" = tg5040 ]; then
  for c in 1 2 3; do echo 1 > /sys/devices/system/cpu/cpu$c/online 2>/dev/null; done
fi
echo "[driver] done launcher=$(pidof nextui.elf) online=$(cat /sys/devices/system/cpu/online)"
