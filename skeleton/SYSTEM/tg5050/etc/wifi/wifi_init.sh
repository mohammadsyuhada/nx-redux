#!/bin/sh

WIFI_INTERFACE="wlan0"
WPA_SUPPLICANT_CONF="/etc/wifi/wpa_supplicant/wpa_supplicant.conf"

start() {
	# Load WiFi driver module if not loaded
	if ! lsmod | grep -q aic8800_fdrv; then
		retries=0
		while [ $retries -lt 5 ]; do
			modprobe aic8800_fdrv 2>/dev/null
			sleep 0.5
			if lsmod | grep -q aic8800_fdrv; then
				break
			fi
			retries=$((retries + 1))
			sleep 1
		done
	fi

	# Wait for wlan0 interface to appear (driver may need time after module load)
	retries=0
	while [ $retries -lt 10 ]; do
		if [ -d "/sys/class/net/$WIFI_INTERFACE" ]; then
			break
		fi
		retries=$((retries + 1))
		sleep 0.5
	done

	if [ ! -d "/sys/class/net/$WIFI_INTERFACE" ]; then
		echo "wifi_init: $WIFI_INTERFACE did not appear after module load" >&2
		return 1
	fi

	# Unblock wifi via rfkill
	rfkill unblock wifi 2>/dev/null

	# Bring up the interface (with retry)
	retries=0
	while [ $retries -lt 5 ]; do
		ip link set $WIFI_INTERFACE up 2>/dev/null && break
		retries=$((retries + 1))
		sleep 0.5
	done

	mkdir -p /etc/wifi/sockets

	# Create default wpa_supplicant.conf if it doesn't exist
	if [ ! -f "$WPA_SUPPLICANT_CONF" ]; then
		mkdir -p "$(dirname "$WPA_SUPPLICANT_CONF")"
		cat > "$WPA_SUPPLICANT_CONF" << 'EOF'
# cat /etc/wifi/wpa_supplicant/wpa_supplicant.conf
ctrl_interface=/etc/wifi/sockets
disable_scan_offload=1
update_config=1
wowlan_triggers=any

EOF
	fi

	# Start wpa_supplicant if not running (with retry)
	if ! pidof wpa_supplicant > /dev/null 2>&1; then
		retries=0
		while [ $retries -lt 5 ]; do
			wpa_supplicant -B -i $WIFI_INTERFACE -c $WPA_SUPPLICANT_CONF -O /etc/wifi/sockets -D nl80211 2>/dev/null
			sleep 0.5
			if pidof wpa_supplicant > /dev/null 2>&1; then
				break
			fi
			retries=$((retries + 1))
			sleep 0.5
		done
	fi

	# Start DHCP client to obtain IP address
	# udhcpc -b exits after obtaining a lease, so just run it once
	if ! pgrep -f udhcpc > /dev/null 2>&1; then
		udhcpc -i $WIFI_INTERFACE -b 2>/dev/null
	fi
}

stop() {
	# Disconnect and disable
	wpa_cli -p /etc/wifi/sockets -i $WIFI_INTERFACE disconnect 2>/dev/null
	
	# Bring down interface
	ip link set $WIFI_INTERFACE down 2>/dev/null
	
	# Block wifi to save power
	rfkill block wifi 2>/dev/null

	# Kill wpa_supplicant
	killall wpa_supplicant 2>/dev/null

	# Kill DHCP client
	kill $(pgrep -f udhcpc) 2>/dev/null
}

case "$1" in
  start|"")
        start
        ;;
  stop)
        stop
        ;;
  *)
        echo "Usage: $0 {start|stop}"
        exit 1
esac