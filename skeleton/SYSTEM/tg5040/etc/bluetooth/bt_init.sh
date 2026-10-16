#!/bin/sh
# Taken from allwinner/btmanager/config/xradio_bt_init.sh for NXRedux
bt_hciattach="hciattach"
BT_RECONNECT_HELPER="$(dirname "$0")/bt_reconnect.sh"
# launch.sh copies this file alone to /etc; use the SD helper after reboot.
[ -r "$BT_RECONNECT_HELPER" ] || BT_RECONNECT_HELPER="${SYSTEM_PATH:-/mnt/SDCARD/.system}/etc/bluetooth/bt_reconnect.sh"
if [ -r "$BT_RECONNECT_HELPER" ]; then
	. "$BT_RECONNECT_HELPER"
else
	# Reconnect is optional; missing SD state must never suppress shutdown.
	bt_toggle_begin() { return 1; }
	bt_capture_audio() { return 0; }
	bt_token_current() { return 1; }
	bt_prepare_reconnect() { return 1; }
	bt_daemon_identity() { return 1; }
	bt_boot_claim() { return 1; }
	bt_boot_enabled() { return 1; }
fi
TRIMUI_MODEL=`strings /usr/trimui/bin/MainUI | grep ^Trimui`
DEVICE_NAME="$TRIMUI_MODEL (NX Redux)"

reset_bluetooth_power() {
	echo 0 > /sys/class/rfkill/rfkill0/state;
	sleep 1
	echo 1 > /sys/class/rfkill/rfkill0/state;
	sleep 1
}

start_hci_attach() {
	h=`ps | grep "$bt_hciattach" | grep -v grep`
	[ -n "$h" ] && {
		killall "$bt_hciattach"
	}

	echo 1 > /proc/bluetooth/sleep/btwrite
	reset_bluetooth_power

	(exec 9>&-; "$bt_hciattach" -n ttyS1 xradio) >/dev/null 2>&1 &

	wait_hci0_count=0
	while true
	do
		[ -d /sys/class/bluetooth/hci0 ] && break
		usleep 100000
		let wait_hci0_count++
		[ $wait_hci0_count -eq 70 ] && {
			echo "bring up hci0 failed"
			exit 1
		}
	done
}

start_bt() {
	BT_DAEMON_STARTED=0
	rfkill.elf unblock bluetooth

	if [ -d "/sys/class/bluetooth/hci0" ];then
		echo "Bluetooth init has been completed!!"
	else
		start_hci_attach
	fi
	
	# Start bluetooth daemon if not running
    d=`ps | grep bluetoothd | grep -v grep`
	[ -z "$d" ] && {
		(exec 9>&-; /etc/bluetooth/bluetoothd start)
		BT_DAEMON_STARTED=1
		sleep 1
    }

	a=`ps | grep bluealsa | grep -v grep`
	[ -z "$a" ] && {
		(exec 9>&-; bluealsa -p a2dp-source) &
		sleep 1
		# Power on adapter
		bluetoothctl power on 2>/dev/null
		
		# Set discoverable and pairable
		bluetoothctl discoverable on 2>/dev/null
		bluetoothctl pairable on 2>/dev/null
		
		# Set default agent for automatic pairing (no input/output)
		bluetoothctl agent NoInputNoOutput 2>/dev/null
		bluetoothctl default-agent 2>/dev/null
		
		# Set adapter name
		bluetoothctl system-alias "$DEVICE_NAME" 2>/dev/null
    }

}

start_reconnect_worker() {
	[ "$BT_DAEMON_STARTED" -eq 1 ] && bt_prepare_reconnect &&
		bt_daemon_identity && bt_token_current || return 0
	(exec 7>&- 8>&- 9>&-; sh "$BT_RECONNECT_HELPER" reconnect_worker "$BT_RECONNECT_TOKEN" \
		"$BT_RECONNECT_TARGET" "$BT_DAEMON_IDENTITY") >/dev/null 2>&1 &
}

start_boot_worker() {
	[ "${BT_BOOT_FIRST:-0}" -eq 1 ] && bt_token_current &&
		bt_boot_enabled && bt_valid_addr "$bt_boot_addr" &&
		[ ! -e "$BT_RECONNECT_STATE/boot-cancel" ] &&
		bt_daemon_identity || return 0
	(exec 7>&- 8>&- 9>&-; sh "$BT_RECONNECT_HELPER" reconnect_worker "$BT_RECONNECT_TOKEN" \
		"$bt_boot_addr" "$BT_DAEMON_IDENTITY" boot) >/dev/null 2>&1 &
}

ble_start() {
	rfkill.elf unblock bluetooth

	if [ -d "/sys/class/bluetooth/hci0" ];then
		echo "Bluetooth init has been completed!!"
	else
		start_hci_attach
	fi

	hci_is_up=`hciconfig hci0 | grep RUNNING`
	[ -z "$hci_is_up" ] && {
		hciconfig hci0 up
	}

	MAC_STR=`hciconfig | grep "BD Address" | awk '{print $3}'`
	LE_MAC=${MAC_STR/2/C}
	OLD_LE_MAC_T=`cat /sys/kernel/debug/bluetooth/hci0/random_address`
	OLD_LE_MAC=$(echo $OLD_LE_MAC_T | tr [a-z] [A-Z])
	if [ -n "$LE_MAC" ];then
		if [ "$LE_MAC" != "$OLD_LE_MAC" ];then
			hciconfig hci0 lerandaddr $LE_MAC
		else
			echo "the ble random_address has been set."
		fi
	fi
}

stop_bt() {
	bt_capture_audio
	# stop bluealsa
	killall bluealsa 2>/dev/null

	# Stop bluetooth service
	d=`ps | grep bluetoothd | grep -v grep`
	[ -n "$d" ] && {
		# stop bluetoothctl
		bluetoothctl power off 2>/dev/null
		bluetoothctl pairable off 2>/dev/null
		killall bluetoothctl 2>/dev/null
		killall bluetoothd
		sleep 1
	}

	# The stock S80hciattach starts hciattach at every boot, Bluetooth on or off, and launch.sh's stop lands while
	# it is still attaching (~10 s in). Killing it before hci0 is registered and opened frees the hci_uart under the
	# tty close (Oops in hci_uart_flush, then a panic or a watchdog reset): let it register hci0 first (the start
	# path's 7 s), so the down below serialises with its open and the kill comes after.
	h=`ps | grep "$bt_hciattach" | grep -v grep`
	[ -n "$h" ] && {
		wait_hci0_count=0
		while [ ! -d /sys/class/bluetooth/hci0 ] && [ $wait_hci0_count -lt 70 ]
		do
			usleep 100000
			let wait_hci0_count++
		done
	}

	hciconfig hci0 down
	t=`ps | grep hcidump | grep -v grep`
	[ -n "$t" ] && {
		killall hcidump
	}

	h=`ps | grep "$bt_hciattach" | grep -v grep`
	[ -n "$h" ] && {
		killall "$bt_hciattach"
		usleep 500000
	}
	echo 0 > /proc/bluetooth/sleep/btwrite
	echo 0 > /sys/class/rfkill/rfkill0/state;
	echo "stop bluetoothd and hciattach"
}

BT_BOOT_FIRST=0
if [ "${2:-}" = boot ]; then
	case "$1" in
		start|stop) bt_boot_claim && BT_BOOT_FIRST=1 ;;
	esac
fi

case "$1" in
	stop|restart)
		# BusyBox ash can exit on a failed exec redirection while opening the
		# lifecycle lock. Even then, the physical stack shutdown must run.
		trap 'stop_bt' 0
		;;
esac

case "$1" in
	start|stop|restart)
		if bt_toggle_begin; then
			:
		else
			bt_begin_status=$?
			# Superseded requests stop here. A failed lock/state check must
			# never suppress shutdown; startup fails closed so an old worker
			# cannot race a newly started daemon without lifecycle locking.
			if [ "$bt_begin_status" -eq 2 ]; then
				# A newer request owns this lifecycle; do not run our EXIT trap.
				trap - 0
				exit 0
			fi
			BT_RECONNECT_DISABLED=1
		fi
		;;
esac

case "$1" in
	start)
		if [ "${BT_RECONNECT_DISABLED:-0}" -ne 0 ]; then
			echo "Bluetooth start blocked: reconnect lifecycle state unavailable" >&2
			exit 1
		fi
		start_bt
		if [ "${2:-}" = boot ]; then
			start_boot_worker
		else
			start_reconnect_worker
		fi
		;;
	stop)
		stop_bt
		trap - 0
		;;
	restart)
		stop_bt
		trap - 0
		if [ "${BT_RECONNECT_DISABLED:-0}" -ne 0 ]; then
			echo "Bluetooth restart stopped; startup blocked: reconnect lifecycle state unavailable" >&2
			exit 1
		fi
		sleep 0.5
		start_bt
		BT_RECONNECT_PREVIOUS_TOKEN=$BT_RECONNECT_TOKEN
		start_reconnect_worker
		;;
  *)
		echo "Usage: $0 {start|stop|restart}"
		exit 1
		;;
esac
