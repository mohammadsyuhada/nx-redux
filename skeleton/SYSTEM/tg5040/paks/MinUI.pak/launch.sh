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

export PLATFORM="tg5040"
export SDCARD_PATH="/mnt/SDCARD"
export BIOS_PATH="$SDCARD_PATH/Bios"
export ROMS_PATH="$SDCARD_PATH/Roms"
export SAVES_PATH="$SDCARD_PATH/Saves"
export CHEATS_PATH="$SDCARD_PATH/Cheats"
export SYSTEM_PATH="$SDCARD_PATH/.system/$PLATFORM"
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
	reboot_next
	exit 0
fi

#######################################

mkdir -p "$BIOS_PATH"
mkdir -p "$ROMS_PATH"
mkdir -p "$SAVES_PATH"
mkdir -p "$CHEATS_PATH"
mkdir -p "$USERDATA_PATH"
mkdir -p "$LOGS_PATH"
mkdir -p "$SHARED_USERDATA_PATH/.minui"

export TRIMUI_MODEL=`strings /usr/trimui/bin/MainUI | grep ^Trimui`
if [ "$TRIMUI_MODEL" = "Trimui Brick" ]; then
	export DEVICE="brick"
elif [ "$TRIMUI_MODEL" = "Trimui Brick Pro" ]; then
	export DEVICE="brickpro"
else
	export DEVICE="smartpro"
fi

export IS_NEXT="yes"

#######################################

##Remove Old Led Daemon
if [ -f "/etc/LedControl" ]; then
	rm -Rf "/etc/LedControl" 2> /dev/null
fi
if [ -f "/etc/init.d/lcservice" ]; then
	/etc/init.d/lcservice disable
	rm /etc/init.d/lcservice 2> /dev/null
fi

# clear shadercache unconditionally, until it properly invalidates itself
rm -rf $SDCARD_PATH/.shadercache

#PD11 pull high for VCC-5v
echo 107 > /sys/class/gpio/export
echo -n out > /sys/class/gpio/gpio107/direction
echo -n 1 > /sys/class/gpio/gpio107/value

#rumble motor PH3
echo 227 > /sys/class/gpio/export
echo -n out > /sys/class/gpio/gpio227/direction
echo -n 0 > /sys/class/gpio/gpio227/value

if [ "$TRIMUI_MODEL" = "Trimui Smart Pro" ]; then
	#Left/Right Pad PD14/PD18
	echo 110 > /sys/class/gpio/export
	echo -n out > /sys/class/gpio/gpio110/direction
	echo -n 1 > /sys/class/gpio/gpio110/value

	echo 114 > /sys/class/gpio/export
	echo -n out > /sys/class/gpio/gpio114/direction
	echo -n 1 > /sys/class/gpio/gpio114/value
fi

#DIP Switch PH19
echo 243 > /sys/class/gpio/export
echo -n in > /sys/class/gpio/gpio243/direction

syslogd -S

#######################################

export LD_LIBRARY_PATH=$SYSTEM_PATH/lib:$SHARED_SYSTEM_PATH/lib:/usr/trimui/lib:$LD_LIBRARY_PATH
export PATH=$SYSTEM_PATH/bin:$SHARED_SYSTEM_PATH/bin:$PATH


# leds_off
echo 0 > /sys/class/led_anim/max_scale
if [ "$DEVICE" = "brick" ] || [ "$DEVICE" = "brickpro" ]; then
	echo 0 > /sys/class/led_anim/max_scale_lr
	echo 0 > /sys/class/led_anim/max_scale_f1f2
fi
if [ "$DEVICE" = "brickpro" ]; then
	# Brick Pro's fifth zone (triggers)
	echo 0 > /sys/class/led_anim/max_scale_rear
fi

# start the device's own stock gpio input daemon, by absolute path: Smart Pro
# and Brick wire their buttons differently, so a single vendored inputd can't
# serve both (v1.2.0 shipped one and broke all Smart Pro buttons). The tg5040
# OSD trigger is keymon's MENU long-press, which the stock inputd fully covers.
/usr/trimui/bin/trimui_inputd &

echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

keymon.elf & # &> $SDCARD_PATH/keymon.txt &

# Overlay-mount the SD card's OSD tree onto /usr/trimui/osd — the SD card is
# the source of truth for the OSD (daemon binary, assets, toast scripts,
# every widget). trimui_osdd has /usr/trimui/osd hardcoded in the
# closed-source binary, so it can't read from the SD card directly. tg5050
# overlay-mounts the SD tree itself as the lower layer, but tg5040's kernel
# 4.9 overlayfs rejects the exFAT SD card outright ("filesystem on '...' not
# supported" — its exfat driver's dentry revalidation isn't supported by
# overlayfs; hardware-verified on Brick 2026-07-26). So here the assembled
# tree (~1MB, no font) is staged into tmpfs first and the overlay is built
# from that staging copy instead of straight off the SD card. The copy lands
# in RAM, never on the rootfs, so rootfs writes stay at zero; SD edits take
# effect next boot via re-staging. Stock-only files the SD tree doesn't ship
# (regular.ttf, the 16MB CJK font) show through from the rootfs layer
# underneath.

# /etc writes below (bt/wifi init scripts) still need a writable rootfs
mount -o remount,rw /

OSD_DST="/usr/trimui/osd"
OSD_SRC="$SYSTEM_PATH/osd"
# The daemon binary and its resolution-locked assets (bg.png, block*.png:
# 1024x768/124px grid on Brick vs 1280x720/120px grid on Smart Pro) are
# model-specific and firmware builds are NOT interchangeable, so they live in
# an osd-$DEVICE layer stacked above the shared tree. No layer shipped for
# this model -> leave the firmware's OSD alone: the shared widgets alone have
# no daemon, no bg.png and no osdlayout.json, which would leave the OSD dead
# instead of merely un-themed. On staging or mount failure the daemon below
# still starts from whatever the rootfs holds; the marker records the
# failure for diagnosis.
OSD_SRC_MODEL="$SYSTEM_PATH/osd-$DEVICE"
if [ -d "$OSD_SRC_MODEL" ] && ! grep -q " $OSD_DST " /proc/mounts; then
	OSD_STAGE="/tmp/nx_osd"
	rm -rf "$OSD_STAGE"
	mkdir -p "$OSD_STAGE"
	if cp -r "$OSD_SRC/." "$OSD_STAGE/" && cp -r "$OSD_SRC_MODEL/." "$OSD_STAGE/"; then
		chmod +x "$OSD_STAGE/trimui_osdd" "$OSD_STAGE"/*.sh \
			"$OSD_STAGE"/widgets/*/*.sh "$OSD_STAGE"/widgets/app_music/pic2argb 2> /dev/null
		mount -t overlay overlay \
			-o ro,lowerdir="$OSD_STAGE:$OSD_DST" "$OSD_DST" \
			|| touch /tmp/nx_osd_mount_failed
	else
		touch /tmp/nx_osd_mount_failed
	fi
fi # end osd overlay mount

# Start OSD overlay daemon (system-wide quick menu)
if [ -x "$OSD_DST/trimui_osdd" ]; then
	cd "$OSD_DST" && ./trimui_osdd &
fi
cd "$SYSTEM_PATH/bin"

# Ensure .asoundrc is clean at boot — /etc/asound.conf handles speaker routing.
# audiomon will write .asoundrc when USB/BT devices connect.
rm -f $USERDATA_PATH/.asoundrc
audiomon.elf & # &> $SDCARD_PATH/audiomon.txt &

# BT handling
# NOTE: On tg5040 (xradio combo chip), running bluetoothd+hciattach degrades
# WiFi throughput from ~330 KB/s to ~2 KB/s due to coexistence interference.
# Only start BT when the user has it enabled.
bluetoothon=$(nextval.elf bluetooth | sed -n 's/.*"bluetooth": \([0-9]*\).*/\1/p')
# somehow trimui deploys aic?
cp -f $SYSTEM_PATH/etc/bluetooth/bt_init.sh /etc/bluetooth/bt_init.sh
if [ "$bluetoothon" -eq 1 ]; then
	/etc/bluetooth/bt_init.sh start > /dev/null 2>&1 &
else
	/etc/bluetooth/bt_init.sh stop > /dev/null 2>&1 &
fi

# wifi handling
# on by default, disable based on systemval setting
wifion=$(nextval.elf wifi | sed -n 's/.*"wifi": \([0-9]*\).*/\1/p')
cp -f $SYSTEM_PATH/etc/wifi/wifi_init.sh /etc/wifi/wifi_init.sh
if [ "$wifion" -eq 0 ]; then
	/etc/wifi/wifi_init.sh stop > /dev/null 2>&1 &
else 
	/etc/wifi/wifi_init.sh start > /dev/null 2>&1 &
fi

# SSH handling - developer setting
sshonboot=$(nextval.elf sshOnBoot | sed -n 's/.*"sshOnBoot": \([0-9]*\).*/\1/p')
if [ "$sshonboot" -eq 1 ]; then
	/etc/init.d/sshd start > /dev/null 2>&1 &
fi

#######################################

AUTO_PATH=$USERDATA_PATH/auto.sh
if [ -f "$AUTO_PATH" ]; then
	"$AUTO_PATH"
fi

cd $(dirname "$0")

#######################################

# kill show2.elf if running
killall -9 show2.elf > /dev/null 2>&1

EXEC_PATH="/tmp/nextui_exec"
NEXT_PATH="/tmp/next"
touch "$EXEC_PATH"  && sync
while [ -f $EXEC_PATH ]; do
	nextui.elf &> $LOGS_PATH/nextui.txt

	if [ -f $NEXT_PATH ]; then
		CMD=`cat $NEXT_PATH`
		eval $CMD
		rm -f $NEXT_PATH
		# Restore CPU state (games/tools may change governor and freq)
		echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
		echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null
		echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null
	fi

	if [ -f "/tmp/poweroff" ]; then
		poweroff_next
		exit 0
	fi
	if [ -f "/tmp/reboot" ]; then
		reboot_next
		exit 0
	fi
done

poweroff_next # just in case
