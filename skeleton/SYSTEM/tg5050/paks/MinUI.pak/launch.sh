#!/bin/sh
# MinUI.pak

# recover from readonly SD card -------------------------------
# touch /mnt/writetest
# sync
# if [ -f /mnt/writetest ] ; then
# 	rm -f /mnt/writetest
# else
# 	e2fsck -p /dev/root > /mnt/SDCARD/RootRecovery.txt
# 	reboot
# fi

export PLATFORM="tg5050"
export SDCARD_PATH="/mnt/SDCARD"
export BIOS_PATH="$SDCARD_PATH/Bios"
export ROMS_PATH="$SDCARD_PATH/Roms"
export SAVES_PATH="$SDCARD_PATH/Saves"
export CHEATS_PATH="$SDCARD_PATH/Cheats"
export SYSTEM_PATH="$SDCARD_PATH/.system"
export CORES_PATH="$SYSTEM_PATH/cores"
export USERDATA_PATH="$SDCARD_PATH/.userdata/$PLATFORM"
export SHARED_USERDATA_PATH="$SDCARD_PATH/.userdata/shared"
export LOGS_PATH="$USERDATA_PATH/logs"
export DATETIME_PATH="$SHARED_USERDATA_PATH/datetime.txt"
export SHARED_SYSTEM_PATH="$SDCARD_PATH/.system/shared"
export HOME="$USERDATA_PATH"

#######################################

if [ -f "/tmp/poweroff" ]; then
	poweroff_next
	exit 0
fi
if [ -f "/tmp/reboot" ]; then
	reboot
	exit 0
fi

#######################################

mkdir -p "$BIOS_PATH"
mkdir -p "$ROMS_PATH"
mkdir -p "$SAVES_PATH"
mkdir -p "$CHEATS_PATH"
mkdir -p "$USERDATA_PATH"
mkdir -p "$SHARED_USERDATA_PATH/.minui"

export TRIMUI_MODEL=`strings /usr/trimui/bin/MainUI | grep ^Trimui`
if [ "$TRIMUI_MODEL" = "Trimui Smart Pro S" ]; then
	export DEVICE="smartpros"
fi

export IS_NEXT="yes"

#######################################

# NOTE: stock's launch sequence did `sync + drop_caches` here; deliberately
# not carried over — evicting the page cache right before nextui (the biggest
# reader of the boot) made everything reload cold and slowed boot down.

#5V enable
# echo 335 > /sys/class/gpio/export
# echo -n out > /sys/class/gpio/gpio335/direction
# echo -n 1 > /sys/class/gpio/gpio335/value

#rumble motor (PWM motor driver, 16-bit level 0-65535)
echo 0 > /sys/class/motor/level

#Left/Right Pad PK12/PK16 , run in trimui_inputd
# echo 332 > /sys/class/gpio/export
# echo -n out > /sys/class/gpio/gpio332/direction
# echo -n 1 > /sys/class/gpio/gpio332/value

# echo 336 > /sys/class/gpio/export
# echo -n out > /sys/class/gpio/gpio336/direction
# echo -n 1 > /sys/class/gpio/gpio336/value

#DIP Switch PL11 , run in trimui_inputd
# echo 363 > /sys/class/gpio/export
# echo -n in > /sys/class/gpio/gpio363/direction

#syslogd -S

#######################################

export LD_LIBRARY_PATH=$SYSTEM_PATH/lib:$SHARED_SYSTEM_PATH/lib:/usr/trimui/lib:$LD_LIBRARY_PATH
export PATH=$SYSTEM_PATH/bin:$SHARED_SYSTEM_PATH/bin:$PATH


echo before leds `cat /proc/uptime` >> /tmp/nextui_boottime

# leds_off
echo 0 > /sys/class/led_anim/max_scale

# start gpio input daemon
trimui_inputd &

echo schedutil > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq

echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

#LITTLE_PATH=/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
#CPU_SPEED_PERF_LITTLE=2000000
#echo $CPU_SPEED_PERF_LITTLE > $LITTLE_PATH



# Very little libretro cores profit from multithreading, even stock OS is 
# only very seldomly using more than 1+2 cores. Use as a baseline, the 
# higher-end cores can just enable more cores themselves if needed.

# little Cortex-A55 CPU0 - 408Mhz to 1416Mhz
echo 1 > /sys/devices/system/cpu/cpu0/online
echo 1 > /sys/devices/system/cpu/cpu1/online

echo 0 > /sys/devices/system/cpu/cpu3/online
echo 0 > /sys/devices/system/cpu/cpu2/online

# big Cortex-A55 CPU4 - 408Mhz to 2160Mhz
echo 1 > /sys/devices/system/cpu/cpu4/online

echo 0 > /sys/devices/system/cpu/cpu7/online
echo 0 > /sys/devices/system/cpu/cpu6/online
echo 0 > /sys/devices/system/cpu/cpu5/online

keymon.elf & # &> $SDCARD_PATH/keymon.txt &

# Overlay-mount the SD card's OSD tree onto /usr/trimui/osd — the SD card is
# the source of truth for the OSD (daemon binary, assets, toast scripts,
# every widget). trimui_osdd has /usr/trimui/osd hardcoded in the
# closed-source binary, so it can't read from the SD card directly; a
# read-only overlay mount presents the SD tree at that path without ever
# writing to the rootfs. Stock-only files the SD tree doesn't ship
# (regular.ttf, the 16MB CJK font) show through from the rootfs layer
# underneath. On mount failure the daemon below still starts from whatever
# the rootfs holds; the marker records the failure for diagnosis.

# /etc writes below (wifi init script) still need a writable rootfs
mount -o remount,rw /

# One-time rootfs patch: stop stock rcS from starting sshd. launch.sh fully
# owns sshd via the sshOnBoot setting (below), and the stock start blocked
# every boot ~1.8s waiting on kernel entropy — only for tg5050.sh to kill it
# again. Original rcS is kept as rcS.nxbak; restore-to-stock puts it back.
if ! grep -q nx_skip_stock_sshd /etc/init.d/rcS; then
	cp -f /etc/init.d/rcS /etc/init.d/rcS.nxbak
	sed -i '/\[ ! -f "\$i" \] && continue/a\
\
     # nx_skip_stock_sshd: sshd is managed by MinUI.pak/launch.sh (sshOnBoot\
     # setting); the stock start blocked boot ~1.8s waiting on kernel entropy.\
     [ "${i##*/}" = "S50sshd" ] && continue' /etc/init.d/rcS
fi

OSD_DST="/usr/trimui/osd"
OSD_SRC="$SYSTEM_PATH/osd"
# Theme-accent copies of the daemon's active slider / toast frame images
# (no-op for the default theme; the white focus ring is left as shipped) go
# into a tmpfs layer stacked above the SD tree, so the card itself is never
# written. Must run before trimui_osdd loads its images.
OSD_TINT="/tmp/nx_osd_tint"
rm -rf "$OSD_TINT"
mkdir -p "$OSD_TINT"
"$SYSTEM_PATH/bin/osdmusic.elf" --tint-osd "$OSD_SRC" "$OSD_TINT" 2> /dev/null
if ! grep -q " $OSD_DST " /proc/mounts; then
	# overlayfs takes the SD tree as a layer directly when the card is exFAT
	# (the firmware mounts those through FUSE), but refuses a FAT32 card:
	# vfat dentries carry their own hash/compare ops, which overlayfs rejects
	# outright ("filesystem on ... not supported"). When the direct mount
	# fails, stage the SD tree (~640 KB) into tmpfs the way tg5040 does and
	# mount that instead. vfat carries no exec bits, hence the chmod.
	if ! mount -t overlay overlay \
		-o ro,lowerdir="$OSD_TINT:$OSD_SRC:$OSD_DST" "$OSD_DST" 2> /dev/null; then
		OSD_STAGE="/tmp/nx_osd"
		rm -rf "$OSD_STAGE"
		mkdir -p "$OSD_STAGE"
		if cp -r "$OSD_SRC/." "$OSD_STAGE/"; then
			chmod +x "$OSD_STAGE/trimui_osdd" "$OSD_STAGE/trimui_osdd.xbox" "$OSD_STAGE"/*.sh \
				"$OSD_STAGE"/widgets/*/*.sh 2> /dev/null
			mount -t overlay overlay \
				-o ro,lowerdir="$OSD_TINT:$OSD_STAGE:$OSD_DST" "$OSD_DST" \
				|| touch /tmp/nx_osd_mount_failed
		else
			touch /tmp/nx_osd_mount_failed
		fi
	fi
fi # end osd overlay mount

# Start OSD overlay daemon (system-wide quick menu). Settings > System >
# Button layout = Xbox needs the daemon's OK/cancel buttons swapped; that is
# a separately patched copy (scripts/patch-osdd-layout.sh), picked here at
# boot — the setting applies after a restart, like everywhere else.
OSDD=trimui_osdd
if [ "$(nextval.elf buttonlayout | sed -n 's/.*"buttonlayout": \([0-9]*\).*/\1/p')" = "1" ] \
	&& [ -x "$OSD_DST/trimui_osdd.xbox" ]; then
	OSDD=trimui_osdd.xbox
fi
if [ -x "$OSD_DST/$OSDD" ]; then
	# libosdwait.so (workspace/all/osdwait) interposes the daemon's SDL_WaitEvent:
	# with no SDL window to block on, SDL 2.30 polls every 1 ms (~920 wakeups/s,
	# ~4.5% of a core while the panel is hidden and idle); the shim polls every
	# 20 ms while hidden and 1 ms while shown. Bare start if the library is absent.
	OSDWAIT="$SYSTEM_PATH/lib/libosdwait.so"
	if [ -f "$OSDWAIT" ]; then
		cd "$OSD_DST" && LD_PRELOAD="$OSDWAIT" ./$OSDD &
	else
		cd "$OSD_DST" && ./$OSDD &
	fi
fi
cd "$SYSTEM_PATH/bin"

# Ensure .asoundrc is clean at boot — /etc/asound.conf handles speaker routing.
# audiomon will write .asoundrc when USB/BT devices connect.
rm -f $USERDATA_PATH/.asoundrc /tmp/nx_audio_sink
audiomon.elf & #&> $SDCARD_PATH/audiomon.txt &
# Debug logging (Developer setting). App/game logs are redirected by every
# pak's launch.sh through $LOGS_PATH. When the setting is off, point that at
# tmpfs (wiped before every launch) so nothing lands on the SD card; when on,
# keep the persistent .userdata/<plat>/logs directory. Re-evaluated before
# each launch so toggling it in Settings applies without a reboot.
nx_update_logs_path() {
	dbglog=$(nextval.elf debugLogging | sed -n 's/.*"debugLogging": \([0-9]*\).*/\1/p')
	if [ "$dbglog" = "1" ]; then
		export NX_DEBUG_LOGGING=1
		export LOGS_PATH="$USERDATA_PATH/logs"
		rm -rf /tmp/nx-logs
	else
		export NX_DEBUG_LOGGING=0
		export LOGS_PATH="/tmp/nx-logs"
		rm -rf "$LOGS_PATH"
	fi
	mkdir -p "$LOGS_PATH"
}
nx_update_logs_path
# musicplayerd runs for the whole session, so its log cannot live in the
# per-launch tmpfs dir (the wipe would unlink its open file); when debug
# logging is off it goes to /dev/null instead.
MPD_LOG=/dev/null
[ "$NX_DEBUG_LOGGING" = "1" ] && MPD_LOG="$LOGS_PATH/music-playerd.txt"
musicplayerd.elf </dev/null >> "$MPD_LOG" 2>&1 &

# Boot-time radio/ssh init below is backgrounded; the launcher runs the CPU at
# full range until all of it has exited, polling this marker (15 s fallback —
# see workspace/all/nextui/cpu_policy.h). /tmp is tmpfs, so the marker never
# outlives a boot; the rm is belt and braces. A relaunch after a game or tool
# finds the marker present and caps at its first frame as before.
BOOT_DONE_MARKER=/tmp/nx_boot_done
rm -f "$BOOT_DONE_MARKER"
BOOT_JOBS=""

# wifi handling
wifion=$(nextval.elf wifi | sed -n 's/.*"wifi": \([0-9]*\).*/\1/p')
cp -f $SYSTEM_PATH/etc/wifi/wifi_init.sh /etc/wifi/wifi_init.sh
if [ "$wifion" -eq 1 ]; then
	/etc/wifi/wifi_init.sh start > /dev/null 2>&1 &
	BOOT_JOBS="$BOOT_JOBS $!"
fi
echo after wifi `cat /proc/uptime` >> /tmp/nextui_boottime

# BT handling — always start bluetoothd so bluetoothctl commands never hang.
# If BT is off, the adapter gets powered off but the daemon stays alive.
bluetoothon=$(nextval.elf bluetooth | sed -n 's/.*"bluetooth": \([0-9]*\).*/\1/p')
cp -f $SYSTEM_PATH/etc/bluetooth/bt_init.sh /etc/bluetooth/bt_init.sh
# Power the adapter off only after bt_init.sh start has finished. It used to
# be a detached "sleep 5; power off": the start script spends ~7 s loading the
# aic8800 modules and attaching, so that power-off hit a controller that did
# not exist yet and the script's own "power on" landed afterwards, turning
# Bluetooth on at every boot for users who had it off.
(
	/etc/bluetooth/bt_init.sh start
	if [ "$bluetoothon" -ne 1 ]; then
		bluetoothctl power off
	fi
) > /dev/null 2>&1 &
BOOT_JOBS="$BOOT_JOBS $!"
echo after bluetooth `cat /proc/uptime` >> /tmp/nextui_boottime

# SSH handling - developer setting
sshonboot=$(nextval.elf sshOnBoot | sed -n 's/.*"sshOnBoot": \([0-9]*\).*/\1/p')
if [ "$sshonboot" -eq 1 ]; then
	/etc/init.d/S50sshd start > /dev/null 2>&1 &
	BOOT_JOBS="$BOOT_JOBS $!"
else
	# Stop SSH started by stock init system (S50sshd)
	/etc/init.d/S50sshd stop > /dev/null 2>&1
fi

# Touch the marker once every boot job above has exited (kill -0 = alive).
(
	for p in $BOOT_JOBS; do
		while kill -0 $p 2>/dev/null; do sleep 0.2; done
	done
	touch "$BOOT_DONE_MARKER"
) > /dev/null 2>&1 &

#######################################

AUTO_PATH=$USERDATA_PATH/auto.sh
if [ -f "$AUTO_PATH" ]; then
	echo before auto.sh `cat /proc/uptime` >> /tmp/nextui_boottime
	"$AUTO_PATH"
	echo after auto.sh `cat /proc/uptime` >> /tmp/nextui_boottime
fi

cd $(dirname "$0")

#######################################

# kill show2.elf if running
killall -9 show2.elf > /dev/null 2>&1

EXEC_PATH="/tmp/nextui_exec"
NEXT_PATH="/tmp/next"
touch "$EXEC_PATH"  && sync
while [ -f $EXEC_PATH ]; do
	nx_update_logs_path
	nextui.elf &> "$LOGS_PATH/nextui.txt"

	if [ -f $NEXT_PATH ]; then
		CMD=`cat $NEXT_PATH`
		nx_update_logs_path
		# The launcher takes the big core offline while it runs (see
		# nextui/cpu_policy.h). Every pak expects cpu4 up — N64 pins its
		# emulator to it, DC/NDS/PS set its clocks, minarch drives policy4 —
		# so bring it back in the boot-default state before handing over.
		echo 1 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
		echo schedutil > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor 2>/dev/null
		echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq 2>/dev/null
		echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null
		eval $CMD
		rm -f $NEXT_PATH
		# Restore CPU state (games/tools may change governor, freq, and cores)
		echo 0 > /sys/devices/system/cpu/cpu2/online 2>/dev/null
		echo 0 > /sys/devices/system/cpu/cpu3/online 2>/dev/null
		echo 0 > /sys/devices/system/cpu/cpu5/online 2>/dev/null
		echo 0 > /sys/devices/system/cpu/cpu6/online 2>/dev/null
		echo 0 > /sys/devices/system/cpu/cpu7/online 2>/dev/null
		echo schedutil > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor 2>/dev/null
		echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq 2>/dev/null
		echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null
		echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
		echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null
		echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null
	fi

	if [ -f "/tmp/poweroff" ]; then
		musicplayerctl.elf shutdown >/dev/null 2>&1 || true
		poweroff_next
		exit 0
	fi
	if [ -f "/tmp/reboot" ]; then
		musicplayerctl.elf shutdown >/dev/null 2>&1 || true
		reboot
		exit 0
	fi
done

musicplayerctl.elf shutdown >/dev/null 2>&1 || true
poweroff_next # just in case
