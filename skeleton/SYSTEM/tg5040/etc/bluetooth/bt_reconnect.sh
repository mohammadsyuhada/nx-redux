#!/bin/sh
# Recall only the bonded, trusted audio sink connected before a stack stop (toggle or suspend).
# Toggle/suspend selection is volatile; boot selection is a separate explicit opt-in.
BT_RECONNECT_STATE=${BT_RECONNECT_STATE:-/tmp/nx-bt-reconnect}
BT_BOOT_DIR=${USERDATA_PATH:-/mnt/SDCARD/.userdata/tg5040}
BT_BOOT_FILE="$BT_BOOT_DIR/bt-boot-audio"

bt_valid_addr() {
	printf '%s\n' "$1" | LC_ALL=C grep -Eq '^([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}$'
}

bt_property_yes() {
	printf '%s\n' "$1" | awk -v key="$2" '
		{ sub(/\r$/, ""); sub(/^[ \t]+/, ""); sub(/[ \t]+$/, "") }
		$0 == key ": yes" { found=1 }
		END { exit !found }'
}

bt_audio_sink() {
	# Check a UUID property, not an arbitrary substring in a device name.
	printf '%s\n' "$1" | LC_ALL=C grep -Eq '^[[:space:]]*UUID:.*\(0000110b-0000-1000-8000-00805f9b34fb\)[[:space:]]*$'
}

bt_token_current() {
	[ "${BT_RECONNECT_DISABLED:-0}" -eq 0 ] || return 1
	[ "$(cat "$BT_RECONNECT_STATE/token" 2>/dev/null)" = "$BT_RECONNECT_TOKEN" ]
}

# Boot may synchronize the RTC/wall clock. Uptime cannot jump backwards on NTP.
bt_now() {
	IFS=' ' read -r bt_uptime bt_idle < /proc/uptime || return 1
	bt_uptime=${bt_uptime%%.*}
	case "$bt_uptime" in ''|*[!0-9]*) return 1 ;; esac
	printf '%s\n' "$bt_uptime"
}

bt_seconds_left() {
	BT_WAIT=$((BT_DEADLINE - $(bt_now)))
	[ "$BT_WAIT" -gt 0 ] || return 1
	[ "$BT_WAIT" -le "$1" ] || BT_WAIT=$1
}

bt_state_prepare() {
	if [ ! -d "$BT_RECONNECT_STATE" ]; then
		(umask 077; mkdir "$BT_RECONNECT_STATE") || [ -d "$BT_RECONNECT_STATE" ] || return 1
	fi
	[ ! -L "$BT_RECONNECT_STATE" ] || return 1
	[ "$(ls -nd "$BT_RECONNECT_STATE" | awk '{print $3}')" = "$(id -u)" ] || return 1
	chmod 700 "$BT_RECONNECT_STATE" || return 1
}

# Atomic record: enabled flag and last successfully connected audio address.
# Missing/malformed means Off. Never source user data as shell commands.
bt_boot_read() {
	bt_boot_on=0; bt_boot_addr=-
	[ -d "$BT_BOOT_DIR" ] && [ ! -L "$BT_BOOT_DIR" ] || return 1
	[ -f "$BT_BOOT_FILE" ] && [ ! -L "$BT_BOOT_FILE" ] || return 1
	bt_record=$(cat "$BT_BOOT_FILE" 2>/dev/null) || return 1
	printf '%s\n' "$bt_record" | LC_ALL=C grep -Eq '^[01] (-|([[:xdigit:]]{2}:){5}[[:xdigit:]]{2})$' || return 1
	# Reject multiple records, even individually valid ones.
	[ "$(printf '%s\n' "$bt_record" | wc -l)" -eq 1 ] || return 1
	bt_boot_on=${bt_record%% *}; bt_boot_addr=${bt_record#* }
}

bt_boot_enabled() { bt_boot_read && [ "$bt_boot_on" = 1 ]; }

bt_pref_lock() {
	bt_state_prepare || return 1
	[ ! -L "$BT_RECONNECT_STATE/pref-lock" ] && [ ! -d "$BT_RECONNECT_STATE/pref-lock" ] || return 1
	(umask 077; touch "$BT_RECONNECT_STATE/pref-lock") || return 1
	exec 7>"$BT_RECONNECT_STATE/pref-lock" || return 1
	busybox flock -n 7
}

bt_boot_write() {
	[ -d "$BT_BOOT_DIR" ] && [ ! -L "$BT_BOOT_DIR" ] || return 1
	[ ! -L "$BT_BOOT_FILE" ] && [ ! -d "$BT_BOOT_FILE" ] || return 1
	(umask 077; set -C; printf '%s %s\n' "$bt_boot_on" "$bt_boot_addr" > "$BT_BOOT_FILE.$$") || return 1
	mv -f "$BT_BOOT_FILE.$$" "$BT_BOOT_FILE" || return 1
	bt_boot_read
}

bt_boot_remember() {
	bt_valid_addr "$1" || return 1
	bt_info=$(bt_ctl 2 info "$1" 2>/dev/null) || return 1
	bt_property_yes "$bt_info" Connected && bt_property_yes "$bt_info" Bonded &&
		bt_property_yes "$bt_info" Trusted && bt_audio_sink "$bt_info" || return 0
	bt_pref_lock || return 1
	bt_boot_read || :
	bt_boot_addr=$1
	bt_boot_write
}

bt_boot_lock() {
	bt_state_prepare || return 1
	busybox flock --help >/dev/null 2>&1 || return 1
	[ ! -L "$BT_RECONNECT_STATE/boot-lock" ] && [ ! -d "$BT_RECONNECT_STATE/boot-lock" ] || return 1
	(umask 077; touch "$BT_RECONNECT_STATE/boot-lock") || return 1
	exec 8>"$BT_RECONNECT_STATE/boot-lock" || return 1
}

bt_boot_cancel() {
	bt_boot_lock || return 1
	# This inhibitor lasts until reboot, including when a worker has not spawned yet.
	[ ! -L "$BT_RECONNECT_STATE/boot-cancel" ] || return 1
	(umask 077; : > "$BT_RECONNECT_STATE/boot-cancel") || return 1
	bt_cancel_tries=0
	while ! busybox flock -n 8; do
		bt_cancel_tries=$((bt_cancel_tries + 1))
		[ "$bt_cancel_tries" -lt 120 ] || { exec 8>&-; return 1; }
		sleep 0.1
	done
	exec 8>&-
}

bt_boot_seed() {
	# Upgrade seed only: without history, use a unique currently connected sink.
	# Ambiguity/absence leaves an empty target until the next explicit connection.
	BT_DEADLINE=$(( $(bt_now) + 6 ))
	bt_devices=$(bt_ctl 2 devices Connected 2>/dev/null) || return 0
	bt_selected=
	for bt_addr in $(printf '%s\n' "$bt_devices" | awk '$1 == "Device" {print $2}'); do
		bt_seconds_left 2 || return 0
		bt_valid_addr "$bt_addr" || continue
		bt_info=$(bt_ctl "$BT_WAIT" info "$bt_addr" 2>/dev/null) || return 0
		if bt_property_yes "$bt_info" Connected && bt_property_yes "$bt_info" Bonded &&
			bt_property_yes "$bt_info" Trusted && bt_audio_sink "$bt_info"; then
			[ -z "$bt_selected" ] || return 0
			bt_selected=$bt_addr
		fi
	done
	[ -z "$bt_selected" ] || bt_boot_addr=$bt_selected
}

bt_boot_set() {
	case "$1" in on|off) ;; *) return 1 ;; esac
	bt_boot_cancel || return 1
	bt_pref_lock || return 1
	bt_boot_read || :
	if [ "$1" = on ]; then
		[ "$bt_boot_addr" != - ] || bt_boot_seed
		bt_boot_on=1
	else
		bt_boot_on=0
	fi
	bt_boot_write
}

bt_toggle_begin() {
	bt_state_prepare || return 1
	BT_RECONNECT_TOKEN=$$
	(umask 077; printf '%s\n' "$BT_RECONNECT_TOKEN" > "$BT_RECONNECT_STATE/token.$$") || return 1
	mv -f "$BT_RECONNECT_STATE/token.$$" "$BT_RECONNECT_STATE/token" || return 1
	# The target BusyBox provides flock; the kernel drops this lock even on SIGKILL.
	busybox flock --help >/dev/null 2>&1 || return 1
	[ ! -d "$BT_RECONNECT_STATE/lock" ] && [ ! -L "$BT_RECONNECT_STATE/lock" ] || return 1
	(umask 077; touch "$BT_RECONNECT_STATE/lock") || return 1
	chmod 600 "$BT_RECONNECT_STATE/lock" || return 1
	exec 9>"$BT_RECONNECT_STATE/lock" || return 1
	bt_lock_tries=0
	while ! busybox flock -n 9; do
		if ! bt_token_current; then
			exec 9>&-
			return 2
		fi
		bt_lock_tries=$((bt_lock_tries + 1))
		if [ "$bt_lock_tries" -ge 200 ]; then
			exec 9>&-
			echo "Bluetooth toggle busy" >&2
			return 3
		fi
		sleep 0.1
	done
	bt_token_current || return 2
}

bt_current_pid() {
	# Builtin read opens proc/self for this shell, including command substitutions.
	{ IFS= read -r bt_self_stat < /proc/self/stat; } 2>/dev/null || return 1
	BT_CURRENT_PID=${bt_self_stat%% *}
}

bt_process_identity() {
	BT_PROCESS_IDENTITY=
	bt_identity_pid=$1
	bt_identity_parent=$2
	{ IFS= read -r bt_proc_stat < "/proc/$bt_identity_pid/stat"; } 2>/dev/null || return 1
	[ "${bt_proc_stat%% *}" = "$bt_identity_pid" ] || return 1
	# Strip the complete parenthesized comm field, which may contain spaces.
	bt_proc_fields=${bt_proc_stat##*) }
	set -- $bt_proc_fields
	[ "$2" = "$bt_identity_parent" ] || return 1
	[ "$#" -ge 20 ] || return 1
	shift 19
	case "$1" in ''|*[!0-9]*) return 1 ;; esac
	BT_PROCESS_IDENTITY=$1
}

bt_kill_identity() {
	[ -n "$3" ] || return 1
	bt_process_identity "$1" "$2" || return 1
	[ "$BT_PROCESS_IDENTITY" = "$3" ] || return 1
	kill -KILL "$1" 2>/dev/null
}

# bluetoothctl starts its internal timeout after readiness and may buffer
# output until it expires. Use one external deadline for the exact child.
bt_run_bounded() {
	bt_limit=$1
	shift
	bt_current_pid || return 1
	bt_parent=$BT_CURRENT_PID
	(exec 7>&- 8>&- 9>&-; exec "$@") &
	bt_child=$!
	bt_process_identity "$bt_child" "$bt_parent"
	bt_child_identity=$BT_PROCESS_IDENTITY
	(
		exec 7>&- 8>&- 9>&-
		busybox sleep "$bt_limit"
		bt_kill_identity "$bt_child" "$bt_parent" "$bt_child_identity"
	) >/dev/null 2>&1 &
	bt_watchdog=$!
	bt_process_identity "$bt_watchdog" "$bt_parent"
	bt_watchdog_identity=$BT_PROCESS_IDENTITY
	wait "$bt_child" 2>/dev/null
	bt_result=$?
	bt_kill_identity "$bt_watchdog" "$bt_parent" "$bt_watchdog_identity"
	wait "$bt_watchdog" 2>/dev/null
	return "$bt_result"
}

bt_ctl() {
	bt_limit=$1
	shift
	bt_run_bounded "$bt_limit" bluetoothctl "$@"
}

bt_capture_audio() {
	bt_token_current || return 0
	# A redundant stop while already off must not erase the previous selection.
	bt_show=$(bt_ctl 2 show 2>/dev/null)
	bt_property_yes "$bt_show" Powered || return 0
	bt_token_current || return 0
	rm -f "$BT_RECONNECT_STATE/audio"
	BT_DEADLINE=$(( $(bt_now) + 6 ))
	bt_devices=$(bt_ctl 2 devices Connected 2>/dev/null)
	bt_selected=
	for bt_addr in $(printf '%s\n' "$bt_devices" | awk '$1 == "Device" {print $2}'); do
		bt_token_current && bt_seconds_left 2 || return 0
		bt_valid_addr "$bt_addr" || continue
		bt_info=$(bt_ctl "$BT_WAIT" info "$bt_addr" 2>/dev/null)
		if bt_property_yes "$bt_info" Connected && bt_property_yes "$bt_info" Bonded &&
			bt_property_yes "$bt_info" Trusted && bt_audio_sink "$bt_info"; then
			bt_selected=$bt_addr
			break
		fi
	done
	bt_token_current || return 0
	if [ -n "$bt_selected" ]; then
		(umask 077; printf '%s\n' "$bt_selected" > "$BT_RECONNECT_STATE/audio.$$") &&
			mv -f "$BT_RECONNECT_STATE/audio.$$" "$BT_RECONNECT_STATE/audio"
	else
		rm -f "$BT_RECONNECT_STATE/audio"
	fi
}

bt_reconnect_log() {
	logger -t nx-bt-reconnect "$1" 2>/dev/null || :
}

bt_worker_current() {
	if bt_token_current; then
		if [ "${BT_BOOT_MODE:-0}" -ne 1 ]; then return 0; fi
		if [ ! -e "$BT_RECONNECT_STATE/boot-cancel" ] && bt_boot_enabled &&
			[ "$bt_boot_addr" = "$bt_addr" ]; then return 0; fi
	fi
	bt_reconnect_log "cancelled by a newer Bluetooth request"
	return 1
}

bt_worker_time_left() {
	bt_seconds_left "$1" && return 0
	bt_reconnect_log "30-second reconnect window exhausted"
	return 1
}

bt_reconnect_audio() {
	BT_RECONNECT_TOKEN=$1
	if [ "$2" = boot ]; then
		BT_BOOT_MODE=1
		bt_boot_lock && busybox flock -n 8 || return 0
		# launch.sh can be re-entered: at most one boot window per OS boot.
		mkdir "$BT_RECONNECT_STATE/boot-consumed" 2>/dev/null || return 0
		bt_boot_enabled || return 0
		bt_addr=$bt_boot_addr
	else
		bt_addr=$(cat "$BT_RECONNECT_STATE/audio" 2>/dev/null)
	fi
	bt_valid_addr "$bt_addr" && bt_worker_current || return 0
	bt_reconnect_log "starting bounded audio recall"
	# One bounded window per stack start; never compete continuously with phones.
	BT_DEADLINE=$(( $(bt_now) + 30 ))
	bt_ready_deadline=$(( $(bt_now) + 10 ))
	bt_ready=0
	while bt_worker_current && bt_worker_time_left 1; do
		bt_show=$(bt_ctl "$BT_WAIT" show 2>/dev/null)
		if bt_property_yes "$bt_show" Powered &&
			bt_run_bounded 1 dbus-send --system --print-reply --reply-timeout=1000 \
			--dest=org.freedesktop.DBus /org/freedesktop/DBus \
			org.freedesktop.DBus.NameHasOwner string:org.bluealsa 2>/dev/null |
			grep -q 'boolean true'; then
			bt_ready=1
			break
		fi
		[ "$(bt_now)" -lt "$bt_ready_deadline" ] || break
		sleep 1
	done
	[ "$bt_ready" -eq 1 ] || { bt_reconnect_log "adapter or BlueALSA readiness timed out"; return 0; }
	bt_attempt=0
	while [ "$bt_attempt" -lt 2 ] && bt_worker_current && bt_worker_time_left 2; do
		bt_info=$(bt_ctl "$BT_WAIT" info "$bt_addr" 2>/dev/null)
		bt_property_yes "$bt_info" Bonded && bt_property_yes "$bt_info" Trusted &&
			bt_audio_sink "$bt_info" || { bt_reconnect_log "saved audio device no longer bonded and trusted"; return 0; }
		bt_property_yes "$bt_info" Connected && { bt_reconnect_log "connection confirmed"; return 0; }
		if [ "${BT_BOOT_MODE:-0}" -eq 1 ]; then
			bt_worker_time_left 2 || return 0
			bt_others=$(bt_ctl "$BT_WAIT" devices Connected 2>/dev/null) || return 0
			for bt_other in $(printf '%s\n' "$bt_others" | awk '$1 == "Device" {print $2}'); do
				[ "$bt_other" != "$bt_addr" ] || continue
				bt_valid_addr "$bt_other" && bt_worker_current && bt_worker_time_left 2 || return 0
				bt_other_info=$(bt_ctl "$BT_WAIT" info "$bt_other" 2>/dev/null) || return 0
				if bt_property_yes "$bt_other_info" Connected && bt_audio_sink "$bt_other_info"; then
					bt_reconnect_log "another audio device is connected; skipping boot recall"
					return 0
				fi
			done
		fi
		bt_worker_current && bt_worker_time_left 8 || return 0
		bt_reconnect_log "connect attempt $((bt_attempt + 1))"
		bt_ctl "$BT_WAIT" connect "$bt_addr" >/dev/null 2>&1
		# bluetoothctl can exit successfully after an HCI failure: inspect state.
		bt_worker_current && bt_worker_time_left 2 || return 0
		bt_info=$(bt_ctl "$BT_WAIT" info "$bt_addr" 2>/dev/null)
		bt_property_yes "$bt_info" Connected && { bt_reconnect_log "connection confirmed"; return 0; }
		bt_attempt=$((bt_attempt + 1))
		[ "$bt_attempt" -lt 2 ] && sleep 1
	done
	bt_reconnect_log "two connect attempts exhausted"
	return 0
}

# The file is normally sourced by bt_init.sh; only workers execute this mode.
case "$1" in
	reconnect_worker) bt_reconnect_audio "$2" "$3" ;;
	boot_enabled) bt_boot_enabled; exit $? ;;
	boot_set) bt_boot_set "$2"; exit $? ;;
	remember_boot) bt_boot_remember "$2"; exit $? ;;
	cancel_boot) bt_boot_cancel; exit $? ;;
	forget_boot)
		bt_boot_cancel || exit 1
		bt_pref_lock || exit 1
		if bt_boot_read && [ "$bt_boot_addr" = "$2" ]; then
			bt_boot_addr=-; bt_boot_write
		fi
		;;
esac
