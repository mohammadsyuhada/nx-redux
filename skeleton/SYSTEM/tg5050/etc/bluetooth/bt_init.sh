#!/bin/sh
# Bluetooth initialization script for NX Redux
bt_hciattach="hciattach"
DEVICE_NAME="Trimui Smart Pro S (NX Redux)"

reset_bluetooth_power() {
	echo 0 > /sys/class/rfkill/rfkill0/state;
	sleep 1
	echo 1 > /sys/class/rfkill/rfkill0/state;
	sleep 1
}

start_hci_attach() {
	# Wait for btlpm proc entry to appear (created by aic8800_btlpm module)
	for i in $(seq 1 20); do
		[ -e /proc/bluetooth/sleep/btwrite ] && break
		usleep 100000
	done

	# After a suspend with a Bluetooth device connected, the aic8800 radio wakes
	# in a low-power state and only answers HCI once it sees a rising edge on
	# the BT wake line while it is powered. The old sequence raised the wake
	# line first and power-cycled the radio afterwards (stop_bt leaves rfkill0
	# off), so the edge landed on an unpowered radio and hciattach timed out:
	# hci0 never came back and Bluetooth could not be turned on until reboot.
	# Assert the wake line only after the radio is powered, and retry the whole
	# reset+attach a few times as insurance.
	attach_tries=3
	attempt=1
	while [ $attempt -le $attach_tries ]; do
		# Drop any stale hciattach still holding ttyAS1 and let it release.
		h=`ps | grep "$bt_hciattach" | grep -v grep`
		[ -n "$h" ] && { killall "$bt_hciattach"; sleep 1; }

		# De-assert the wake line, power-cycle the radio, then assert wake so
		# the radio sees the rising edge after it is powered.
		echo 0 > /proc/bluetooth/sleep/btwrite 2>/dev/null
		reset_bluetooth_power
		echo 1 > /proc/bluetooth/sleep/btwrite 2>/dev/null

		"$bt_hciattach" -n ttyAS1 aic >/dev/null 2>&1 &

		wait_hci0_count=0
		while [ $wait_hci0_count -lt 70 ]; do
			[ -d /sys/class/bluetooth/hci0 ] && return 0
			usleep 100000
			wait_hci0_count=$((wait_hci0_count + 1))
		done
		echo "hci0 attach attempt $attempt failed"
		attempt=$((attempt + 1))
	done
	echo "bring up hci0 failed after $attach_tries attempts"
	return 1
}

start_bt() {
	# Load BT driver module if not loaded
	# Looks like this also needs the wifi driver module loaded for proper operation
	if ! lsmod | grep -q aic8800_fdrv; then
		modprobe aic8800_fdrv 2>/dev/null
		sleep 0.5
	fi
	if ! lsmod | grep -q aic8800_btlpm; then
		modprobe aic8800_btlpm 2>/dev/null
		sleep 1
	fi

	# Wait for SDIO driver to finish probing (BT and WiFi share the aic8800 chip)
	# wlan0 appearing means the shared driver is fully initialized
	for i in $(seq 1 20); do
		[ -d /sys/class/net/wlan0 ] && break
		sleep 0.5
	done

	# Only skip the attach when a controller is actually registered. The old
	# [ -d /sys/class/bluetooth/hci0 ] test is unreliable: when hciattach dies
	# while hci0 is still up, that sysfs directory lingers ~2 s (until the
	# kernel's HCI close times out) while the device is already gone, so
	# start_bt would wrongly decide "already up" and never re-attach.
	if hciconfig hci0 >/dev/null 2>&1; then
		echo "Bluetooth init has been completed!!"
	else
		start_hci_attach
	fi

	# Start bluetooth daemon if not running
    d=`ps | grep bluetoothd | grep -v grep`
	[ -z "$d" ] && {
		/etc/bluetooth/bluetoothd start
		sleep 1
    }

	# Start bluealsa if not running
	a=`ps | grep bluealsa | grep -v grep`
	[ -z "$a" ] && {
		# bluealsa -p a2dp-source --keep-alive=-1 &
		bluealsa -p a2dp-source &
		sleep 1
    }

	# Always (re)apply adapter state. This used to live inside the "bluealsa
	# not running" block, so a re-attach while bluealsa was already alive (the
	# usual case after sleep/wake) left the freshly attached adapter powered
	# off with nothing to turn it back on.
	bluetoothctl power on 2>/dev/null
	bluetoothctl discoverable on 2>/dev/null
	bluetoothctl pairable on 2>/dev/null
	bluetoothctl agent NoInputNoOutput 2>/dev/null
	bluetoothctl default-agent 2>/dev/null
	bluetoothctl system-alias "$DEVICE_NAME" 2>/dev/null
}

stop_bt() {
	# stop bluealsa
	killall bluealsa 2>/dev/null

	# Stop bluetooth service
	d=`ps | grep bluetoothd | grep -v grep`
	[ -n "$d" ] && {
		# stop bluetoothctl
		bluetoothctl power off 2>/dev/null
		#bluetoothctl discoverable off 2>/dev/null
		bluetoothctl pairable off 2>/dev/null
		#bluetoothctl remove $(bluetoothctl devices | awk '{print $2}') 2>/dev/null
		killall bluetoothctl 2>/dev/null
		killall bluetoothd
		sleep 1
	}

	t=`ps | grep hcidump | grep -v grep`
	[ -n "$t" ] && {
		killall hcidump
	}
	# xr819s_stop
	hciconfig hci0 down
	h=`ps | grep "$bt_hciattach" | grep -v grep`
	[ -n "$h" ] && {
		killall "$bt_hciattach"
		usleep 500000
	}
	echo 0 > /proc/bluetooth/sleep/btwrite 2>/dev/null
	echo 0 > /sys/class/rfkill/rfkill0/state;
	echo "stop bluetoothd and hciattach"
}

case "$1" in
	start)
		start_bt
		;;
	stop)
		stop_bt
		;;
	restart)
		stop_bt
		sleep 0.5
		start_bt
		;;
	*)
		echo "Usage: $0 {start|stop|restart}"
		exit 1
		;;
esac

exit 0